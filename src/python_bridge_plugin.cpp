/*
 * Python Bridge Plugin for FFmpeg OpenCV Plugin System
 *
 * This plugin embeds a CPython interpreter via pybind11 and delegates
 * frame processing to user-written Python scripts. The Python scripts
 * use numpy arrays that share memory with cv::Mat (zero-copy).
 *
 * Parameters (passed via FFmpeg's params= option, separated by '&'):
 *   script=<path>       - Path to the Python script (required)
 *   class=<name>        - Python class name to instantiate (required)
 *   <key>=<value>       - Additional parameters forwarded to Python plugin
 *
 * Example usage:
 *   ffmpeg -i input.mp4 \
 *     -vf "oc_plugin=plugin=libpython_bridge_plugin.so:params=script=blur.py&class=BlurPlugin&ksize=15" \
 *     output.mp4
 *
 * The Python class should inherit from quink_plugin.ProcessPluginBase or
 * quink_plugin.DetectPluginBase (provided in python_sdk/quink_plugin.py).
 */

#include <atomic>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <pybind11/embed.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "quink_oc_plugin.h"

namespace py = pybind11;

static std::atomic<int> s_module_counter{0};

// parse "key=value&key2=value2" parameter strings
static std::unordered_map<std::string, std::string> parseParams(
    const char *params) {
    std::unordered_map<std::string, std::string> result;
    if (!params || !params[0])
        return result;

    std::string s(params);
    size_t pos = 0;
    while (pos < s.size()) {
        size_t amp = s.find('&', pos);
        if (amp == std::string::npos)
            amp = s.size();
        std::string token = s.substr(pos, amp - pos);
        size_t eq = token.find('=');
        if (eq != std::string::npos) {
            result[token.substr(0, eq)] = token.substr(eq + 1);
        }
        pos = amp + 1;
    }
    return result;
}

/* cv::Mat <-> numpy.ndarray zero-copy conversion
 *
 * The returned array does NOT own the data; the Mat must outlive it.
 * Only CV_8UC3 (BGR24) and CV_8UC4 (BGRA) are used by FFmpeg's CPU path.
 */
static py::array matToNumpy(cv::Mat &mat) {
    if (mat.empty())
        return py::array();

    if (mat.depth() != CV_8U)
        throw std::runtime_error("Unsupported cv::Mat depth: " +
                                 std::to_string(mat.depth()) +
                                 " (only CV_8U is supported)");

    int channels = mat.channels();
    std::vector<py::ssize_t> shape = {mat.rows, mat.cols, channels};
    std::vector<py::ssize_t> strides = {
        (py::ssize_t) mat.step[0],
        (py::ssize_t) mat.step[1],
        (py::ssize_t) mat.elemSize1()
    };

    return py::array_t<uint8_t>(shape, strides, mat.data, py::none());
}

static py::array matToNumpyReadonly(const cv::Mat &mat) {
    py::array arr = matToNumpy(const_cast<cv::Mat &>(mat));
    reinterpret_cast<py::detail::PyArray_Proxy *>(arr.ptr())->flags &=
            ~py::detail::npy_api::NPY_ARRAY_WRITEABLE_;
    return arr;
}

/* Python interpreter singleton
 *
 * Initialized on first use; intentionally never finalized (the static
 * destructor runs too late for safe teardown in a dlopen'd plugin).
 */
class PythonInterpreter {
public:
    static PythonInterpreter &instance() {
        static PythonInterpreter inst;
        return inst;
    }

    bool isInitialized() const { return initialized_; }

private:
    PythonInterpreter() {
        try {
            py::initialize_interpreter();
            initialized_ = true;
            // Release GIL so subsequent gil_scoped_acquire won't deadlock
            tstate_ = PyEval_SaveThread();
        } catch (const std::exception &e) {
            fprintf(stderr, "[python_bridge] Failed to initialize Python: %s\n",
                    e.what());
        }
    }

    ~PythonInterpreter() = default; // intentionally skip finalize

    PythonInterpreter(const PythonInterpreter &) = delete;

    PythonInterpreter &operator=(const PythonInterpreter &) = delete;

    bool initialized_ = false;
    PyThreadState *tstate_ = nullptr;
};

static std::string getDirectory(const std::string &path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos)
        return ".";
    return path.substr(0, pos);
}

static std::string getModuleName(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    std::string filename = (slash == std::string::npos)
                               ? path
                               : path.substr(slash + 1);
    // Strip .py extension
    size_t dot = filename.rfind(".py");
    if (dot != std::string::npos && dot + 3 == filename.size())
        filename = filename.substr(0, dot);
    return filename;
}

/// Build "key1=val1&key2=val2" from all keys except "script" and "class".
static std::string buildUserParams(
    const std::unordered_map<std::string, std::string> &params) {
    std::string result;
    for (auto &kv: params) {
        if (kv.first == "script" || kv.first == "class")
            continue;
        if (!result.empty())
            result += "&";
        result += kv.first + "=" + kv.second;
    }
    return result;
}

/* Load Python module from file path using importlib.
 *
 * Each call assigns a unique internal name (_pybridge_<name>_<N>) to avoid
 * sys.modules cache collisions when multiple instances load same-named scripts.
 */
static py::module_ loadModuleFromPath(const std::string &script_path,
                                      const std::string &module_name) {
    int id = s_module_counter.fetch_add(1);
    std::string unique_name = "_pybridge_" + module_name + "_" +
                              std::to_string(id);

    py::module_ importlib_util = py::module_::import("importlib.util");
    py::object spec = importlib_util.attr("spec_from_file_location")(
        unique_name, script_path);

    if (spec.is_none())
        throw std::runtime_error(
            "Failed to create module spec for: " + script_path);

    py::object module = importlib_util.attr("module_from_spec")(spec);

    py::module_ sys = py::module_::import("sys");
    sys.attr("modules")[py::str(unique_name)] = module;
    spec.attr("loader").attr("exec_module")(module);

    return py::reinterpret_borrow<py::module_>(module);
}

class PythonProcessBridge : public quink::ProcessPlugin {
public:
    ~PythonProcessBridge() override {
        if (py_plugin_.ptr() != nullptr) {
            py::gil_scoped_acquire gil;
            py_plugin_ = py::object{};
        }
    }

    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        auto &interp = PythonInterpreter::instance();
        if (!interp.isInitialized()) {
            fprintf(
                stderr, "[python_bridge] Python interpreter not available\n");
            return false;
        }

        auto kv = parseParams(params);
        auto script_it = kv.find("script");
        auto class_it = kv.find("class");
        if (script_it == kv.end() || class_it == kv.end()) {
            fprintf(
                stderr,
                "[python_bridge] Missing required params: script=<path>&class=<name>\n");
            return false;
        }

        script_path_ = script_it->second;
        class_name_ = class_it->second;
        user_params_ = buildUserParams(kv);
        nb_inputs_ = nb_inputs;
        nb_outputs_ = nb_outputs;

        try {
            py::gil_scoped_acquire gil;

            py::module_ sys = py::module_::import("sys");
            py::list path = sys.attr("path");
            std::string dir = getDirectory(script_path_);
            if (!path.attr("__contains__")(dir).cast<bool>())
                path.attr("insert")(0, dir);

            std::string module_name = getModuleName(script_path_);
            py::module_ user_module = loadModuleFromPath(
                script_path_, module_name);
            py_plugin_ = user_module.attr(class_name_.c_str())();
            if (py::hasattr(py_plugin_, "init")) {
                bool ok = py_plugin_.attr("init")(
                            user_params_, nb_inputs, nb_outputs)
                        .cast<bool>();
                if (!ok) {
                    fprintf(
                        stderr,
                        "[python_bridge] Python plugin init() returned false\n");
                    return false;
                }
            }

            return true;
        } catch (py::error_already_set &e) {
            fprintf(stderr, "[python_bridge] Python init error: %s\n",
                    e.what());
            return false;
        }
    }

    quink::ProcessResult process(const std::vector<cv::Mat> &inputs,
                                 std::vector<quink::ProcessOutput> &
                                 outputs) override {
        try {
            py::gil_scoped_acquire gil;

            py::list py_inputs;
            for (auto &mat: inputs)
                py_inputs.append(matToNumpyReadonly(mat));

            py::list py_outputs;
            for (auto &out: outputs)
                py_outputs.append(matToNumpy(out.frame));

            int result = py_plugin_.attr("process")(py_inputs, py_outputs).cast<
                int>();
            return static_cast<quink::ProcessResult>(result);
        } catch (py::error_already_set &e) {
            fprintf(stderr, "[python_bridge] Python process error: %s\n",
                    e.what());
            return quink::ProcessResult::Error;
        }
    }

    bool flush(std::vector<quink::ProcessOutput> &) override {
        // Python plugins cannot do delayed output (frame buffering) because:
        //   1. numpy arrays do not hold cv::Mat references — data is invalid
        //      after process() returns (np.copy() would work but defeats zero-copy)
        //   2. ref_frame is not exposed to Python, so timestamps would be wrong
        // TryAgain is supported for frame-dropping (e.g., decimation) where no
        // data needs to be saved. Flush always returns false.
        return false;
    }

    bool configure(const std::vector<quink::FrameConfig> &inputs,
                   std::vector<quink::FrameConfig> &outputs) override {
        try {
            py::gil_scoped_acquire gil;

            if (!py::hasattr(py_plugin_, "configure"))
                return true;

            py::list py_inputs;
            for (auto &cfg: inputs) {
                py::dict d;
                d["width"] = cfg.width;
                d["height"] = cfg.height;
                d["cv_type"] = cfg.cv_type;
                d["pix_fmt"] = static_cast<int>(cfg.pix_fmt);
                d["colorspace"] = cfg.colorspace;
                d["limited_range"] = cfg.limited_range;
                py_inputs.append(d);
            }

            py::list py_outputs;
            for (auto &cfg: outputs) {
                py::dict d;
                d["width"] = cfg.width;
                d["height"] = cfg.height;
                d["cv_type"] = cfg.cv_type;
                d["pix_fmt"] = static_cast<int>(cfg.pix_fmt);
                d["colorspace"] = cfg.colorspace;
                d["limited_range"] = cfg.limited_range;
                py_outputs.append(d);
            }

            bool ok = py_plugin_.attr("configure")(py_inputs, py_outputs).cast<
                bool>();

            if (ok) {
                for (size_t i = 0; i < outputs.size() && i < (size_t) py::len(
                                       py_outputs); i++) {
                    py::dict d = py_outputs[i];
                    outputs[i].width = d["width"].cast<int>();
                    outputs[i].height = d["height"].cast<int>();
                }
            }

            return ok;
        } catch (py::error_already_set &e) {
            fprintf(stderr, "[python_bridge] Python configure error: %s\n",
                    e.what());
            return false;
        }
    }

    void uninit() override {
        try {
            py::gil_scoped_acquire gil;

            if (py_plugin_ && py::hasattr(py_plugin_, "uninit"))
                py_plugin_.attr("uninit")();
            py_plugin_ = py::object{};
        } catch (py::error_already_set &e) {
            fprintf(stderr, "[python_bridge] Python uninit error: %s\n",
                    e.what());
        }
    }

private:
    py::object py_plugin_;
    std::string script_path_;
    std::string class_name_;
    std::string user_params_;
    int nb_inputs_ = 0;
    int nb_outputs_ = 0;
};

QUINK_OC_PROCESS_PLUGIN_ENTRY(PythonProcessBridge, "python_bridge",
                              "Python OpenCV plugin bridge (process)")

class PythonDetectBridge : public quink::DetectPlugin {
public:
    ~PythonDetectBridge() override {
        if (py_plugin_.ptr() != nullptr) {
            py::gil_scoped_acquire gil;
            py_plugin_ = py::object{};
        }
    }

    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        if (nb_inputs != 1 || nb_outputs != 1)
            return false;

        auto &interp = PythonInterpreter::instance();
        if (!interp.isInitialized()) {
            fprintf(
                stderr, "[python_bridge] Python interpreter not available\n");
            return false;
        }

        auto kv = parseParams(params);
        auto script_it = kv.find("script");
        auto class_it = kv.find("class");
        if (script_it == kv.end() || class_it == kv.end()) {
            fprintf(
                stderr,
                "[python_bridge] Missing required params: script=<path>&class=<name>\n");
            return false;
        }

        script_path_ = script_it->second;
        class_name_ = class_it->second;
        user_params_ = buildUserParams(kv);

        try {
            py::gil_scoped_acquire gil;

            py::module_ sys = py::module_::import("sys");
            py::list path = sys.attr("path");
            std::string dir = getDirectory(script_path_);
            if (!path.attr("__contains__")(dir).cast<bool>())
                path.attr("insert")(0, dir);

            std::string module_name = getModuleName(script_path_);
            py::module_ user_module = loadModuleFromPath(
                script_path_, module_name);
            py_plugin_ = user_module.attr(class_name_.c_str())();

            if (py::hasattr(py_plugin_, "init")) {
                bool ok = py_plugin_.attr("init")(user_params_, 1, 1).cast<
                    bool>();
                if (!ok) {
                    fprintf(
                        stderr,
                        "[python_bridge] Python detect plugin init() returned false\n");
                    return false;
                }
            }

            return true;
        } catch (py::error_already_set &e) {
            fprintf(stderr, "[python_bridge] Python detect init error: %s\n",
                    e.what());
            return false;
        }
    }

    quink::ProcessResult detect(const cv::Mat &input, cv::Mat &output,
                                quink::Detections &detections) override {
        try {
            py::gil_scoped_acquire gil;

            py::array py_input = matToNumpyReadonly(input);
            py::array py_output = matToNumpy(output);
            py::tuple result = py_plugin_.attr("detect")(py_input, py_output).
                    cast<py::tuple>();
            int code = result[0].cast<int>();

            detections.clear();
            if (code == 0 && py::len(result) > 1) {
                py::dict det_dict = result[1].cast<py::dict>();
                parseDetections(det_dict, detections);
            }

            return static_cast<quink::ProcessResult>(code);
        } catch (py::error_already_set &e) {
            fprintf(stderr, "[python_bridge] Python detect error: %s\n",
                    e.what());
            return quink::ProcessResult::Error;
        }
    }

    bool flushDetect(cv::Mat &output, quink::Detections &detections) override {
        try {
            py::gil_scoped_acquire gil;

            if (!py::hasattr(py_plugin_, "flush_detect"))
                return false;

            py::array py_output = matToNumpy(output);
            py::object result = py_plugin_.attr("flush_detect")(py_output);

            if (result.is_none() || !result.cast<bool>())
                return false;

            if (py::isinstance<py::tuple>(result)) {
                py::tuple tup = result.cast<py::tuple>();
                if (py::len(tup) > 1) {
                    detections.clear();
                    parseDetections(tup[1].cast<py::dict>(), detections);
                }
            }

            return true;
        } catch (py::error_already_set &e) {
            fprintf(stderr, "[python_bridge] Python flush_detect error: %s\n",
                    e.what());
            return false;
        }
    }

    void uninit() override {
        try {
            py::gil_scoped_acquire gil;
            if (py_plugin_ && py::hasattr(py_plugin_, "uninit"))
                py_plugin_.attr("uninit")();
            py_plugin_ = py::object{};
        } catch (py::error_already_set &e) {
            fprintf(stderr, "[python_bridge] Python detect uninit error: %s\n",
                    e.what());
        }
    }

private:
    void parseDetections(const py::dict &det_dict,
                         quink::Detections &detections) {
        if (!det_dict.contains("boxes"))
            return;

        py::list boxes = det_dict["boxes"].cast<py::list>();
        py::list class_ids = det_dict["class_ids"].cast<py::list>();
        py::list confidences = det_dict["confidences"].cast<py::list>();

        bool has_labels = det_dict.contains("labels");
        py::list labels;
        if (has_labels)
            labels = det_dict["labels"].cast<py::list>();

        size_t n = py::len(boxes);
        for (size_t i = 0; i < n; i++) {
            py::tuple box = boxes[i].cast<py::tuple>();
            int x = box[0].cast<int>();
            int y = box[1].cast<int>();
            int w = box[2].cast<int>();
            int h = box[3].cast<int>();

            int cls_id = class_ids[i].cast<int>();
            float conf = confidences[i].cast<float>();
            std::string label = has_labels ? labels[i].cast<std::string>() : "";

            detections.add(cv::Rect(x, y, w, h), cls_id, conf, label);
        }
    }

    py::object py_plugin_;
    std::string script_path_;
    std::string class_name_;
    std::string user_params_;
};
