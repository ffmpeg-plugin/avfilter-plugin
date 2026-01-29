#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudawarping.hpp>
#include "plugin_common.h"

class CudaAlphaBlendPlugin : public quink::CudaProcessPlugin {
public:
    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        if (nb_inputs < 2 || nb_outputs != 1)
            return false;
        return params_.parse(params);
    }

    quink::ProcessResult process(const std::vector<cv::cuda::GpuMat> &inputs,
                                 std::vector<cv::cuda::GpuMat> &outputs,
                                 cv::cuda::Stream &stream) override {
        if (inputs.size() < 2 || outputs.empty())
            return quink::ProcessResult::Error;

        cv::cuda::GpuMat in1_resized;
        if (inputs[0].size() != inputs[1].size())
            cv::cuda::resize(inputs[1], in1_resized, inputs[0].size(), 0, 0, cv::INTER_LINEAR, stream);
        else
            in1_resized = inputs[1];

        cv::cuda::addWeighted(inputs[0], 1.0 - params_.alpha, in1_resized, params_.alpha, 0.0, outputs[0], -1, stream);

        for (size_t i = 2; i < inputs.size(); i++) {
            cv::cuda::GpuMat extra;
            if (inputs[i].size() != outputs[0].size())
                cv::cuda::resize(inputs[i], extra, outputs[0].size(), 0, 0, cv::INTER_LINEAR, stream);
            else
                extra = inputs[i];
            double w = 1.0 / (i + 1);
            cv::cuda::addWeighted(outputs[0], 1.0 - w, extra, w, 0.0, outputs[0], -1, stream);
        }

        return quink::ProcessResult::Ok;
    }

    bool flush(std::vector<cv::cuda::GpuMat> &, cv::cuda::Stream &) override { return false; }

    bool configure(const std::vector<quink::FrameConfig> &inputs,
                   std::vector<quink::FrameConfig> &outputs) override {
        outputs[0].width = inputs[0].width;
        outputs[0].height = inputs[0].height;
        outputs[0].pix_fmt = inputs[0].pix_fmt;
        return true;
    }

    void uninit() override {}

private:
    plugin_common::BlendParams params_;
};

QUINK_OC_CUDA_PROCESS_PLUGIN_ENTRY(CudaAlphaBlendPlugin, "cuda_blend", "CUDA alpha blend multiple inputs")
