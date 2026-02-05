#include <cstdlib>
#include <cstring>

#include <opencv2/cudacodec.hpp>
#include <opencv2/cudafilters.hpp>
#include <opencv2/cudawarping.hpp>

#include "quink_oc_plugin.h"

#include <iostream>

class CudaGaussianBlurPlugin : public QuinkOCCudaProcessPlugin {
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

    QuinkOCProcessResult process(const std::vector<cv::cuda::GpuMat> &inputs,
                                 std::vector<cv::cuda::GpuMat> &outputs,
                                 cv::cuda::Stream &stream) override {
        if (inputs.empty() || outputs.empty())
            return QUINK_OC_ERROR;

        cv::cuda::GpuMat tmp;
        cv::cuda::GpuMat in = inputs[0];
        if (convert_) {
            if (!convert_->convert(in, tmp, surface_format_, out_format_, cv::cudacodec::EIGHT, false, stream)) {
                std::cerr << "Error converting NV12/P016 to BGR." << std::endl;
                return QUINK_OC_ERROR;
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

        return QUINK_OC_OK;
    }

    bool flush(std::vector<cv::cuda::GpuMat> &, cv::cuda::Stream &) override {
        return false;
    }

    bool configure(const std::vector<QuinkOCFrameConfig> &inputs,
                   std::vector<QuinkOCFrameConfig> &outputs) override {
        const QuinkOCFrameConfig &in = inputs[0];
        QuinkOCFrameConfig &out = outputs.front();
        // test resize
        out.width *= scale_;
        out.height *= scale_;

        in_pix_fmt_ = in.pix_fmt;
        if (in.pix_fmt == QUINK_PIX_FMT_NV12 || in.pix_fmt == QUINK_PIX_FMT_P016) {
            // notify format change to FFmpeg
            out.pix_fmt = QUINK_PIX_FMT_BGRA;
            // set requested output format for converter
            out_format_ = cv::cudacodec::BGRA;
            if (in.pix_fmt == QUINK_PIX_FMT_NV12)
                surface_format_ = cv::cudacodec::SF_NV12;
            else
                surface_format_ = cv::cudacodec::SF_P016;
            // TODO: change colorspace
            convert_ = cv::cudacodec::createNVSurfaceToColorConverter(cv::cudacodec::ColorSpaceStandard::BT709, !in.limited_range);
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
    QuinkPixelFormat in_pix_fmt_ = QUINK_PIX_FMT_NONE;
    cv::cudacodec::SurfaceFormat surface_format_ = cv::cudacodec::SF_NV12;
    cv::cudacodec::ColorFormat out_format_ = cv::cudacodec::UNDEFINED;
    cv::Ptr<cv::cudacodec::NVSurfaceToColorConverter> convert_;
};

QUINK_OC_CUDA_PROCESS_PLUGIN_ENTRY(CudaGaussianBlurPlugin, "cuda_blur", "CUDA Gaussian blur effect")
