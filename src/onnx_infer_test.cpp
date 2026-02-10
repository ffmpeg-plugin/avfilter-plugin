/*
 * Unit test for OnnxInferPlugin (CPU cv::Mat version)
 *
 * Tests the plugin through the quink::ProcessPlugin interface directly,
 * without any FFmpeg dependency.
 *
 * Usage:
 *   onnx_infer_test <model_path> [input_image] [output_image]
 *       [--input <name>] [--output <name>]
 *       [--noise <float>] [--blur <float>]
 *       [--tile_h <int>] [--tile_w <int>]
 *       [--overlap <int>]
 *
 * If input_image is omitted, a synthetic gradient test pattern is used.
 */

#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include "quink_oc_plugin.h"

/* Forward declare the descriptor function from the plugin shared library */
extern "C" const QuinkOCPluginDescriptor* quink_oc_plugin_get_descriptor();

static void printUsage(const char *prog) {
    std::cerr << "Usage: " << prog << " <model_path> [input_image] [output_image]\n"
              << "         [--input <name>] [--output <name>]\n"
              << "         [--noise <float>] [--blur <float>]\n"
              << "         [--tile_h <int>] [--tile_w <int>]\n"
              << "         [--overlap <int>]\n"
              << "\n"
              << "If input_image is omitted, a 512x512 synthetic pattern is used.\n";
}

static cv::Mat createTestPattern(int h, int w) {
    cv::Mat img(h, w, CV_8UC3);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            img.at<cv::Vec3b>(y, x) = cv::Vec3b(
                static_cast<uint8_t>(x * 255 / w),
                static_cast<uint8_t>(y * 255 / h),
                static_cast<uint8_t>((x + y) * 255 / (w + h))
            );
        }
    }
    return img;
}

static double computePSNR(const cv::Mat &a, const cv::Mat &b) {
    if (a.size() != b.size() || a.type() != b.type())
        return 0.0;
    cv::Mat diff;
    cv::absdiff(a, b, diff);
    diff.convertTo(diff, CV_32F);
    diff = diff.mul(diff);
    cv::Scalar s = cv::sum(diff);
    double mse = (s[0] + s[1] + s[2]) / (a.channels() * a.total());
    if (mse < 1e-10) return 100.0;
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    /* Parse arguments */
    std::string model_path;
    std::string input_image;
    std::string output_image;
    std::string input_name = "input";
    std::string output_name = "output";
    float noise = 0.24f;
    float blur_val = 1.0f;
    int tile_h = -1;
    int tile_w = -1;
    int overlap = -1;  /* -1 = auto (tile_size / 4) */

    int positional = 0;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input_name = argv[++i];
        } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_name = argv[++i];
        } else if (std::strcmp(argv[i], "--noise") == 0 && i + 1 < argc) {
            noise = std::stof(argv[++i]);
        } else if (std::strcmp(argv[i], "--blur") == 0 && i + 1 < argc) {
            blur_val = std::stof(argv[++i]);
        } else if (std::strcmp(argv[i], "--tile_h") == 0 && i + 1 < argc) {
            tile_h = std::stoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--tile_w") == 0 && i + 1 < argc) {
            tile_w = std::stoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--overlap") == 0 && i + 1 < argc) {
            overlap = std::stoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            if (positional == 0) model_path = argv[i];
            else if (positional == 1) input_image = argv[i];
            else if (positional == 2) output_image = argv[i];
            positional++;
        }
    }

    if (model_path.empty()) {
        std::cerr << "Error: model path is required\n";
        return 1;
    }

    /* Build params string */
    std::string params = "model=" + model_path;
    params += "&input=" + input_name;
    params += "&output=" + output_name;
    if (overlap >= 0)
        params += "&overlap=" + std::to_string(overlap);
    if (tile_h > 0)
        params += "&tile_h=" + std::to_string(tile_h);
    if (tile_w > 0)
        params += "&tile_w=" + std::to_string(tile_w);
    params += "&noise=" + std::to_string(noise);
    params += "&blur=" + std::to_string(blur_val);

    std::cout << "=== ONNX Infer Plugin Unit Test ===" << std::endl;
    std::cout << "Params: " << params << std::endl;

    /* Get plugin descriptor */
    const QuinkOCPluginDescriptor *desc = quink_oc_plugin_get_descriptor();
    if (!desc) {
        std::cerr << "Error: Failed to get plugin descriptor" << std::endl;
        return 1;
    }

    std::cout << "Plugin: " << desc->name << " - " << desc->description << std::endl;
    std::cout << "API version: " << desc->api_version << std::endl;

    /* Create plugin instance */
    quink::PluginBase *base = desc->create();
    if (!base) {
        std::cerr << "Error: Failed to create plugin instance" << std::endl;
        return 1;
    }

    auto *plugin = dynamic_cast<quink::ProcessPlugin *>(base);
    if (!plugin) {
        std::cerr << "Error: Plugin is not a ProcessPlugin" << std::endl;
        desc->destroy(base);
        return 1;
    }

    /* Initialize */
    std::cout << "\n--- init ---" << std::endl;
    if (!plugin->init(params.c_str(), 1, 1)) {
        std::cerr << "Error: Plugin init failed" << std::endl;
        desc->destroy(base);
        return 1;
    }
    std::cout << "init: OK" << std::endl;

    /* Load or create test image */
    cv::Mat input_mat;
    if (!input_image.empty()) {
        input_mat = cv::imread(input_image, cv::IMREAD_COLOR);
        if (input_mat.empty()) {
            std::cerr << "Error: Cannot read input image: " << input_image << std::endl;
            plugin->uninit();
            desc->destroy(base);
            return 1;
        }
        std::cout << "Input: " << input_image
                  << " (" << input_mat.cols << "x" << input_mat.rows << ")" << std::endl;
    } else {
        input_mat = createTestPattern(512, 512);
        std::cout << "Input: synthetic pattern (512x512)" << std::endl;
    }

    /* Configure */
    std::cout << "\n--- configure ---" << std::endl;
    quink::FrameConfig in_cfg;
    in_cfg.width = input_mat.cols;
    in_cfg.height = input_mat.rows;
    in_cfg.cv_type = input_mat.type();
    in_cfg.pix_fmt = (input_mat.channels() == 4)
        ? quink::QPixelFormat::BGRA : quink::QPixelFormat::BGR;

    quink::FrameConfig out_cfg = in_cfg;
    std::vector<quink::FrameConfig> in_cfgs = {in_cfg};
    std::vector<quink::FrameConfig> out_cfgs = {out_cfg};

    if (!plugin->configure(in_cfgs, out_cfgs)) {
        std::cerr << "Error: Plugin configure failed" << std::endl;
        plugin->uninit();
        desc->destroy(base);
        return 1;
    }
    std::cout << "configure: OK (output " << out_cfgs[0].width << "x"
              << out_cfgs[0].height << ")" << std::endl;

    int scale_w = (in_cfg.width  > 0) ? out_cfgs[0].width  / in_cfg.width  : 1;
    int scale_h = (in_cfg.height > 0) ? out_cfgs[0].height / in_cfg.height : 1;
    if (scale_w < 1) scale_w = 1;
    if (scale_h < 1) scale_h = 1;
    if (scale_w > 1 || scale_h > 1)
        std::cout << "Super-resolution: " << scale_w << "x" << scale_h << std::endl;

    /* Process */
    std::cout << "\n--- process ---" << std::endl;
    cv::Mat output_mat(out_cfgs[0].height, out_cfgs[0].width, input_mat.type());

    std::vector<cv::Mat> inputs_vec = {input_mat};
    quink::ProcessOutput proc_out;
    proc_out.frame = output_mat;
    std::vector<quink::ProcessOutput> outputs_vec = {proc_out};

    auto result = plugin->process(inputs_vec, outputs_vec);
    if (result != quink::ProcessResult::Ok) {
        std::cerr << "Error: Plugin process failed (result="
                  << static_cast<int>(result) << ")" << std::endl;
        plugin->uninit();
        desc->destroy(base);
        return 1;
    }

    output_mat = outputs_vec[0].frame;
    std::cout << "process: OK" << std::endl;

    /* Verify output */
    std::cout << "\n--- verification ---" << std::endl;
    std::cout << "Output size: " << output_mat.cols << "x" << output_mat.rows
              << " channels=" << output_mat.channels()
              << " type=" << output_mat.type() << std::endl;

    /* Check output is not identical to input (model did something) */
    double psnr;
    if (input_mat.size() == output_mat.size()) {
        psnr = computePSNR(input_mat, output_mat);
        std::cout << "PSNR (input vs output): " << psnr << " dB" << std::endl;
    } else {
        /* Super-resolution: resize input to output size for comparison */
        cv::Mat input_resized;
        cv::resize(input_mat, input_resized, output_mat.size(), 0, 0, cv::INTER_CUBIC);
        psnr = computePSNR(input_resized, output_mat);
        std::cout << "PSNR (resized input vs output): " << psnr << " dB" << std::endl;
    }

    if (psnr > 60.0) {
        std::cerr << "WARNING: Output is nearly identical to input (PSNR > 60 dB)."
                  << " Model may not be working correctly." << std::endl;
    }

    /* Check output is not all black/white */
    cv::Scalar mean_val = cv::mean(output_mat);
    std::cout << "Output mean: [" << mean_val[0] << ", " << mean_val[1]
              << ", " << mean_val[2] << "]" << std::endl;

    bool valid = true;
    if (mean_val[0] < 1.0 && mean_val[1] < 1.0 && mean_val[2] < 1.0) {
        std::cerr << "FAIL: Output is all black!" << std::endl;
        valid = false;
    }
    if (mean_val[0] > 254.0 && mean_val[1] > 254.0 && mean_val[2] > 254.0) {
        std::cerr << "FAIL: Output is all white!" << std::endl;
        valid = false;
    }

    /* Save output if requested */
    if (!output_image.empty()) {
        cv::imwrite(output_image, output_mat);
        std::cout << "Output saved: " << output_image << std::endl;
    }

    /* Flush test */
    std::cout << "\n--- flush ---" << std::endl;
    bool flushed = plugin->flush(outputs_vec);
    std::cout << "flush: " << (flushed ? "returned frame" : "no more frames") << std::endl;

    /* Cleanup */
    plugin->uninit();
    desc->destroy(base);

    std::cout << "\n=== " << (valid ? "PASS" : "FAIL") << " ===" << std::endl;
    return valid ? 0 : 1;
}
