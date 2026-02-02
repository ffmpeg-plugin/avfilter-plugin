/*
 * Demo Detect Plugin
 *
 * Demonstrates the QuinkOCDetectPlugin interface with optional frame buffering.
 * This plugin simulates object detection by finding colored regions in the frame.
 *
 * Parameters:
 *   delay=N   - Buffer N frames before outputting (default: 0, immediate output)
 *
 * Example usage:
 *   ffmpeg -i input.mp4 -vf oc_plugin=plugin=libdetect_plugin.dylib:params=delay=2 output.mp4
 */

#include <quink_oc_plugin.h>
#include <opencv2/imgproc.hpp>
#include <cstdlib>
#include <cstring>
#include <deque>

class DemoDetectPlugin : public QuinkOCDetectPlugin {
public:
    bool init(const char *params, int nb_inputs, int nb_outputs) override {
        // DETECT plugins must be 1:1
        if (nb_inputs != 1 || nb_outputs != 1)
            return false;

        // Parse delay parameter
        if (params && params[0]) {
            const char *pos = strstr(params, "delay=");
            if (pos) {
                delay_frames_ = atoi(pos + 6);
                if (delay_frames_ < 0) delay_frames_ = 0;
                if (delay_frames_ > 10) delay_frames_ = 10;
            }
        }

        return true;
    }

    void uninit() override {
        frame_buffer_.clear();
    }

    QuinkOCProcessResult detect(const cv::Mat &input, cv::Mat &output,
                                QuinkOCDetections &detections) override {
        detections.clear();

        if (delay_frames_ == 0) {
            // Immediate mode: process and return immediately
            output = input;  // Pass-through (zero-copy)
            detectColoredRegions(input, detections);
            return QuinkOCProcessResult::QUINK_OC_OK;
        }

        // Delayed mode: buffer frames
        frame_buffer_.push_back(input.clone());

        if (static_cast<int>(frame_buffer_.size()) <= delay_frames_) {
            // Still buffering, no output yet
            return QuinkOCProcessResult::QUINK_OC_TRY_AGAIN;
        }

        // Output the oldest buffered frame
        output = frame_buffer_.front();
        detectColoredRegions(output, detections);
        frame_buffer_.pop_front();

        return QuinkOCProcessResult::QUINK_OC_OK;
    }

    bool flushDetect(cv::Mat &output, QuinkOCDetections &detections) override {
        detections.clear();

        if (frame_buffer_.empty())
            return false;

        output = frame_buffer_.front();
        detectColoredRegions(output, detections);
        frame_buffer_.pop_front();

        return true;
    }

private:
    /**
     * Demo detection: find colored regions in the image.
     * This simulates real detection by finding red, green, and blue regions.
     */
    void detectColoredRegions(const cv::Mat &frame, QuinkOCDetections &detections) {
        cv::Mat hsv;
        cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);

        // Define color ranges to detect
        struct ColorDef {
            const char* name;
            int class_id;
            cv::Scalar lower;
            cv::Scalar upper;
        };

        ColorDef colors[] = {
            {"red",    0, cv::Scalar(0, 100, 100),   cv::Scalar(10, 255, 255)},
            {"green",  1, cv::Scalar(35, 100, 100),  cv::Scalar(85, 255, 255)},
            {"blue",   2, cv::Scalar(100, 100, 100), cv::Scalar(130, 255, 255)},
            {"yellow", 3, cv::Scalar(20, 100, 100),  cv::Scalar(35, 255, 255)},
        };

        for (const auto& color : colors) {
            cv::Mat mask;
            cv::inRange(hsv, color.lower, color.upper, mask);

            // Find contours
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

            for (const auto& contour : contours) {
                double area = cv::contourArea(contour);
                // Filter small regions (noise)
                if (area < 500) continue;

                cv::Rect bbox = cv::boundingRect(contour);
                // Calculate confidence based on area (demo purposes)
                float confidence = std::min(1.0f, static_cast<float>(area) / 10000.0f);

                detections.add(bbox, color.class_id, confidence, color.name);
            }
        }
    }

    int delay_frames_ = 0;
    std::deque<cv::Mat> frame_buffer_;
};

/*
 * Register as DETECT plugin.
 *
 * Data flow:
 *   Input --> detect() --> TRY_AGAIN (buffering if delay > 0)
 *                      --> OK (output frame + detections)
 *   EOF --> flushDetect() --> output remaining buffered frames
 */
QUINK_OC_DETECT_PLUGIN_ENTRY(DemoDetectPlugin, "demo_detect",
                             "Demo detection plugin with optional frame buffering (delay=N)")
