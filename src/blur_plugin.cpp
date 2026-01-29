#include <quink_oc_plugin.h>
#include <opencv2/imgproc.hpp>
#include <cstdlib>
#include <cstring>

class GaussianBlurPlugin : public QuinkOCPlugin {
public:
    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        if (nb_inputs != 1 || nb_outputs != 1)
            return false;  // Only supports 1 input and 1 output
        if (!params || !params[0]) return true;

        const char *pos = strstr(params, "ksize=");
        if (pos) {
            kernel_size_ = atoi(pos + 6);
            if (kernel_size_ % 2 == 0)
                kernel_size_++;
            if (kernel_size_ < 1)
                kernel_size_ = 1;
        }

        return true;
    }

    QuinkOCProcessResult process(const std::vector<cv::Mat> &inputs,
                                 std::vector<cv::Mat> &outputs) override {
        if (inputs.empty() || outputs.empty())
            return QUINK_OC_ERROR;
        
        cv::GaussianBlur(inputs[0], outputs[0],
                         cv::Size(kernel_size_, kernel_size_), 0);
        return QUINK_OC_OK;
    }

    bool flush(std::vector<cv::Mat> &) override {
        return false;
    }

    bool configure(const std::vector<QuinkOCFrameConfig> &inputs,
                   std::vector<QuinkOCFrameConfig> &outputs) override {
        (void)inputs;
        (void)outputs;
        return true;
    }

    void uninit() override { }

private:
    int kernel_size_ = 5;
};

QUINK_OC_PLUGIN_ENTRY(GaussianBlurPlugin, "blur", "Gaussian blur effect")
