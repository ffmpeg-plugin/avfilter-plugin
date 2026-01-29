#include <opencv2/cudafilters.hpp>
#include <opencv2/cudawarping.hpp>
#include "plugin_common.h"

class CudaSplitPlugin : public quink::CudaProcessPlugin {
public:
    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        (void)params;
        if (nb_inputs != 1 || nb_outputs < 1 || nb_outputs > 4)
            return false;
        cfg_.num_outputs = nb_outputs;
        return true;
    }

    quink::ProcessResult process(const std::vector<cv::cuda::GpuMat> &inputs,
                                 std::vector<cv::cuda::GpuMat> &outputs,
                                 cv::cuda::Stream &stream) override {
        if (inputs.empty() || outputs.size() < static_cast<size_t>(cfg_.num_outputs))
            return quink::ProcessResult::Error;

        const cv::cuda::GpuMat &src = inputs[0];

        /* Output 0: pass-through */
        outputs[0] = src;

        /* Output 1: Gaussian blur */
        if (cfg_.num_outputs >= 2 && blur_filter_)
            blur_filter_->apply(src, outputs[1], stream);

        /* Output 2: half-resolution */
        if (cfg_.num_outputs >= 3)
            cv::cuda::resize(src, outputs[2], outputs[2].size(), 0, 0, cv::INTER_LINEAR, stream);

        /* Output 3: blur + half-resolution */
        if (cfg_.num_outputs >= 4 && blur_filter_) {
            blur_filter_->apply(src, tmp_, stream);
            cv::cuda::resize(tmp_, outputs[3], outputs[3].size(), 0, 0, cv::INTER_LINEAR, stream);
        }

        return quink::ProcessResult::Ok;
    }

    bool flush(std::vector<cv::cuda::GpuMat> &, cv::cuda::Stream &) override { return false; }

    bool configure(const std::vector<quink::FrameConfig> &inputs,
                   std::vector<quink::FrameConfig> &outputs) override {
        if (!cfg_.configureOutputs(inputs[0], outputs))
            return false;

        blur_filter_ = cv::cuda::createGaussianFilter(
            inputs[0].cv_type, inputs[0].cv_type,
            cv::Size(15, 15), 0);
        return true;
    }

    void uninit() override { blur_filter_.release(); }

private:
    plugin_common::SplitConfig cfg_;
    cv::Ptr<cv::cuda::Filter> blur_filter_;
    cv::cuda::GpuMat tmp_;
};

QUINK_OC_CUDA_PROCESS_PLUGIN_ENTRY(CudaSplitPlugin, "cuda_split", "CUDA single input to multiple outputs")
