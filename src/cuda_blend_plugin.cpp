#include <cstdlib>
#include <cstring>
#include <iostream>

#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudawarping.hpp>

#include "quink_oc_plugin.h"

/**
 * CUDA Alpha Blend Plugin (N:1 mode)
 *
 * Blends multiple CUDA input streams into a single output using alpha weights.
 * Parameters: alpha=<float> (0.0 to 1.0, default 0.5)
 *   - For 2 inputs: output = input[0] * (1 - alpha) + input[1] * alpha
 *   - For N inputs: equal weight blending
 */
class CudaAlphaBlendPlugin : public quink::CudaProcessPlugin {
public:
    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        if (nb_inputs < 2 || nb_outputs != 1)
            return false;  // Requires at least 2 inputs and exactly 1 output
        nb_inputs_ = nb_inputs;

        if (!params || !params[0])
            return true;

        const char *pos = strstr(params, "alpha=");
        if (pos) {
            alpha_ = atof(pos + 6);
            if (alpha_ < 0.0) alpha_ = 0.0;
            if (alpha_ > 1.0) alpha_ = 1.0;
        }
        return true;
    }

    quink::ProcessResult process(const std::vector<cv::cuda::GpuMat> &inputs,
                                 std::vector<cv::cuda::GpuMat> &outputs,
                                 cv::cuda::Stream &stream) override {
        if (inputs.size() < 2 || outputs.empty())
            return quink::ProcessResult::Error;

        const cv::cuda::GpuMat &in0 = inputs[0];
        const cv::cuda::GpuMat &in1 = inputs[1];
        cv::cuda::GpuMat &out = outputs[0];

        // Resize second input if sizes differ
        cv::cuda::GpuMat in1_resized;
        if (in0.size() != in1.size()) {
            cv::cuda::resize(in1, in1_resized, in0.size(), 0, 0, cv::INTER_LINEAR, stream);
        } else {
            in1_resized = in1;
        }

        // Alpha blend: out = in0 * (1 - alpha) + in1 * alpha
        cv::cuda::addWeighted(in0, 1.0 - alpha_, in1_resized, alpha_, 0.0, out, -1, stream);

        // If more than 2 inputs, blend remaining with equal weight
        for (size_t i = 2; i < inputs.size(); i++) {
            cv::cuda::GpuMat extra;
            if (inputs[i].size() != out.size()) {
                cv::cuda::resize(inputs[i], extra, out.size(), 0, 0, cv::INTER_LINEAR, stream);
            } else {
                extra = inputs[i];
            }
            double w = 1.0 / (i + 1);
            cv::cuda::addWeighted(out, 1.0 - w, extra, w, 0.0, out, -1, stream);
        }

        return quink::ProcessResult::Ok;
    }

    bool flush(std::vector<cv::cuda::GpuMat> &, cv::cuda::Stream &) override {
        return false;
    }

    bool configure(const std::vector<quink::FrameConfig> &inputs,
                   std::vector<quink::FrameConfig> &outputs) override {
        // Output takes dimensions from first input
        outputs[0].width = inputs[0].width;
        outputs[0].height = inputs[0].height;
        outputs[0].pix_fmt = inputs[0].pix_fmt;
        return true;
    }

    void uninit() override {}

private:
    int nb_inputs_ = 2;
    double alpha_ = 0.5;
};

QUINK_OC_CUDA_PROCESS_PLUGIN_ENTRY(CudaAlphaBlendPlugin, "cuda_blend", "CUDA alpha blend multiple inputs")
