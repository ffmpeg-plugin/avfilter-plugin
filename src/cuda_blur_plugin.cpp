#include <quink_oc_plugin.h>
#include <opencv2/cudafilters.hpp>
#include <cstdlib>
#include <cstring>

class CudaGaussianBlurPlugin : public QuinkOCCudaProcessPlugin {
public:
    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        if (nb_inputs != 1 || nb_outputs != 1)
            return false;  // Only supports 1 input and 1 output
        if (!params || !params[0]) {
            createFilter();
            return true;
        }

        const char *pos = strstr(params, "ksize=");
        if (pos) {
            kernel_size_ = atoi(pos + 6);
            if (kernel_size_ % 2 == 0)
                kernel_size_++;
            if (kernel_size_ < 1)
                kernel_size_ = 1;
        }

        createFilter();
        return true;
    }

    QuinkOCProcessResult process(const std::vector<cv::cuda::GpuMat> &inputs,
                                 std::vector<cv::cuda::GpuMat> &outputs,
                                 cv::cuda::Stream &stream) override {
        if (inputs.empty() || outputs.empty())
            return QUINK_OC_ERROR;

        // Apply Gaussian blur using CUDA
        blur_filter_->apply(inputs[0], outputs[0], stream);
        return QUINK_OC_OK;
    }

    bool flush(std::vector<cv::cuda::GpuMat> &, cv::cuda::Stream &) override {
        return false;
    }

    bool configure(const std::vector<QuinkOCFrameConfig> &inputs,
                   std::vector<QuinkOCFrameConfig> &outputs) override {
        (void)inputs;
        (void)outputs;
        return true;
    }

    void uninit() override {
        blur_filter_.release();
    }

private:
    void createFilter() {
        blur_filter_ = cv::cuda::createGaussianFilter(
            CV_8UC4,  // srcType
            CV_8UC4,  // dstType
            cv::Size(kernel_size_, kernel_size_),
            0  // sigma, auto-calculated
        );
    }

    int kernel_size_ = 5;
    cv::Ptr<cv::cuda::Filter> blur_filter_;
};

QUINK_OC_CUDA_PROCESS_PLUGIN_ENTRY(CudaGaussianBlurPlugin, "cuda_blur", "CUDA Gaussian blur effect")
