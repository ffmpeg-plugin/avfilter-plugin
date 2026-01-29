# FFmpeg OpenCV Plugin SDK

Header-only SDK for creating OpenCV-based video filter plugins for FFmpeg's `oc_plugin` filter.

## Build Example Plugins

```bash
cmake -B build && cmake --build build
```

To use as header-only library only (no OpenCV required):
```bash
cmake -B build -DBUILD_PLUGINS=OFF
```

## Plugin Usage Examples

```bash
# Blur (ksize: kernel size, must be odd)
ffmpeg -i input.mp4 -vf "oc_plugin=plugin=libblur_plugin.dylib:params='ksize=5'" output.mp4

# Blend two inputs (alpha: 0.0-1.0)
ffmpeg -i bg.mp4 -i fg.mp4 \
    -filter_complex "[0:v][1:v]oc_plugin=plugin=libblend_plugin.dylib:inputs=2:params='alpha=0.5'" \
    output.mp4

# Frame averaging (frames: 1-16)
ffmpeg -i input.mp4 -vf "oc_plugin=plugin=libavgframes_plugin.dylib:params='frames=3'" output.mp4

# Split: single input -> multiple outputs (outputs: 1-4)
# out0: passthrough, out1: grayscale, out2: edge detection
ffmpeg -i input.mp4 \
    -filter_complex "oc_plugin=plugin=libsplit_plugin.dylib:outputs=3:params='outputs=3'[out0][out1][out2]" \
    -map "[out0]" passthrough.mp4 -map "[out1]" gray.mp4 -map "[out2]" edges.mp4
```

Note: Use `.so` on Linux, `.dylib` on macOS, `.dll` on Windows.
