#include <cstdlib>
#include <cstring>
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <quink_oc_plugin.h>

class GaussianBlurPlugin : public quink::ProcessPlugin {
public:
    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        if (nb_inputs != 1 || nb_outputs != 1)
            return false;  // Only supports 1 input and 1 output
        if (!params || !params[0]) return true;

        std::string key = "ksize=";
        const char *pos = strstr(params, key.c_str());
        if (pos) {
            kernel_size_ = atoi(pos + key.size());
            if (kernel_size_ % 2 == 0)
                kernel_size_++;
            if (kernel_size_ < 1)
                kernel_size_ = 1;
        }
        key = "scale=";
        pos = strstr(params, key.c_str());
        if (pos) {
            scale_ = atof(pos + key.size());
            if (scale_ < 0) {
                std::cerr << "scale must be non-negative." << std::endl;
                return false;
            }
        }

        return true;
    }

    quink::ProcessResult process(const std::vector<cv::Mat> &inputs,
                                 std::vector<cv::Mat> &outputs) override {
        if (inputs.empty() || outputs.empty())
            return quink::ProcessResult::Error;

        if (scale_ == 1.0f) {
            cv::GaussianBlur(inputs[0], outputs[0],
                             cv::Size(kernel_size_, kernel_size_), 0);
        } else {
            cv::resize(inputs[0], outputs[0], outputs[0].size(), 0, 0, cv::INTER_CUBIC);
            cv::GaussianBlur(outputs[0], outputs[0],
                            cv::Size(kernel_size_, kernel_size_), 0);
        }

        return quink::ProcessResult::Ok;
    }

    bool flush(std::vector<cv::Mat> &) override {
        return false;
    }

    bool configure(const std::vector<quink::FrameConfig> &inputs,
                   std::vector<quink::FrameConfig> &outputs) override {
        (void)inputs;
        // test resize
        outputs.front().width *= scale_;
        outputs.front().height *= scale_;
        return true;
    }

    void uninit() override { }

private:
    float scale_ = 1.0f;
    int kernel_size_ = 5;
};

QUINK_OC_PROCESS_PLUGIN_ENTRY(GaussianBlurPlugin, "blur", "Gaussian blur effect")
