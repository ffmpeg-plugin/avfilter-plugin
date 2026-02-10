/*
 * ONNX Runtime Inference Plugin (CPU)
 *
 * Generic single-input single-output model inference plugin.
 * Handles BGR/BGRA uint8 -> RGB float32 preprocessing and reverse postprocessing.
 * Supports tiled inference with configurable overlap for models with fixed input size.
 *
 * Parameters (passed via params string, key=value separated by &):
 *   model=<path>          - Path to ONNX model file (required)
 *   input=<name>          - Model input tensor name (default: "input")
 *   output=<name>         - Model output tensor name (default: "output")
 *   tile_h=<int>          - Tile height (auto-detected from model, or image height)
 *   tile_w=<int>          - Tile width  (auto-detected from model, or image width)
 *   overlap=<int>         - Tile overlap in pixels (default: tile_size/4)
 *   scale=<int>           - Output scale factor (auto-detected via probe inference
 *                           if not specified; e.g., scale=4 for 4x super-resolution)
 *   <param_name>=<float>  - Additional scalar float parameters fed to the model
 *                           (e.g., noise=0.24&blur=1.0)
 */

#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <iostream>

#include <onnxruntime_cxx_api.h>
#include <opencv2/imgproc.hpp>

#include "quink_oc_plugin.h"

namespace {

/* ── Parameter parsing ──────────────────────────────────────────────── */

struct OnnxInferParams {
    std::string model_path;
    std::string input_name  = "input";
    std::string output_name = "output";
    int tile_h = -1;  /* -1 means auto-detect from model or image */
    int tile_w = -1;
    int overlap = -1; /* -1 means auto: tile_size / 4 */
    int scale  = -1;  /* -1 means auto-detect via model shape or probe inference */

    /* Extra scalar float parameters (e.g. noise, blur) */
    std::map<std::string, float> extra_params;

    bool parse(const char *params) {
        if (!params || !params[0])
            return false; /* model path is required */

        std::string s(params);

        /* Strip surrounding quotes that ffmpeg filter option parser may leave */
        while (!s.empty() && (s.front() == '"' || s.front() == '\''))
            s.erase(s.begin());
        while (!s.empty() && (s.back() == '"' || s.back() == '\''))
            s.pop_back();

        size_t pos = 0;
        while (pos < s.size()) {
            size_t amp = s.find('&', pos);
            if (amp == std::string::npos) amp = s.size();
            std::string token = s.substr(pos, amp - pos);
            pos = amp + 1;

            size_t eq = token.find('=');
            if (eq == std::string::npos) continue;
            std::string key = token.substr(0, eq);
            std::string val = token.substr(eq + 1);

            if (key == "model")       model_path = val;
            else if (key == "input")  input_name = val;
            else if (key == "output") output_name = val;
            else if (key == "tile_h") tile_h = std::stoi(val);
            else if (key == "tile_w") tile_w = std::stoi(val);
            else if (key == "overlap") overlap = std::stoi(val);
            else if (key == "scale")  scale = std::stoi(val);
            else {
                /* Treat everything else as a float parameter for the model */
                try {
                    extra_params[key] = std::stof(val);
                } catch (const std::exception &e) {
                    std::cerr << "[onnx_infer] WARNING: ignoring non-float param: "
                              << key << "=" << val << std::endl;
                }
            }
        }
        return !model_path.empty();
    }
};

/* ── Weight mask for tile blending ──────────────────────────────────── */

/**
 * Create a weight mask for blending overlapping tiles.
 *
 * The mask has value 1.0 in the interior (non-overlapping region) and
 * linearly ramps from 0→1 across the overlap margin. The minimum value
 * is clamped to a small positive epsilon so no pixel ever has zero weight.
 *
 * When overlap=0, the mask is all 1.0 (no blending needed).
 */
static cv::Mat createWeightMask(int h, int w, int overlap) {
    cv::Mat mask(h, w, CV_32FC1, cv::Scalar(1.0f));
    if (overlap <= 0)
        return mask;

    /* For each edge, ramp linearly over 'overlap' pixels: 1/(overlap+1) → 1 */
    for (int y = 0; y < h; y++) {
        /* Vertical ramp: distance from top/bottom edge */
        float wy = 1.0f;
        if (y < overlap)
            wy = static_cast<float>(y + 1) / (overlap + 1);
        else if (y >= h - overlap)
            wy = static_cast<float>(h - y) / (overlap + 1);

        for (int x = 0; x < w; x++) {
            /* Horizontal ramp: distance from left/right edge */
            float wx = 1.0f;
            if (x < overlap)
                wx = static_cast<float>(x + 1) / (overlap + 1);
            else if (x >= w - overlap)
                wx = static_cast<float>(w - x) / (overlap + 1);

            mask.at<float>(y, x) = wy * wx;
        }
    }
    return mask;
}

/* ── ONNX Runtime inference wrapper ─────────────────────────────────── */

class OnnxSession {
public:
    bool init(const std::string &model_path) {
        try {
            env_ = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "onnx_infer_plugin");
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(4);
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            appendHardwareProviders(opts);

            session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), opts);

            Ort::AllocatorWithDefaultOptions alloc;
            size_t num_inputs = session_->GetInputCount();
            for (size_t i = 0; i < num_inputs; i++) {
                auto name = session_->GetInputNameAllocated(i, alloc);
                auto info = session_->GetInputTypeInfo(i);
                auto shape = info.GetTensorTypeAndShapeInfo().GetShape();
                input_names_.push_back(name.get());
                input_shapes_.push_back(shape);
            }

            size_t num_outputs = session_->GetOutputCount();
            for (size_t i = 0; i < num_outputs; i++) {
                auto name = session_->GetOutputNameAllocated(i, alloc);
                auto info = session_->GetOutputTypeInfo(i);
                auto shape = info.GetTensorTypeAndShapeInfo().GetShape();
                output_names_.push_back(name.get());
                output_shapes_.push_back(shape);
            }

            return true;
        } catch (const Ort::Exception &e) {
            std::cerr << "OnnxSession init error: " << e.what() << std::endl;
            return false;
        }
    }

    /**
     * Query the model's input tensor spatial size (shape [1, H, W, 3]).
     * Returns true with valid dimensions if the model has fixed input shape.
     * Returns false if the tensor is not found, has wrong rank, or uses
     * dynamic dimensions (ONNX represents these as -1).
     */
    bool getInputTileSize(const std::string &input_name, int &tile_h, int &tile_w) const {
        for (size_t i = 0; i < input_names_.size(); i++) {
            if (input_names_[i] == input_name && input_shapes_[i].size() == 4) {
                int64_t h = input_shapes_[i][1];
                int64_t w = input_shapes_[i][2];
                if (h > 0 && w > 0) {
                    tile_h = static_cast<int>(h);
                    tile_w = static_cast<int>(w);
                    return true;
                }
                /* Dynamic shape: h or w is -1 (or 0) */
                return false;
            }
        }
        return false;
    }

    /**
     * Query the model's output tensor spatial size (shape [1, H, W, 3]).
     * Returns true with valid dimensions if the model has fixed output shape.
     * Returns false for dynamic output shapes.
     */
    bool getOutputTileSize(const std::string &output_name, int &tile_h, int &tile_w) const {
        for (size_t i = 0; i < output_names_.size(); i++) {
            if (output_names_[i] == output_name && output_shapes_[i].size() == 4) {
                int64_t h = output_shapes_[i][1];
                int64_t w = output_shapes_[i][2];
                if (h > 0 && w > 0) {
                    tile_h = static_cast<int>(h);
                    tile_w = static_cast<int>(w);
                    return true;
                }
                return false;
            }
        }
        return false;
    }

    /**
     * Run a probe inference with a small tile to detect the output scale factor.
     * Returns the scale ratio (output_h / input_h). Returns 0 on failure.
     */
    int probeScale(const std::string &input_name, int probe_h, int probe_w,
                   const std::map<std::string, float> &extra_params,
                   const std::string &output_name) {
        try {
            Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(
                OrtArenaAllocator, OrtMemTypeDefault);

            std::vector<float> dummy(probe_h * probe_w * 3, 0.5f);
            std::vector<Ort::Value> inputs;
            std::vector<const char *> input_name_ptrs;
            std::vector<std::vector<float>> param_storage;

            for (size_t i = 0; i < input_names_.size(); i++) {
                const std::string &name = input_names_[i];
                input_name_ptrs.push_back(name.c_str());
                if (name == input_name) {
                    std::array<int64_t, 4> shape = {1, probe_h, probe_w, 3};
                    inputs.push_back(Ort::Value::CreateTensor<float>(
                        mem, dummy.data(), dummy.size(),
                        shape.data(), shape.size()));
                } else {
                    auto it = extra_params.find(name);
                    float val = (it != extra_params.end()) ? it->second : 0.0f;
                    param_storage.push_back({val});
                    auto &buf = param_storage.back();
                    std::vector<int64_t> shape;
                    for (auto d : input_shapes_[i]) shape.push_back(d);
                    inputs.push_back(Ort::Value::CreateTensor<float>(
                        mem, buf.data(), buf.size(),
                        shape.data(), shape.size()));
                }
            }

            int out_idx = -1;
            std::vector<const char *> output_name_ptrs;
            for (size_t i = 0; i < output_names_.size(); i++) {
                output_name_ptrs.push_back(output_names_[i].c_str());
                if (output_names_[i] == output_name) out_idx = static_cast<int>(i);
            }
            if (out_idx < 0) return 0;

            auto results = session_->Run(Ort::RunOptions{nullptr},
                input_name_ptrs.data(), inputs.data(), inputs.size(),
                output_name_ptrs.data(), output_name_ptrs.size());

            auto shape = results[out_idx].GetTensorTypeAndShapeInfo().GetShape();
            /* Expected output shape: [1, out_h, out_w, 3] */
            if (shape.size() == 4 && shape[1] > 0 && shape[2] > 0) {
                int sh = static_cast<int>(shape[1]) / probe_h;
                int sw = static_cast<int>(shape[2]) / probe_w;
                if (sh == sw && sh >= 1) return sh;
            }
            return 0;
        } catch (const Ort::Exception &e) {
            std::cerr << "OnnxSession probe error: " << e.what() << std::endl;
            return 0;
        }
    }

    /**
     * Run inference with one image tile and optional scalar params.
     *
     * @param input_name    Name of image input tensor
     * @param tile_data     Pointer to float32 data [1, in_h, in_w, C]
     * @param in_h, in_w    Input tile spatial size
     * @param channels      Number of channels (3)
     * @param extra_params  Map of scalar float params (name -> value)
     * @param output_name   Name of output tensor to retrieve
     * @param out_data      Output float buffer (caller-allocated, out_h * out_w * C)
     * @param out_h, out_w  Output tile spatial size (may differ for super-res)
     * @return true on success
     */
    bool run(const std::string &input_name,
             const float *tile_data, int in_h, int in_w, int channels,
             const std::map<std::string, float> &extra_params,
             const std::string &output_name,
             float *out_data, int out_h, int out_w) {
        try {
            Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(
                OrtArenaAllocator, OrtMemTypeDefault);

            std::vector<Ort::Value> inputs;
            std::vector<const char *> input_name_ptrs;
            /* Keep string storage alive during run */
            std::vector<std::vector<float>> param_storage;

            for (size_t i = 0; i < input_names_.size(); i++) {
                const std::string &name = input_names_[i];
                input_name_ptrs.push_back(name.c_str());

                if (name == input_name) {
                    /* Image tensor */
                    std::array<int64_t, 4> shape = {1, in_h, in_w, channels};
                    size_t count = in_h * in_w * channels;
                    inputs.push_back(Ort::Value::CreateTensor<float>(
                        mem, const_cast<float *>(tile_data), count,
                        shape.data(), shape.size()));
                } else {
                    /* Scalar parameter */
                    auto it = extra_params.find(name);
                    float val = (it != extra_params.end()) ? it->second : 0.0f;
                    param_storage.push_back({val});
                    auto &buf = param_storage.back();
                    std::vector<int64_t> shape;
                    for (auto d : input_shapes_[i]) shape.push_back(d);
                    inputs.push_back(Ort::Value::CreateTensor<float>(
                        mem, buf.data(), buf.size(),
                        shape.data(), shape.size()));
                }
            }

            /* Output names */
            int out_idx = -1;
            std::vector<const char *> output_name_ptrs;
            for (size_t i = 0; i < output_names_.size(); i++) {
                output_name_ptrs.push_back(output_names_[i].c_str());
                if (output_names_[i] == output_name) out_idx = static_cast<int>(i);
            }
            if (out_idx < 0) return false;

            auto results = session_->Run(Ort::RunOptions{nullptr},
                input_name_ptrs.data(), inputs.data(), inputs.size(),
                output_name_ptrs.data(), output_name_ptrs.size());

            const float *result_data = results[out_idx].GetTensorData<float>();
            size_t count = out_h * out_w * channels;
            std::memcpy(out_data, result_data, count * sizeof(float));

            return true;
        } catch (const Ort::Exception &e) {
            std::cerr << "OnnxSession run error: " << e.what() << std::endl;
            return false;
        }
    }

    /**
     * Try to register all available hardware execution providers, highest
     * priority first.  Each EP is wrapped in its own try/catch so that an
     * unavailable backend is silently skipped.  CPU is always the final
     * fallback (registered automatically by ORT).
     *
     * Priority order:
     *   NVIDIA  – TensorRT > CUDA
     *   Apple   – CoreML
     *   Intel   – OpenVINO
     *   AMD     – MIGraphX > ROCm
     *   Generic – DirectML (Windows GPU), XNNPACK (mobile/embedded)
     */
    static void appendHardwareProviders(Ort::SessionOptions &opts) {
        /* ── NVIDIA TensorRT ─────────────────────────────────────────── */
#ifdef USE_TENSORRT
        try {
            OrtTensorRTProviderOptions trt_opts{};
            trt_opts.device_id = 0;
            opts.AppendExecutionProvider_TensorRT(trt_opts);
        } catch (...) {}
#endif

        /* ── NVIDIA CUDA ─────────────────────────────────────────────── */
#ifdef USE_CUDA
        try {
            OrtCUDAProviderOptions cuda_opts{};
            cuda_opts.device_id = 0;
            opts.AppendExecutionProvider_CUDA(cuda_opts);
        } catch (...) {}
#endif

        /* ── Apple CoreML ────────────────────────────────────────────── */
#ifdef __APPLE__
        try {
            std::unordered_map<std::string, std::string> coreml_opts;
            opts.AppendExecutionProvider("CoreML", coreml_opts);
        } catch (...) {}
#endif

        /* ── Intel OpenVINO ──────────────────────────────────────────── */
#ifdef USE_OPENVINO
        try {
            opts.AppendExecutionProvider_OpenVINO_V2();
        } catch (...) {}
#endif

        /* ── AMD MIGraphX ────────────────────────────────────────────── */
#ifdef USE_MIGRAPHX
        try {
            OrtMIGraphXProviderOptions migx_opts{};
            migx_opts.device_id = 0;
            opts.AppendExecutionProvider_MIGraphX(migx_opts);
        } catch (...) {}
#endif

        /* ── AMD ROCm ────────────────────────────────────────────────── */
#ifdef USE_ROCM
        try {
            OrtROCMProviderOptions rocm_opts{};
            rocm_opts.device_id = 0;
            opts.AppendExecutionProvider_ROCM(rocm_opts);
        } catch (...) {}
#endif

        /* ── DirectML (Windows GPU – AMD/Intel/NVIDIA) ───────────────── */
#ifdef USE_DIRECTML
        try {
            std::unordered_map<std::string, std::string> dml_opts;
            dml_opts["device_id"] = "0";
            opts.AppendExecutionProvider("DML", dml_opts);
        } catch (...) {}
#endif

        /* ── XNNPACK (optimized CPU for ARM / mobile) ────────────────── */
#ifdef USE_XNNPACK
        try {
            std::unordered_map<std::string, std::string> xnn_opts;
            opts.AppendExecutionProvider("XNNPACK", xnn_opts);
        } catch (...) {}
#endif

        /* CPU is always available as the final fallback (implicit). */
    }

private:
    Ort::Env env_{nullptr};
    std::unique_ptr<Ort::Session> session_;
    std::vector<std::string> input_names_;
    std::vector<std::vector<int64_t>> input_shapes_;
    std::vector<std::string> output_names_;
    std::vector<std::vector<int64_t>> output_shapes_;
};

} // anonymous namespace

/* ── CPU Process Plugin ─────────────────────────────────────────────── */

class OnnxInferPlugin : public quink::ProcessPlugin {
public:
    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        if (nb_inputs != 1 || nb_outputs != 1)
            return false;
        if (!params_.parse(params))
            return false;
        if (!session_.init(params_.model_path))
            return false;

        /* --- Resolve input tile size ---
         *
         * Priority:
         *   1. Model has fixed input shape → use model shape.
         *      If user also set tile_h/tile_w, warn and use model shape.
         *   2. Model has dynamic shape + user set tile_h/tile_w → use user values.
         *   3. Model has dynamic shape + user did not set → defer to image size
         *      (tile_h_/tile_w_ = 0, resolved in process()).
         */
        int model_in_h = 0, model_in_w = 0;
        bool model_has_fixed_input = session_.getInputTileSize(
            params_.input_name, model_in_h, model_in_w);

        if (model_has_fixed_input) {
            tile_h_ = model_in_h;
            tile_w_ = model_in_w;
            if (params_.tile_h > 0 || params_.tile_w > 0) {
                std::cerr << "[onnx_infer] WARNING: model has fixed input shape "
                          << model_in_w << "x" << model_in_h
                          << ", ignoring user tile_h/tile_w" << std::endl;
            }
        } else if (params_.tile_h > 0 && params_.tile_w > 0) {
            tile_h_ = params_.tile_h;
            tile_w_ = params_.tile_w;
            std::cout << "[onnx_infer] Model has dynamic input shape, "
                      << "using user tile size: "
                      << tile_w_ << "x" << tile_h_ << std::endl;
        } else {
            /* Will use image size at process time (no tiling) */
            tile_h_ = 0;
            tile_w_ = 0;
            std::cout << "[onnx_infer] Model has dynamic input shape, "
                      << "will use image size as tile (no tiling)" << std::endl;
        }

        /* --- Resolve output tile size and scale factor --- */
        int model_out_h = 0, model_out_w = 0;
        bool model_has_fixed_output = session_.getOutputTileSize(
            params_.output_name, model_out_h, model_out_w);

        if (model_has_fixed_output && tile_h_ > 0) {
            out_tile_h_ = model_out_h;
            out_tile_w_ = model_out_w;
            scale_h_ = out_tile_h_ / tile_h_;
            scale_w_ = out_tile_w_ / tile_w_;
        } else if (params_.scale > 0) {
            /* User explicitly specified scale factor */
            scale_h_ = params_.scale;
            scale_w_ = params_.scale;
            out_tile_h_ = tile_h_ * scale_h_;  /* may be 0 */
            out_tile_w_ = tile_w_ * scale_w_;
        } else if (tile_h_ > 0) {
            /* Both shapes are dynamic but tile size is known:
             * run a probe inference to detect scale factor */
            int probe_scale = session_.probeScale(
                params_.input_name, tile_h_, tile_w_,
                params_.extra_params, params_.output_name);
            if (probe_scale > 1) {
                scale_h_ = probe_scale;
                scale_w_ = probe_scale;
                out_tile_h_ = tile_h_ * scale_h_;
                out_tile_w_ = tile_w_ * scale_w_;
                std::cout << "[onnx_infer] Probe inference detected "
                          << probe_scale << "x super-resolution" << std::endl;
            } else {
                /* probe_scale == 1 or 0 (failure): assume 1x */
                out_tile_h_ = tile_h_;
                out_tile_w_ = tile_w_;
                scale_h_ = 1;
                scale_w_ = 1;
            }
        } else {
            /* Fully dynamic (no tile size): try probe with a small test size */
            int probe_h = 64, probe_w = 64;
            int probe_scale = session_.probeScale(
                params_.input_name, probe_h, probe_w,
                params_.extra_params, params_.output_name);
            if (probe_scale > 1) {
                scale_h_ = probe_scale;
                scale_w_ = probe_scale;
                std::cout << "[onnx_infer] Probe inference detected "
                          << probe_scale << "x super-resolution" << std::endl;
            } else {
                scale_h_ = 1;
                scale_w_ = 1;
            }
            out_tile_h_ = 0;
            out_tile_w_ = 0;
        }
        if (scale_h_ < 1) scale_h_ = 1;
        if (scale_w_ < 1) scale_w_ = 1;

        /* --- Resolve overlap --- */
        if (params_.overlap < 0 && tile_h_ > 0)
            params_.overlap = tile_h_ / 4;
        if (params_.overlap < 0)
            params_.overlap = 0;  /* will be recalculated in process() for dynamic case */

        /* Pre-create weight mask (only if tile size is known) */
        if (out_tile_h_ > 0 && out_tile_w_ > 0)
            weight_mask_ = createWeightMask(out_tile_h_, out_tile_w_,
                                            params_.overlap * scale_h_);

        /* --- Info message --- */
        std::cout << "[onnx_infer] Tile: "
                  << (tile_w_ > 0 ? std::to_string(tile_w_) : "dynamic") << "x"
                  << (tile_h_ > 0 ? std::to_string(tile_h_) : "dynamic");
        if (scale_h_ > 1 || scale_w_ > 1)
            std::cout << " -> " << out_tile_w_ << "x" << out_tile_h_
                      << " (scale " << scale_w_ << "x" << scale_h_ << ")";
        std::cout << ", Overlap: " << params_.overlap << std::endl;

        return true;
    }

    quink::ProcessResult process(const std::vector<cv::Mat> &inputs,
                                 std::vector<quink::ProcessOutput> &outputs) override {
        if (inputs.empty() || outputs.empty())
            return quink::ProcessResult::Error;

        const cv::Mat &in = inputs[0];
        cv::Mat &out = outputs[0].frame;

        /* Determine channel count and whether we need alpha handling */
        int in_channels = in.channels();
        bool has_alpha = (in_channels == 4);

        /* Convert BGR(A) uint8 -> RGB float32 [0, 1] */
        cv::Mat rgb_float;
        cv::Mat alpha_channel;

        if (has_alpha) {
            /* BGRA -> separate alpha + BGR -> RGB */
            std::vector<cv::Mat> bgra_planes;
            cv::split(in, bgra_planes);
            alpha_channel = bgra_planes[3]; /* preserve alpha */

            cv::Mat bgr;
            cv::merge(std::vector<cv::Mat>{bgra_planes[0], bgra_planes[1], bgra_planes[2]}, bgr);
            cv::Mat rgb;
            cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
            rgb.convertTo(rgb_float, CV_32FC3, 1.0 / 255.0);
        } else {
            cv::Mat rgb;
            cv::cvtColor(in, rgb, cv::COLOR_BGR2RGB);
            rgb.convertTo(rgb_float, CV_32FC3, 1.0 / 255.0);
        }

        /* Resolve tile size for this frame (handles dynamic-shape models) */
        int h = rgb_float.rows;
        int w = rgb_float.cols;
        int cur_tile_h = tile_h_;
        int cur_tile_w = tile_w_;
        int cur_out_tile_h = out_tile_h_;
        int cur_out_tile_w = out_tile_w_;
        int cur_scale_h = scale_h_;
        int cur_scale_w = scale_w_;
        int overlap = params_.overlap;

        if (cur_tile_h <= 0 || cur_tile_w <= 0) {
            /* Dynamic shape: use entire image, no tiling */
            cur_tile_h = h;
            cur_tile_w = w;
            cur_out_tile_h = h * cur_scale_h;
            cur_out_tile_w = w * cur_scale_w;
            overlap = 0;
        }

        int step_h = cur_tile_h - overlap;
        int step_w = cur_tile_w - overlap;

        if (step_h <= 0 || step_w <= 0)
            return quink::ProcessResult::Error;

        int n_tiles_y = std::max(1, (h - overlap + step_h - 1) / step_h);
        int n_tiles_x = std::max(1, (w - overlap + step_w - 1) / step_w);
        int padded_h = (n_tiles_y - 1) * step_h + cur_tile_h;
        int padded_w = (n_tiles_x - 1) * step_w + cur_tile_w;

        /* Output dimensions: scaled by super-resolution factor */
        int out_overlap_h = overlap * cur_scale_h;
        int out_overlap_w = overlap * cur_scale_w;
        int out_step_h = cur_out_tile_h - out_overlap_h;
        int out_step_w = cur_out_tile_w - out_overlap_w;
        int out_padded_h = (n_tiles_y - 1) * out_step_h + cur_out_tile_h;
        int out_padded_w = (n_tiles_x - 1) * out_step_w + cur_out_tile_w;

        /* Weight mask: use pre-computed if tile size matches, else create on the fly */
        cv::Mat wt_mask;
        if (!weight_mask_.empty() &&
            weight_mask_.rows == cur_out_tile_h &&
            weight_mask_.cols == cur_out_tile_w) {
            wt_mask = weight_mask_;
        } else {
            wt_mask = createWeightMask(cur_out_tile_h, cur_out_tile_w,
                                       overlap * cur_scale_h);
        }

        /* Pad with reflect */
        cv::Mat padded;
        int pad_b = padded_h - h;
        int pad_r = padded_w - w;
        if (pad_b > 0 || pad_r > 0) {
            cv::copyMakeBorder(rgb_float, padded, 0, pad_b, 0, pad_r,
                               cv::BORDER_REFLECT);
        } else {
            padded = rgb_float;
        }

        /* Accumulation buffers (in output resolution) */
        cv::Mat output_acc = cv::Mat::zeros(out_padded_h, out_padded_w, CV_32FC3);
        cv::Mat weight_acc = cv::Mat::zeros(out_padded_h, out_padded_w, CV_32FC1);

        std::vector<float> tile_buf(cur_tile_h * cur_tile_w * 3);
        std::vector<float> out_buf(cur_out_tile_h * cur_out_tile_w * 3);

        for (int ty = 0; ty < n_tiles_y; ty++) {
            for (int tx = 0; tx < n_tiles_x; tx++) {
                int y = ty * step_h;
                int x = tx * step_w;

                cv::Mat tile = padded(cv::Rect(x, y, cur_tile_w, cur_tile_h));

                /* cv::Mat -> contiguous float buffer (H, W, 3) */
                if (tile.isContinuous()) {
                    std::memcpy(tile_buf.data(), tile.ptr<float>(),
                                cur_tile_h * cur_tile_w * 3 * sizeof(float));
                } else {
                    for (int r = 0; r < cur_tile_h; r++) {
                        std::memcpy(tile_buf.data() + r * cur_tile_w * 3,
                                    tile.ptr<float>(r),
                                    cur_tile_w * 3 * sizeof(float));
                    }
                }

                if (!session_.run(params_.input_name,
                                  tile_buf.data(), cur_tile_h, cur_tile_w, 3,
                                  params_.extra_params,
                                  params_.output_name,
                                  out_buf.data(), cur_out_tile_h, cur_out_tile_w)) {
                    return quink::ProcessResult::Error;
                }

                /* Accumulate with weight mask (in output resolution) */
                int oy = ty * out_step_h;
                int ox = tx * out_step_w;
                cv::Mat out_tile(cur_out_tile_h, cur_out_tile_w, CV_32FC3, out_buf.data());
                cv::Mat roi_acc = output_acc(cv::Rect(ox, oy, cur_out_tile_w, cur_out_tile_h));
                cv::Mat roi_wt = weight_acc(cv::Rect(ox, oy, cur_out_tile_w, cur_out_tile_h));

                for (int r = 0; r < cur_out_tile_h; r++) {
                    const float *src = out_tile.ptr<float>(r);
                    const float *wtp = wt_mask.ptr<float>(r);
                    float *dst = roi_acc.ptr<float>(r);
                    float *wt_acc_p = roi_wt.ptr<float>(r);
                    for (int c = 0; c < cur_out_tile_w; c++) {
                        float w = wtp[c];
                        dst[c * 3 + 0] += src[c * 3 + 0] * w;
                        dst[c * 3 + 1] += src[c * 3 + 1] * w;
                        dst[c * 3 + 2] += src[c * 3 + 2] * w;
                        wt_acc_p[c] += w;
                    }
                }
            }
        }

        /* Normalize by accumulated weights */
        for (int r = 0; r < out_padded_h; r++) {
            float *pix = output_acc.ptr<float>(r);
            const float *wt = weight_acc.ptr<float>(r);
            for (int c = 0; c < out_padded_w; c++) {
                float inv = (wt[c] > 1e-8f) ? (1.0f / wt[c]) : 0.0f;
                pix[c * 3 + 0] *= inv;
                pix[c * 3 + 1] *= inv;
                pix[c * 3 + 2] *= inv;
            }
        }

        /* Crop to target output size */
        int out_h = h * cur_scale_h;
        int out_w = w * cur_scale_w;
        cv::Mat result_rgb_float = output_acc(cv::Rect(0, 0, out_w, out_h));

        /* RGB float [0,1] -> BGR(A) uint8 */
        /* convertTo with CV_8U saturates to [0, 255] automatically */
        cv::Mat result_rgb_u8;
        result_rgb_float.convertTo(result_rgb_u8, CV_8UC3, 255.0);

        cv::Mat result_bgr;
        cv::cvtColor(result_rgb_u8, result_bgr, cv::COLOR_RGB2BGR);

        if (has_alpha) {
            /* Re-attach alpha channel (resize if super-resolution changed size) */
            cv::Mat alpha_resized;
            if (alpha_channel.rows != out_h || alpha_channel.cols != out_w) {
                cv::resize(alpha_channel, alpha_resized, cv::Size(out_w, out_h),
                           0, 0, cv::INTER_LINEAR);
            } else {
                alpha_resized = alpha_channel;
            }
            cv::Mat result_bgra;
            std::vector<cv::Mat> planes;
            cv::split(result_bgr, planes);
            planes.push_back(alpha_resized);
            cv::merge(planes, result_bgra);
            result_bgra.copyTo(out);
        } else {
            result_bgr.copyTo(out);
        }

        return quink::ProcessResult::Ok;
    }

    bool flush(std::vector<quink::ProcessOutput> &) override { return false; }

    bool configure(const std::vector<quink::FrameConfig> &inputs,
                   std::vector<quink::FrameConfig> &outputs) override {
        /* Output resolution = input × scale factor (super-resolution) */
        outputs[0].width = inputs[0].width * scale_w_;
        outputs[0].height = inputs[0].height * scale_h_;
        outputs[0].pix_fmt = inputs[0].pix_fmt;
        return true;
    }

    void uninit() override {}

private:
    OnnxInferParams params_;
    OnnxSession session_;
    int tile_h_ = 0;      /* Input tile height */
    int tile_w_ = 0;      /* Input tile width */
    int out_tile_h_ = 0;  /* Output tile height (= tile_h_ * scale_h_) */
    int out_tile_w_ = 0;  /* Output tile width  (= tile_w_ * scale_w_) */
    int scale_h_ = 1;     /* Vertical scale factor (1 for same-res, 2/4 for super-res) */
    int scale_w_ = 1;     /* Horizontal scale factor */
    cv::Mat weight_mask_;
};

QUINK_OC_PROCESS_PLUGIN_ENTRY(OnnxInferPlugin, "onnx_infer",
    "Generic ONNX model inference (single-input single-output)")
