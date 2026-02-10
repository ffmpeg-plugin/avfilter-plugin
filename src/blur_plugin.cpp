#include <opencv2/imgproc.hpp>
#include "plugin_common.h"

class GaussianBlurPlugin : public quink::ProcessPlugin {
public:
    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        if (nb_inputs != 1 || nb_outputs != 1)
            return false;
        return params_.parse(params);
    }

    quink::ProcessResult process(const std::vector<cv::Mat> &inputs,
                                 std::vector<quink::ProcessOutput> &outputs) override {
        if (inputs.empty() || outputs.empty())
            return quink::ProcessResult::Error;

        if (params_.scale == 1.0f) {
            cv::GaussianBlur(inputs[0], outputs[0].frame,
                             cv::Size(params_.kernel_size, params_.kernel_size), 0);
        } else {
            cv::resize(inputs[0], outputs[0].frame, outputs[0].frame.size(), 0, 0, cv::INTER_CUBIC);
            cv::GaussianBlur(outputs[0].frame, outputs[0].frame,
                            cv::Size(params_.kernel_size, params_.kernel_size), 0);
        }

        return quink::ProcessResult::Ok;
    }

    bool flush(std::vector<quink::ProcessOutput> &) override { return false; }

    bool configure(const std::vector<quink::FrameConfig> &inputs,
                   std::vector<quink::FrameConfig> &outputs) override {
        return params_.configureOutput(inputs[0], outputs[0]);
    }

    void uninit() override {}

private:
    plugin_common::BlurParams params_;
};

QUINK_OC_PROCESS_PLUGIN_ENTRY(GaussianBlurPlugin, "blur", "Gaussian blur effect")
