#include <quink_oc_plugin.h>
#include <opencv2/imgproc.hpp>
#include <cstdlib>
#include <cstring>
#include <deque>

class FrameAveragePlugin : public quink::ProcessPlugin {
public:
    FrameAveragePlugin() {}

    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        if (nb_inputs != 1 || nb_outputs != 1)
            return false;  // Only supports 1 input and 1 output
        if (!params || !params[0]) return true;
        
        const char *pos = strstr(params, "frames=");
        if (pos) {
            num_frames_ = atoi(pos + 7);
            if (num_frames_ < 1)
                num_frames_ = 1;
            if (num_frames_ > 16)
                num_frames_ = 16;
        }
        return true;
    }

    quink::ProcessResult process(const std::vector<cv::Mat> &inputs,
                                 std::vector<cv::Mat> &outputs) override {
        if (inputs.empty() || outputs.empty())
            return quink::ProcessResult::Error;

        frame_buffer_.push_back(inputs[0].clone());

        if (static_cast<int>(frame_buffer_.size()) < num_frames_)
            return quink::ProcessResult::TryAgain;

        computeAverage(outputs[0]);
        frame_buffer_.pop_front();
        output_count_++;
        return quink::ProcessResult::Ok;
    }

    bool flush(std::vector<cv::Mat> &outputs) override {
        if (frame_buffer_.empty() || outputs.empty())
            return false;
        
        computeAverage(outputs[0]);
        frame_buffer_.pop_front();
        output_count_++;
        return !frame_buffer_.empty();
    }

    bool configure(const std::vector<quink::FrameConfig> &inputs,
                   std::vector<quink::FrameConfig> &outputs) override {
        (void)inputs;
        (void)outputs;
        return true;
    }

    void uninit() override { frame_buffer_.clear(); }

private:
    void computeAverage(cv::Mat &output) {
        if (frame_buffer_.empty())
            return;
        
        cv::Mat accumulator;
        frame_buffer_[0].convertTo(accumulator, CV_32F);
        
        for (size_t i = 1; i < frame_buffer_.size(); i++) {
            cv::Mat temp;
            frame_buffer_[i].convertTo(temp, CV_32F);
            accumulator += temp;
        }
        
        accumulator /= static_cast<double>(frame_buffer_.size());
        accumulator.convertTo(output, frame_buffer_[0].type());
    }

    int num_frames_ = 3;
    std::deque<cv::Mat> frame_buffer_;
    int output_count_ = 0;
};

QUINK_OC_PROCESS_PLUGIN_ENTRY(FrameAveragePlugin, "avgframes", "Temporal frame averaging")
