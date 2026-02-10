#include <opencv2/imgproc.hpp>
#include "plugin_common.h"

class AlphaBlendPlugin : public quink::ProcessPlugin {
public:
    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        if (nb_inputs != 2 || nb_outputs != 1)
            return false;
        return params_.parse(params);
    }

    quink::ProcessResult process(const std::vector<cv::Mat> &inputs,
                                 std::vector<quink::ProcessOutput> &outputs) override {
        if (inputs.size() < 2 || outputs.empty())
            return quink::ProcessResult::Error;

        cv::Mat in2_resized;
        if (inputs[0].size() != inputs[1].size())
            cv::resize(inputs[1], in2_resized, inputs[0].size());
        else
            in2_resized = inputs[1];

        cv::addWeighted(inputs[0], 1.0 - params_.alpha, in2_resized, params_.alpha, 0.0, outputs[0].frame);
        return quink::ProcessResult::Ok;
    }

    bool flush(std::vector<quink::ProcessOutput> &) override { return false; }

    bool configure(const std::vector<quink::FrameConfig> &,
                   std::vector<quink::FrameConfig> &) override {
        return true;
    }

    void uninit() override {}

private:
    plugin_common::BlendParams params_;
};

QUINK_OC_PROCESS_PLUGIN_ENTRY(AlphaBlendPlugin, "blend", "Alpha blend two video streams")
