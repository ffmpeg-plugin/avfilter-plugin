/*
 * Common utilities for oc_plugin demo plugins.
 *
 * Provides shared parameter parsing and configure logic
 * that is reused between CPU and CUDA plugin variants.
 */
#ifndef OC_PLUGIN_COMMON_H
#define OC_PLUGIN_COMMON_H

#include <cstdlib>
#include <cstring>

#include "quink_oc_plugin.h"

namespace plugin_common {

/**
 * Parse "key=value" from a parameter string.
 * @return true if key was found and value was parsed
 */
static inline bool parseFloat(const char *params, const char *key, float &out) {
    if (!params) return false;
    const char *pos = strstr(params, key);
    if (!pos) return false;
    out = static_cast<float>(atof(pos + strlen(key)));
    return true;
}

static inline bool parseDouble(const char *params, const char *key, double &out) {
    if (!params) return false;
    const char *pos = strstr(params, key);
    if (!pos) return false;
    out = atof(pos + strlen(key));
    return true;
}

static inline bool parseInt(const char *params, const char *key, int &out) {
    if (!params) return false;
    const char *pos = strstr(params, key);
    if (!pos) return false;
    out = atoi(pos + strlen(key));
    return true;
}

/*===========================================================================
 * BlurParams: Shared parameter handling for blur plugins
 *===========================================================================*/

struct BlurParams {
    int kernel_size = 5;
    float scale = 1.0f;

    bool parse(const char *params) {
        if (!params || !params[0]) return true;
        parseInt(params, "ksize=", kernel_size);
        if (kernel_size % 2 == 0) kernel_size++;
        if (kernel_size < 1) kernel_size = 1;
        parseFloat(params, "scale=", scale);
        return scale >= 0;
    }

    bool configureOutput(const quink::FrameConfig &in, quink::FrameConfig &out) {
        out.width = static_cast<int>(in.width * scale);
        out.height = static_cast<int>(in.height * scale);
        return true;
    }
};

/*===========================================================================
 * BlendParams: Shared parameter handling for blend plugins
 *===========================================================================*/

struct BlendParams {
    double alpha = 0.5;

    bool parse(const char *params) {
        if (!params || !params[0]) return true;
        parseDouble(params, "alpha=", alpha);
        if (alpha < 0.0) alpha = 0.0;
        if (alpha > 1.0) alpha = 1.0;
        return true;
    }
};

/*===========================================================================
 * SplitConfig: Shared configure logic for split plugins
 *===========================================================================*/

struct SplitConfig {
    int num_outputs = 0;

    /**
     * Configure split outputs:
     *   Output 0: same as input
     *   Output 1: same as input (for blur/grayscale)
     *   Output 2: half resolution
     *   Output 3: half resolution (for blur+resize)
     */
    bool configureOutputs(const quink::FrameConfig &in,
                          std::vector<quink::FrameConfig> &outputs) {
        for (int i = 0; i < num_outputs; i++) {
            outputs[i].pix_fmt = in.pix_fmt;
            if (i < 2) {
                outputs[i].width = in.width;
                outputs[i].height = in.height;
            } else {
                outputs[i].width = in.width / 2;
                outputs[i].height = in.height / 2;
            }
        }
        return true;
    }
};

} // namespace plugin_common

#endif /* OC_PLUGIN_COMMON_H */
