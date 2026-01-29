/*
 * OpenCV Plugin Interface for FFmpeg
 *
 * Copyright (c) 2026 Zhao Zhili <quinkblack@foxmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVFILTER_QUINK_OC_PLUGIN_H
#define AVFILTER_QUINK_OC_PLUGIN_H

#include <opencv2/core.hpp>
#include <vector>

#define QUINK_OC_PLUGIN_API_VERSION 1

/**
 * Supported I/O modes:
 *   - Single-input, single-output (1:1)
 *   - Multi-input, single-output (N:1) - e.g., video compositing, blending
 *   - Single-input, multi-output (1:N) - e.g., video splitting, analysis
 *
 * Multi-input + multi-output (N:M where N>1 and M>1) is NOT supported.
 * Use filter chains to achieve complex routing if needed.
 */

struct QuinkOCFrameConfig {
    int width;
    int height;
    int cv_type;     ///< OpenCV type (e.g., CV_8UC3), ignored for output
};

enum QuinkOCProcessResult {
    QUINK_OC_OK = 0,              ///< Success, output frame(s) produced
    QUINK_OC_TRY_AGAIN = 1,       ///< Success, but output not ready yet
    QUINK_OC_ERROR = -1           ///< Processing error
};

class QuinkOCPlugin {
public:
    virtual ~QuinkOCPlugin() = default;

    /**
     * Initialize the plugin
     * @param params      User-specified parameter string (may be NULL)
     * @param nb_inputs   Number of inputs configured by user (via AVOption)
     * @param nb_outputs  Number of outputs configured by user (via AVOption)
     * @return true on success
     */
    virtual bool init(const char *params, int nb_inputs, int nb_outputs) = 0;

    /**
     * Process frames
     *
     * This method is called for each set of input frames.
     *
     * Allowed usage for outputs:
     *   1. Write directly to output buffer: input.copyTo(output)
     *   2. Zero-copy pass-through: output = input
     *
     * NOT allowed (will cause error):
     *   - output = input.clone() (defeats zero-copy, use copyTo instead)
     *   - output.create(...) or any reallocation
     *
     * @param inputs   Input cv::Mat images (zero-copy from FFmpeg, refcount tied to AVFrame)
     * @param outputs  Output cv::Mat images (pre-allocated buffer to write into)
     */
    virtual QuinkOCProcessResult process(const std::vector<cv::Mat> &inputs,
                                         std::vector<cv::Mat> &outputs) = 0;

    /**
     * Flush buffered frames at end of stream
     *
     * Called when input stream ends. The plugin should output any remaining
     * buffered frames. This method may be called multiple times until it
     * returns false (no more frames to output).
     *
     * @param outputs  Output buffer to write flushed frame into
     * @return true if a frame was output, false if no more frames
     */
    virtual bool flush(std::vector<cv::Mat> &outputs) = 0;

    /**
     * Configure plugin with all input/output dimensions
     *
     * Called during filter configuration. Plugin sets output dimensions based
     * on all inputs. Each output's width/height is initialized to corresponding
     * input's dimensions (output[i] = input[i], or input[0] if i >= num_inputs).
     *
     * @param inputs   Input configurations (read-only)
     * @param outputs  Output configurations (plugin fills width/height)
     * @return true on success
     */
    virtual bool configure(const std::vector<QuinkOCFrameConfig> &inputs,
                           std::vector<QuinkOCFrameConfig> &outputs) = 0;
    virtual void uninit() = 0;
};

/**
 * Plugin Descriptor
 *
 * Contains plugin metadata and factory functions.
 * Plugins export a single function that returns a pointer to a static descriptor.
 */
struct QuinkOCPluginDescriptor {
    int api_version;            ///< Must be QUINK_OC_PLUGIN_API_VERSION
    const char *name;           ///< Plugin name
    const char *description;    ///< Plugin description

    QuinkOCPlugin* (*create)();            ///< Create plugin instance
    void (*destroy)(QuinkOCPlugin* p);     ///< Destroy plugin instance
};

typedef const QuinkOCPluginDescriptor* (*QuinkOCPluginGetDescriptorFunc)();

/** Symbol name to load from shared library */
#define QUINK_OC_PLUGIN_DESCRIPTOR_SYMBOL "quink_oc_plugin_get_descriptor"

#if defined(_WIN32) || defined(_WIN64)
    #define QUINK_OC_EXPORT __declspec(dllexport)
#else
    #define QUINK_OC_EXPORT __attribute__((visibility("default")))
#endif

/**
 * Plugin entry macro
 *
 * Usage: QUINK_OC_PLUGIN_ENTRY(PluginClass, "name", "description")
 */
#define QUINK_OC_PLUGIN_ENTRY(PluginClass, plugin_name, plugin_desc) \
    static QuinkOCPlugin* _quink_create() { return new PluginClass(); } \
    static void _quink_destroy(QuinkOCPlugin* p) { delete p; } \
    extern "C" QUINK_OC_EXPORT const QuinkOCPluginDescriptor* quink_oc_plugin_get_descriptor() { \
        static const QuinkOCPluginDescriptor desc = { \
            QUINK_OC_PLUGIN_API_VERSION, \
            plugin_name, \
            plugin_desc, \
            _quink_create, \
            _quink_destroy \
        }; \
        return &desc; \
    }

#endif /* AVFILTER_QUINK_OC_PLUGIN_H */
