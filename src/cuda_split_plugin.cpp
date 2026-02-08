#include <cstdlib>
#include <cstring>
#include <iostream>

#include <opencv2/cudafilters.hpp>
#include <opencv2/cudawarping.hpp>

#include "quink_oc_plugin.h"

/**
 * CUDA Split Plugin (1:N mode)
 *
 * Splits a single CUDA input into multiple outputs with different processing:
 *   - Output 0: pass-through (zero-copy)
 *   - Output 1: Gaussian blur
 *   - Output 2: half-resolution
 *   - Output 3: Gaussian blur + half-resolution
 */
class CudaSplitPlugin : public quink::CudaProcessPlugin {
public:
    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        if (nb_inputs != 1)
            return false;  // Only supports 1 input
        if (nb_outputs < 1 || nb_outputs > 4)
            return false;  // Supports 1 to 4 outputs
        num_outputs_ = nb_outputs;
        return true;
    }

    quink::ProcessResult process(const std::vector<cv::cuda::GpuMat> &inputs,
                                 std::vector<cv::cuda::GpuMat> &outputs,
                                 cv::cuda::Stream &stream) override {
        if (inputs.empty() || outputs.size() < static_cast<size_t>(num_outputs_))
            return quink::ProcessResult::Error;

        const cv::cuda::GpuMat &src = inputs[0];

        // Output 0: zero-copy pass-through
        outputs[0] = src;

        // Output 1: Gaussian blur
        if (num_outputs_ >= 2 && blur_filter_) {
            blur_filter_->apply(src, outputs[1], stream);
        }

        // Output 2: half-resolution
        if (num_outputs_ >= 3) {
            cv::cuda::resize(src, outputs[2], outputs[2].size(), 0, 0, cv::INTER_LINEAR, stream);
        }

        // Output 3: blur + half-resolution
        if (num_outputs_ >= 4 && blur_filter_) {
            blur_filter_->apply(src, tmp_, stream);
            cv::cuda::resize(tmp_, outputs[3], outputs[3].size(), 0, 0, cv::INTER_LINEAR, stream);
        }

        return quink::ProcessResult::Ok;
    }

    bool flush(std::vector<cv::cuda::GpuMat> &, cv::cuda::Stream &) override {
        return false;
    }

    bool configure(const std::vector<quink::FrameConfig> &inputs,
                   std::vector<quink::FrameConfig> &outputs) override {
        if (inputs.empty()) return false;

        const quink::FrameConfig &in = inputs[0];

        // Output 0: same as input
        outputs[0].width = in.width;
        outputs[0].height = in.height;
        outputs[0].pix_fmt = in.pix_fmt;

        // Output 1: same size, blurred
        if (num_outputs_ >= 2) {
            outputs[1].width = in.width;
            outputs[1].height = in.height;
            outputs[1].pix_fmt = in.pix_fmt;
        }

        // Output 2: half resolution
        if (num_outputs_ >= 3) {
            outputs[2].width = in.width / 2;
            outputs[2].height = in.height / 2;
            outputs[2].pix_fmt = in.pix_fmt;
        }

        // Output 3: half resolution + blur
        if (num_outputs_ >= 4) {
            outputs[3].width = in.width / 2;
            outputs[3].height = in.height / 2;
            outputs[3].pix_fmt = in.pix_fmt;
        }

        // Create Gaussian blur filter
        blur_filter_ = cv::cuda::createGaussianFilter(
            in.cv_type, in.cv_type,
            cv::Size(15, 15), 0);

        return true;
    }

    void uninit() override {
        blur_filter_.release();
    }

private:
    int num_outputs_ = 0;
    cv::Ptr<cv::cuda::Filter> blur_filter_;
    cv::cuda::GpuMat tmp_;  // Temporary buffer for blur + resize
};

QUINK_OC_CUDA_PROCESS_PLUGIN_ENTRY(CudaSplitPlugin, "cuda_split", "CUDA single input to multiple outputs")
