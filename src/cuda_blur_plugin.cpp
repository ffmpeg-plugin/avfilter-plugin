#include <opencv2/cudacodec.hpp>
#include <opencv2/cudafilters.hpp>
#include <opencv2/cudawarping.hpp>
#include "plugin_common.h"

class CudaGaussianBlurPlugin : public quink::CudaProcessPlugin {
public:
    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        if (nb_inputs != 1 || nb_outputs != 1)
            return false;
        return params_.parse(params);
    }

    quink::ProcessResult process(const std::vector<cv::cuda::GpuMat> &inputs,
                                 std::vector<cv::cuda::GpuMat> &outputs,
                                 cv::cuda::Stream &stream) override {
        if (inputs.empty() || outputs.empty())
            return quink::ProcessResult::Error;

        cv::cuda::GpuMat tmp;
        cv::cuda::GpuMat in = inputs[0];

        /* NV12/P016 -> BGRA color space conversion (CUDA-specific) */
        if (convert_) {
            if (!convert_->convert(in, tmp, surface_format_, out_format_, cv::cudacodec::EIGHT, false, stream))
                return quink::ProcessResult::Error;
            in = tmp;
        }

        if (params_.scale == 1.0f) {
            blur_filter_->apply(in, outputs[0], stream);
        } else {
            cv::cuda::resize(in, outputs[0], outputs[0].size(), 0, 0, cv::INTER_LINEAR, stream);
            blur_filter_->apply(outputs[0], outputs[0], stream);
        }

        return quink::ProcessResult::Ok;
    }

    bool flush(std::vector<cv::cuda::GpuMat> &, cv::cuda::Stream &) override { return false; }

    bool configure(const std::vector<quink::FrameConfig> &inputs,
                   std::vector<quink::FrameConfig> &outputs) override {
        const quink::FrameConfig &in = inputs[0];
        quink::FrameConfig &out = outputs[0];

        params_.configureOutput(in, out);

        /* Handle NV12/P016 -> BGRA conversion (CUDA-specific feature) */
        if (in.pix_fmt == quink::QPixelFormat::NV12 || in.pix_fmt == quink::QPixelFormat::P016) {
            out.pix_fmt = quink::QPixelFormat::BGRA;
            out_format_ = cv::cudacodec::BGRA;
            surface_format_ = (in.pix_fmt == quink::QPixelFormat::NV12)
                ? cv::cudacodec::SF_NV12 : cv::cudacodec::SF_P016;
            convert_ = cv::cudacodec::createNVSurfaceToColorConverter(
                static_cast<cv::cudacodec::ColorSpaceStandard>(in.colorspace), !in.limited_range);
            if (!convert_) return false;
            createFilter(CV_8UC4);
        } else {
            out.pix_fmt = in.pix_fmt;
            createFilter(in.cv_type);
        }

        return true;
    }

    void uninit() override { blur_filter_.release(); }

private:
    void createFilter(int cv_type) {
        blur_filter_ = cv::cuda::createGaussianFilter(
            cv_type, cv_type,
            cv::Size(params_.kernel_size, params_.kernel_size), 0);
    }

    plugin_common::BlurParams params_;
    cv::Ptr<cv::cuda::Filter> blur_filter_;
    cv::cudacodec::SurfaceFormat surface_format_ = cv::cudacodec::SF_NV12;
    cv::cudacodec::ColorFormat out_format_ = cv::cudacodec::UNDEFINED;
    cv::Ptr<cv::cudacodec::NVSurfaceToColorConverter> convert_;
};

QUINK_OC_CUDA_PROCESS_PLUGIN_ENTRY(CudaGaussianBlurPlugin, "cuda_blur", "CUDA Gaussian blur effect")
