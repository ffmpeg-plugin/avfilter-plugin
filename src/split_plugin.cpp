#include <quink_oc_plugin.h>
#include <opencv2/imgproc.hpp>
#include <cstdlib>
#include <cstring>

class SplitPlugin : public QuinkOCPlugin {
public:
    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        (void)params;
        if (nb_inputs != 1)
            return false;  // Only supports 1 input
        if (nb_outputs < 1 || nb_outputs > 4)
            return false;  // Supports 1 to 4 outputs
        num_outputs_ = nb_outputs;
        return true;
    }

    QuinkOCProcessResult process(const std::vector<cv::Mat> &inputs,
                                 std::vector<cv::Mat> &outputs) override {
        if (inputs.empty() || outputs.size() < static_cast<size_t>(num_outputs_))
            return QUINK_OC_ERROR;

        const cv::Mat &src = inputs[0];

        outputs[0] = src;
        if (num_outputs_ >= 2) {
            cv::Mat gray;
            cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
            cv::cvtColor(gray, outputs[1], cv::COLOR_GRAY2BGR);
        }

        if (num_outputs_ >= 3) {
            cv::Mat gray, edges;
            cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
            cv::Canny(gray, edges, 50, 150);
            cv::cvtColor(edges, outputs[2], cv::COLOR_GRAY2BGR);
        }

        if (num_outputs_ >= 4) {
            cv::GaussianBlur(src, outputs[3], cv::Size(15, 15), 0);
        }

        return QUINK_OC_OK;
    }

    bool flush(std::vector<cv::Mat> &) override { return false; }

    bool configure(const std::vector<QuinkOCFrameConfig> &inputs,
                   std::vector<QuinkOCFrameConfig> &outputs) override {
        if (inputs.empty()) return false;
        for (auto &out : outputs) {
            out.width = inputs[0].width;
            out.height = inputs[0].height;
        }
        return true;
    }

    void uninit() override {}

private:
    int num_outputs_ = 0;
};

QUINK_OC_PLUGIN_ENTRY(SplitPlugin, "split", "Single input to multiple outputs")
