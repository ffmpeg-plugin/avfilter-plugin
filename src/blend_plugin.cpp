#include <quink_oc_plugin.h>
#include <opencv2/imgproc.hpp>
#include <cstdlib>
#include <cstring>

class AlphaBlendPlugin : public QuinkOCPlugin {
public:
    AlphaBlendPlugin() {}

    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        if (nb_inputs != 2 || nb_outputs != 1)
            return false;  // Requires exactly 2 inputs and 1 output
        if (!params || !params[0]) return true;
        
        const char *pos = strstr(params, "alpha=");
        if (pos) {
            alpha_ = atof(pos + 6);
            if (alpha_ < 0.0)
                alpha_ = 0.0;
            if (alpha_ > 1.0)
                alpha_ = 1.0;
        }
        return true;
    }

    QuinkOCProcessResult process(const std::vector<cv::Mat> &inputs,
                                 std::vector<cv::Mat> &outputs) override {
        if (inputs.size() < 2 || outputs.empty())
            return QUINK_OC_ERROR;

        const cv::Mat& in1 = inputs[0];
        const cv::Mat& in2 = inputs[1];
        cv::Mat& out = outputs[0];

        cv::Mat in2_resized;
        if (in1.size() != in2.size()) {
            cv::resize(in2, in2_resized, in1.size());
        } else {
            in2_resized = in2;
        }

        cv::addWeighted(in1, 1.0 - alpha_, in2_resized, alpha_, 0.0, out);
        return QUINK_OC_OK;
    }

    bool flush(std::vector<cv::Mat> &outputs) override {
        (void)outputs;
        return false;
    }

    bool configure(const std::vector<QuinkOCFrameConfig> &inputs,
                   std::vector<QuinkOCFrameConfig> &outputs) override {
        (void)inputs;
        (void)outputs;
        return true;
    }

    void uninit() override {}

private:
    double alpha_ = 0.5;
};

QUINK_OC_PLUGIN_ENTRY(AlphaBlendPlugin, "blend", "Alpha blend two video streams")
