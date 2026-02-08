#include <cstdlib>
#include <cstring>

#include <opencv2/cudacodec.hpp>
#include <opencv2/cudafilters.hpp>
#include <opencv2/cudawarping.hpp>

#include "quink_oc_plugin.h"

#include <iostream>

class CudaGaussianBlurPlugin : public quink::CudaProcessPlugin {
public:
    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        if (nb_inputs != 1 || nb_outputs != 1)
            return false;  // Only supports 1 input and 1 output
        if (!params || !params[0])
            return true;

        std::string key = "ksize=";
        const char *pos = strstr(params, key.c_str());
        if (pos) {
            kernel_size_ = atoi(pos + key.size());
            if (kernel_size_ % 2 == 0)
                kernel_size_++;
            if (kernel_size_ < 1)
                kernel_size_ = 1;
        }
        key = "scale=";
        pos = strstr(params, key.c_str());
        if (pos) {
            scale_ = atof(pos + key.size());
            if (scale_ < 0) {
                std::cerr << "scale must be non-negative." << std::endl;
                return false;
            }
        }

        return true;
    }

    quink::ProcessResult process(const std::vector<cv::cuda::GpuMat> &inputs,
                                 std::vector<cv::cuda::GpuMat> &outputs,
                                 cv::cuda::Stream &stream) override {
        if (inputs.empty() || outputs.empty())
            return quink::ProcessResult::Error;

        cv::cuda::GpuMat tmp;
        cv::cuda::GpuMat in = inputs[0];
        if (convert_) {
            if (!convert_->convert(in, tmp, surface_format_, out_format_, cv::cudacodec::EIGHT, false, stream)) {
                std::cerr << "Error converting NV12/P016 to BGR." << std::endl;
                return quink::ProcessResult::Error;
            }
            in = tmp;
        }

        // Apply Gaussian blur using CUDA
        if (scale_ == 1.0f) {
            blur_filter_->apply(in, outputs[0], stream);
        } else {
            cv::cuda::resize(in, outputs[0], outputs[0].size(), 0, 0, cv::INTER_LINEAR, stream);
            blur_filter_->apply(outputs[0], outputs[0], stream);
        }

        return quink::ProcessResult::Ok;
    }

    bool flush(std::vector<cv::cuda::GpuMat> &, cv::cuda::Stream &) override {
        return false;
    }

    bool configure(const std::vector<quink::FrameConfig> &inputs,
                   std::vector<quink::FrameConfig> &outputs) override {
        const quink::FrameConfig &in = inputs[0];
        quink::FrameConfig &out = outputs.front();
        // test resize
        out.width *= scale_;
        out.height *= scale_;

        in_pix_fmt_ = in.pix_fmt;
        if (in.pix_fmt == quink::QPixelFormat::NV12 || in.pix_fmt == quink::QPixelFormat::P016) {
            // notify format change to FFmpeg
            out.pix_fmt = quink::QPixelFormat::BGRA;
            // set requested output format for converter
            out_format_ = cv::cudacodec::BGRA;
            if (in.pix_fmt == quink::QPixelFormat::NV12)
                surface_format_ = cv::cudacodec::SF_NV12;
            else
                surface_format_ = cv::cudacodec::SF_P016;
            // There is a bug in opencv cudacodec, while makes bt2020/bt2020c don't work.
            // I have a patch for it.
            convert_ = cv::cudacodec::createNVSurfaceToColorConverter(
                static_cast<cv::cudacodec::ColorSpaceStandard>(in.colorspace), !in.limited_range);
            if (!convert_)
                return false;
            createFilter(CV_8UC4);
        } else {
            out.pix_fmt = in.pix_fmt;
            createFilter(in.cv_type);
        }

        return true;
    }

    void uninit() override {
        blur_filter_.release();
    }

private:
    void createFilter(int cv_type) {
        blur_filter_ = cv::cuda::createGaussianFilter(
            cv_type,  // srcType
            cv_type,  // dstType
            cv::Size(kernel_size_, kernel_size_),
            0  // sigma, auto-calculated
        );
    }

    int kernel_size_ = 5;
    float scale_ = 1.0f;
    cv::Ptr<cv::cuda::Filter> blur_filter_;
    quink::QPixelFormat in_pix_fmt_ = quink::QPixelFormat::None;
    cv::cudacodec::SurfaceFormat surface_format_ = cv::cudacodec::SF_NV12;
    cv::cudacodec::ColorFormat out_format_ = cv::cudacodec::UNDEFINED;
    cv::Ptr<cv::cudacodec::NVSurfaceToColorConverter> convert_;
};

QUINK_OC_CUDA_PROCESS_PLUGIN_ENTRY(CudaGaussianBlurPlugin, "cuda_blur", "CUDA Gaussian blur effect")
