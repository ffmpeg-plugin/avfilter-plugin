# FFmpeg OpenCV Plugin SDK

Header-only SDK for creating OpenCV-based video filter plugins for FFmpeg's `oc_plugin` filter.

For detailed API documentation and plugin development guide, see [Plugin API Documentation](docs/plugin_api.md).

## Build Example Plugins

```bash
cmake -B build && cmake --build build
```

To use as header-only library only (no OpenCV required):
```bash
cmake -B build -DBUILD_PLUGINS=OFF
```

To specify FFmpeg path and run tests:
```bash
cmake -B build -DFFMPEG_CMD=/path/to/ffmpeg && cmake --build build && ctest --test-dir build --output-on-failure
```

## Plugin Usage Examples

### CPU Plugins

```bash
# Blur (ksize: kernel size, must be odd; scale: resize factor)
ffmpeg -i input.mp4 -vf "oc_plugin=plugin=libblur_plugin.dylib:params='ksize=15&scale=0.5'" output.mp4

# Frame averaging with buffering (frames: 1-16, uses TryAgain/flush for temporal buffering)
ffmpeg -i input.mp4 -vf "oc_plugin=plugin=libavgframes_plugin.dylib:params='frames=5'" output.mp4

# Object detection with optional delay (delay: 0-10 frames)
ffmpeg -i input.mp4 -vf "oc_plugin=plugin=libdetect_plugin.dylib:params='delay=2'" output.mp4

# Blend two inputs (alpha: 0.0-1.0, N:1 multi-input mode)
ffmpeg -i bg.mp4 -i fg.mp4 \
    -filter_complex "[0:v][1:v]oc_plugin=plugin=libblend_plugin.dylib:inputs=2:params='alpha=0.5'" \
    output.mp4

# Split: single input -> multiple outputs (1:N multi-output mode)
# out0: passthrough, out1: grayscale, out2: edge detection
ffmpeg -i input.mp4 \
    -filter_complex "oc_plugin=plugin=libsplit_plugin.dylib:outputs=3:params='outputs=3'[out0][out1][out2]" \
    -map "[out0]" passthrough.mp4 -map "[out1]" gray.mp4 -map "[out2]" edges.mp4
```

### CUDA Plugins

CUDA plugins are designed for **full-GPU transcoding pipelines** — hardware decode, GPU filter, and hardware encode — all without frames ever touching CPU memory. This is the primary use case and where CUDA plugins deliver their real value.

> **⚠️ Key: NV12/P016 → BGRA GPU Color Conversion**
>
> In a full-GPU pipeline, hardware decoders output **NV12** (8-bit) or **P016** (10/16-bit) frames as `cv::cuda::GpuMat`.
> Most OpenCV CUDA functions expect **BGRA** input, so the plugin must convert NV12/P016 → BGRA on the GPU before processing.
>
> **The plugin does NOT need to convert back to NV12.** Instead, set `out.pix_fmt = quink::QPixelFormat::BGRA` in `configure()` to tell the host the output is BGRA. Hardware encoders like `hevc_nvenc` accept BGRA input directly.
>
> For the conversion, use `cv::cudacodec::createNVSurfaceToColorConverter` (handles colorspace/range correctly). **Do NOT use `cv::cuda::cvtColor(COLOR_YUV2BGRA_NV12)`** — OpenCV CUDA does not support this conversion code and will throw an exception at runtime. OpenCV CUDA does not provide a BGRA→NV12 conversion either.
>
> Only `cuda_blur_plugin` demonstrates this NV12/P016 → BGRA conversion. Refer to its source code for a complete implementation.
> Other CUDA demo plugins (blend, split) are provided for testing only via `hwupload_cuda`/`hwdownload`.

```bash
# ✅ Recommended: Full GPU pipeline (hw decode → plugin → hw encode, zero CPU copy)
# Hardware decoder outputs NV12 on GPU → plugin converts NV12→BGRA, processes, converts back → hw encode
ffmpeg -hwaccel cuda -hwaccel_output_format cuda -i input.mp4 \
    -vf "oc_plugin=plugin=./libcuda_blur_plugin.so:params='ksize=15'" \
    -c:v hevc_nvenc -b:v 2M output.mp4
```

For **testing or software-decoded inputs**, use `hwupload_cuda` / `hwdownload` to move frames between CPU and GPU:

```bash
# Testing only: CPU decode → hwupload → plugin → hwdownload → CPU encode
# In this path, frames are BGRA on GPU, so NV12 conversion is NOT exercised
ffmpeg -i input.mp4 \
    -vf "format=bgra,hwupload_cuda,oc_plugin=plugin=./libcuda_blur_plugin.so:params='ksize=15',hwdownload,format=bgra" \
    output.mp4
```

For detailed API documentation including NV12/P016 GpuMat layout diagrams and conversion code examples, see [Plugin API Documentation](docs/plugin_api.md#cuda-process-plugin).

Note: CUDA plugins only work on Linux and Windows (NVIDIA GPU required). Use `.so` on Linux, `.dll` on Windows.

## Filter Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `plugin` | string | (required) | Path to plugin shared library |
| `params` | string | (none) | Parameters passed to plugin's `init()` |
| `inputs` | int | 1 | Number of inputs (1-8) |
| `outputs` | int | 1 | Number of outputs (1-8) |
| `shortest` | bool | false | Terminate when shortest input ends (N:1 mode) |

## Windows Platform Notes

⚠️ **Important**: On Windows, FFmpeg cannot load plugin DLLs from absolute paths due to Windows DLL loading restrictions. You must place `ffmpeg.exe` and the plugin DLL in the **same directory**.

### Recommended Setup

1. Copy `ffmpeg.exe` to your plugin build directory, or
2. Copy the plugin `.dll` files to the directory where `ffmpeg.exe` is located

```powershell
# Example: Copy ffmpeg to plugin directory
copy C:\path\to\ffmpeg.exe C:\path\to\plugins\

# Then run from the plugin directory
cd C:\path\to\plugins
ffmpeg -i input.mp4 -vf "oc_plugin=plugin=libblur_plugin.dll:params='ksize=5'" output.mp4
```

### Why This Matters

Windows restricts DLL loading paths for security reasons. When FFmpeg tries to load a plugin DLL:
- ❌ `plugin=C:\full\path\to\libblur_plugin.dll` — **Will fail**
- ✅ `plugin=libblur_plugin.dll` (in same directory as ffmpeg.exe) — **Works**

### CMake Option: COPY_FFMPEG_DEPS

On Windows, the `COPY_FFMPEG_DEPS` option (enabled by default) automatically copies `ffmpeg.exe` and all its dependent DLLs to the build directory during build, so tests can run without manual setup.

```bash
# Enabled by default on Windows
cmake -B build

# Disable if you manage dependencies manually
cmake -B build -DCOPY_FFMPEG_DEPS=OFF
```

This option is only available on Windows and has no effect on other platforms.

## License

This project is licensed under the **GNU Lesser General Public License v2.1 or later (LGPL-2.1-or-later)**. See the [LICENSE](LICENSE) file for details.

> **Note on GPL compatibility**: When FFmpeg itself is built with GPL-licensed components enabled (i.e., configured with `--enable-gpl`), the combined work is governed by the **GNU General Public License (GPL)**. In that case, these plugins — as part of the combined work — are also subject to the terms of the GPL.
