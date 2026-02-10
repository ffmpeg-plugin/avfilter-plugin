#include <opencv2/imgproc.hpp>
#include "plugin_common.h"

class SplitPlugin : public quink::ProcessPlugin {
public:
    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        (void)params;
        if (nb_inputs != 1 || nb_outputs < 1 || nb_outputs > 4)
            return false;
        num_outputs_ = nb_outputs;
        return true;
    }

    quink::ProcessResult process(const std::vector<cv::Mat> &inputs,
                                 std::vector<quink::ProcessOutput> &outputs) override {
        if (inputs.empty() || outputs.size() < static_cast<size_t>(num_outputs_))
            return quink::ProcessResult::Error;

        const cv::Mat &src = inputs[0];

        /* Output 0: pass-through */
        outputs[0].frame = src;

        /* Output 1: grayscale */
        if (num_outputs_ >= 2) {
            cv::Mat gray;
            cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
            cv::cvtColor(gray, outputs[1].frame, cv::COLOR_GRAY2BGR);
        }

        /* Output 2: edge detection */
        if (num_outputs_ >= 3) {
            cv::Mat gray, edges;
            cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
            cv::Canny(gray, edges, 50, 150);
            cv::cvtColor(edges, outputs[2].frame, cv::COLOR_GRAY2BGR);
        }

        /* Output 3: Gaussian blur */
        if (num_outputs_ >= 4)
            cv::GaussianBlur(src, outputs[3].frame, cv::Size(15, 15), 0);

        return quink::ProcessResult::Ok;
    }

    bool flush(std::vector<quink::ProcessOutput> &) override { return false; }

    bool configure(const std::vector<quink::FrameConfig> &inputs,
                   std::vector<quink::FrameConfig> &outputs) override {
        /* All CPU split outputs are same size as input */
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

QUINK_OC_PROCESS_PLUGIN_ENTRY(SplitPlugin, "split", "Single input to multiple outputs")
