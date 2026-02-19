macro(add_plugin name)
    add_library(${name}_plugin SHARED ${name}_plugin.cpp)
    target_link_libraries(${name}_plugin PRIVATE quink_oc_plugin ${OpenCV_LIBS})
    target_include_directories(${name}_plugin PRIVATE ${OpenCV_INCLUDE_DIRS})
    install(TARGETS ${name}_plugin LIBRARY DESTINATION lib)
endmacro()

add_plugin(blur)
add_plugin(blend)
add_plugin(avgframes)
add_plugin(split)
add_plugin(detect)

# ONNX Runtime plugins (optional)
find_path(ORT_INCLUDE_DIR onnxruntime_cxx_api.h
    PATHS
        /opt/homebrew/include/onnxruntime
        /usr/local/include/onnxruntime
        /usr/include/onnxruntime
        $ENV{ONNXRUNTIME_ROOT}/include
    PATH_SUFFIXES onnxruntime
)
find_library(ORT_LIBRARY NAMES onnxruntime
    PATHS
        /opt/homebrew/lib
        /usr/local/lib
        /usr/lib
        $ENV{ONNXRUNTIME_ROOT}/lib
)

if(ORT_INCLUDE_DIR AND ORT_LIBRARY)
    message(STATUS "ONNX Runtime found: ${ORT_LIBRARY}")
    message(STATUS "ONNX Runtime headers: ${ORT_INCLUDE_DIR}")

    # Detect available hardware Execution Providers and set compile definitions.
    # Each EP is guarded by a preprocessor macro (USE_CUDA, USE_TENSORRT, etc.)
    # so that the plugin code only attempts to register EPs that were compiled in.
    set(ONNX_EP_DEFS "")

    # NVIDIA CUDA EP
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        list(APPEND ONNX_EP_DEFS USE_CUDA)
        message(STATUS "  ONNX EP: CUDA enabled (CUDA ${CUDAToolkit_VERSION})")
    endif()

    # NVIDIA TensorRT EP
    find_library(NVINFER_LIBRARY NAMES nvinfer PATHS /usr/lib/x86_64-linux-gnu /usr/local/lib)
    if(NVINFER_LIBRARY)
        list(APPEND ONNX_EP_DEFS USE_TENSORRT)
        message(STATUS "  ONNX EP: TensorRT enabled (${NVINFER_LIBRARY})")
    endif()

    # Intel OpenVINO EP
    find_package(OpenVINO QUIET)
    if(OpenVINO_FOUND)
        list(APPEND ONNX_EP_DEFS USE_OPENVINO)
        message(STATUS "  ONNX EP: OpenVINO enabled")
    endif()

    # AMD ROCm EP
    if(EXISTS "/opt/rocm/lib/libamdhip64.so")
        list(APPEND ONNX_EP_DEFS USE_ROCM)
        message(STATUS "  ONNX EP: ROCm enabled")
    endif()

    # AMD MIGraphX EP
    find_library(MIGRAPHX_LIBRARY NAMES migraphx PATHS /opt/rocm/lib)
    if(MIGRAPHX_LIBRARY)
        list(APPEND ONNX_EP_DEFS USE_MIGRAPHX)
        message(STATUS "  ONNX EP: MIGraphX enabled")
    endif()

    # DirectML EP (Windows)
    if(WIN32)
        find_library(DML_LIBRARY NAMES DirectML directml)
        if(DML_LIBRARY)
            list(APPEND ONNX_EP_DEFS USE_DIRECTML)
            message(STATUS "  ONNX EP: DirectML enabled")
        else()
            # DirectML ships with Windows 10+, always try on Windows
            list(APPEND ONNX_EP_DEFS USE_DIRECTML)
            message(STATUS "  ONNX EP: DirectML enabled (system)")
        endif()
    endif()

    # XNNPACK EP (ARM optimized CPU)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64|ARM")
        list(APPEND ONNX_EP_DEFS USE_XNNPACK)
        message(STATUS "  ONNX EP: XNNPACK enabled (ARM)")
    endif()

    # CoreML EP is handled by __APPLE__ in source code, no extra definition needed.

    # CPU ONNX inference plugin
    add_library(onnx_infer_plugin SHARED onnx_infer_plugin.cpp)
    target_link_libraries(onnx_infer_plugin PRIVATE quink_oc_plugin ${OpenCV_LIBS} ${ORT_LIBRARY})
    target_include_directories(onnx_infer_plugin PRIVATE ${OpenCV_INCLUDE_DIRS} ${ORT_INCLUDE_DIR})
    target_compile_definitions(onnx_infer_plugin PRIVATE ${ONNX_EP_DEFS})
    install(TARGETS onnx_infer_plugin LIBRARY DESTINATION lib)

    # Unit test (links plugin object directly, no FFmpeg dependency)
    find_package(OpenCV QUIET COMPONENTS imgcodecs)
    if(OpenCV_imgcodecs_FOUND)
        add_executable(onnx_infer_test onnx_infer_test.cpp onnx_infer_plugin.cpp)
        target_link_libraries(onnx_infer_test PRIVATE quink_oc_plugin ${OpenCV_LIBS} ${ORT_LIBRARY})
        target_include_directories(onnx_infer_test PRIVATE ${OpenCV_INCLUDE_DIRS} ${ORT_INCLUDE_DIR})
        target_compile_definitions(onnx_infer_test PRIVATE ${ONNX_EP_DEFS})

        # CTest: run onnx_infer_test with bundled tiny sharpen model (no FFmpeg needed)
        set(TEST_ONNX_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_sharpen_model.onnx")
        if(EXISTS "${TEST_ONNX_MODEL}")
            add_test(NAME onnx_infer_test
                COMMAND onnx_infer_test "${TEST_ONNX_MODEL}" "" "" --overlap 0
            )
            message(STATUS "onnx_infer_test will be built (with CTest)")
        else()
            message(STATUS "onnx_infer_test will be built (model not found, CTest skipped)")
        endif()
    endif()

    set(HAVE_ONNX_PLUGINS TRUE)
else()
    message(STATUS "ONNX Runtime not found, skipping ONNX plugins")
    set(HAVE_ONNX_PLUGINS FALSE)
endif()

# Python bridge plugin (optional)
# Requires Python3 development files and pybind11
find_package(Python3 QUIET COMPONENTS Interpreter Development NumPy)
find_package(pybind11 QUIET)

if(Python3_FOUND AND pybind11_FOUND)
    message(STATUS "Python ${Python3_VERSION} found: ${Python3_EXECUTABLE}")
    message(STATUS "pybind11 found: ${pybind11_VERSION}")

    add_library(python_bridge_plugin SHARED python_bridge_plugin.cpp)
    target_link_libraries(python_bridge_plugin PRIVATE
        quink_oc_plugin ${OpenCV_LIBS} pybind11::embed Python3::Python)
    target_include_directories(python_bridge_plugin PRIVATE
        ${OpenCV_INCLUDE_DIRS} ${pybind11_INCLUDE_DIRS})
    install(TARGETS python_bridge_plugin LIBRARY DESTINATION lib)

    set(HAVE_PYTHON_BRIDGE TRUE)
else()
    if(NOT Python3_FOUND)
        message(STATUS "Python3 development files not found, skipping Python bridge plugin")
    endif()
    if(NOT pybind11_FOUND)
        message(STATUS "pybind11 not found, skipping Python bridge plugin")
    endif()
    set(HAVE_PYTHON_BRIDGE FALSE)
endif()

# Python SDK unit tests (pure Python, no C++ build required)
if(Python3_FOUND)
    set(PYTHON_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../python_sdk")
    if(EXISTS "${PYTHON_SDK_DIR}/tests/test_python_bridge.py")
        # Use unittest (always available) instead of pytest for portability
        add_test(NAME python_sdk_test
            COMMAND ${Python3_EXECUTABLE}
                "${PYTHON_SDK_DIR}/tests/test_python_bridge.py" -v
            WORKING_DIRECTORY "${PYTHON_SDK_DIR}"
        )
        message(STATUS "Python SDK tests enabled")
    endif()
endif()

# CUDA plugins (optional)
find_package(OpenCV QUIET COMPONENTS cudacodec cudafilters cudawarping cudaarithm)
if(OpenCV_cudafilters_FOUND)
    message(STATUS "OpenCV CUDA modules found, building CUDA plugins")
    add_plugin(cuda_blur)
    add_plugin(cuda_blend)
    add_plugin(cuda_split)

    set(HAVE_CUDA_PLUGINS TRUE)
else()
    message(STATUS "OpenCV CUDA modules not found, skipping CUDA plugins")
    set(HAVE_CUDA_PLUGINS FALSE)
endif()

# ============================================================================
# Test Configuration
# ============================================================================

# Check if FFMPEG_CMD is set and exists
if(NOT FFMPEG_CMD)
    message(STATUS "FFMPEG_CMD not set, skipping plugin tests")
    return()
endif()

if(NOT EXISTS "${FFMPEG_CMD}")
    message(WARNING "FFMPEG_CMD '${FFMPEG_CMD}' does not exist, skipping plugin tests")
    return()
endif()

message(STATUS "Using FFmpeg: ${FFMPEG_CMD}")

# Plugin directory (where shared libraries are built)
set(PLUGIN_DIR "$<TARGET_FILE_DIR:blur_plugin>")

if(COPY_FFMPEG_DEPS)
    get_filename_component(FFMPEG_DIR "${FFMPEG_CMD}" DIRECTORY)
    get_filename_component(FFMPEG_NAME "${FFMPEG_CMD}" NAME)

    # Copy ffmpeg.exe and all DLLs from ffmpeg directory to plugin build directory
    add_custom_target(copy_ffmpeg_deps ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_CMD}" "${PLUGIN_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${FFMPEG_DIR}/" "${PLUGIN_DIR}/"
        COMMENT "Copying FFmpeg and dependencies to build directory"
        VERBATIM
    )
    add_dependencies(copy_ffmpeg_deps blur_plugin)

    set(TEST_FFMPEG_CMD "${PLUGIN_DIR}/${FFMPEG_NAME}")
    # When ffmpeg is copied alongside plugins, no path prefix needed
    set(PLUGIN_PATH_PREFIX "")
else()
    set(TEST_FFMPEG_CMD "${FFMPEG_CMD}")
    # On Linux/macOS, dlopen requires ./ prefix to search current directory
    set(PLUGIN_PATH_PREFIX "./")
endif()

# Output directory for test artifacts — use a relative path so that
# ffmpeg arguments never contain Windows drive-letter colons (e.g., D:\...)
# which FFmpeg's option parser would misinterpret as key:value separators.
set(TEST_OUT_DIR "test_output")

# Ensure the directory exists at build time
add_custom_target(create_test_output_dir ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TEST_OUT_DIR}"
    WORKING_DIRECTORY ${PLUGIN_DIR}
    COMMENT "Creating test output directory"
)

# Common lavfi test source (small resolution for fast testing)
set(LAVFI_SRC "testsrc=duration=3:size=320x240:rate=30")

# Path to the verification script
set(VERIFY_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/verify_output.cmake")

# ----------------------------------------------------------------------------
# Test macros
#
# Each test runs ffmpeg directly (no bash/shell dependency, works on Windows).
# Tests output .nut files with rawvideo codec (zero encoding overhead,
# playable with ffplay, supports any pixel format including BGR24/BGRA).
#
# All paths in ffmpeg arguments use RELATIVE paths (relative to PLUGIN_DIR)
# to avoid Windows drive-letter colon issues (D:\path mis-parsed as key:value).
#
# Verification:
# - File size check (> 1KB, catches empty/broken output)
# - framemd5 uniformity check (catches black/green/corrupted screen)
# - Reference comparison (verifies filter was actually applied)
# ----------------------------------------------------------------------------

# Helper macro: add a verified test
# Usage: add_verified_test(NAME <name>
#            FFMPEG_ARGS <args string>
#            OUTPUT_FILES <file1> [file2 ...]
#            [REF_FFMPEG_ARGS <args string>]
#            [REF_OUTPUT_FILE <file>]
#            [EXPECT_DIFFERENT_FROM_REF]
#            [MIN_FILE_SIZE <bytes>])
macro(add_verified_test)
    cmake_parse_arguments(VT "EXPECT_DIFFERENT_FROM_REF"
        "NAME;FFMPEG_ARGS;REF_FFMPEG_ARGS;REF_OUTPUT_FILE;MIN_FILE_SIZE"
        "OUTPUT_FILES" ${ARGN})

    # Join OUTPUT_FILES with pipe separator to avoid CMake semicolon issues
    # in add_test COMMAND. The verify script converts | back to ; internally.
    string(REPLACE ";" "|" _VT_OUTPUT_FILES "${VT_OUTPUT_FILES}")

    add_test(NAME ${VT_NAME}
        COMMAND ${CMAKE_COMMAND}
            -DFFMPEG_CMD=${TEST_FFMPEG_CMD}
            -DFFMPEG_ARGS=${VT_FFMPEG_ARGS}
            -DOUTPUT_FILES=${_VT_OUTPUT_FILES}
            -DWORK_DIR=${PLUGIN_DIR}
            -DCHECK_FRAMEMD5=TRUE
            $<$<BOOL:${VT_REF_FFMPEG_ARGS}>:-DREF_FFMPEG_ARGS=${VT_REF_FFMPEG_ARGS}>
            $<$<BOOL:${VT_REF_OUTPUT_FILE}>:-DREF_OUTPUT_FILE=${VT_REF_OUTPUT_FILE}>
            $<$<BOOL:${VT_EXPECT_DIFFERENT_FROM_REF}>:-DEXPECT_DIFFERENT_FROM_REF=TRUE>
            $<$<BOOL:${VT_MIN_FILE_SIZE}>:-DMIN_FILE_SIZE=${VT_MIN_FILE_SIZE}>
            -P "${VERIFY_SCRIPT}"
        WORKING_DIRECTORY ${PLUGIN_DIR}
    )
endmacro()

# --- CPU plugin tests ---

# blur_test: 1:1 blur + scale, verify output differs from raw testsrc
add_verified_test(NAME blur_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -vf oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:blur_plugin>:params=ksize=15&scale=0.5 -c:v rawvideo -f nut ${TEST_OUT_DIR}/blur.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/blur.nut"
    REF_FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -c:v rawvideo -f nut ${TEST_OUT_DIR}/blur_ref.nut"
    REF_OUTPUT_FILE "${TEST_OUT_DIR}/blur_ref.nut"
    EXPECT_DIFFERENT_FROM_REF
)

# avgframes_test: buffered averaging
add_verified_test(NAME avgframes_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -vf oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:avgframes_plugin>:params=frames=5 -c:v rawvideo -f nut ${TEST_OUT_DIR}/avgframes.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/avgframes.nut"
)

# detect_test: detection with delay (uses dynamic testsrc, detect plugin passes through unmodified)
add_verified_test(NAME detect_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -vf oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:detect_plugin>:params=delay=2 -c:v rawvideo -f nut ${TEST_OUT_DIR}/detect.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/detect.nut"
)

# split_test: 1:3 split (pass-through, grayscale, edges)
add_verified_test(NAME split_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -filter_complex oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:split_plugin>:outputs=3:params=outputs=3[out0][out1][out2] -map [out0] -c:v rawvideo -f nut ${TEST_OUT_DIR}/split_pass.nut -map [out1] -c:v rawvideo -f nut ${TEST_OUT_DIR}/split_gray.nut -map [out2] -c:v rawvideo -f nut ${TEST_OUT_DIR}/split_edges.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/split_pass.nut;${TEST_OUT_DIR}/split_gray.nut;${TEST_OUT_DIR}/split_edges.nut"
)

# --- Python bridge plugin tests ---

if(HAVE_PYTHON_BRIDGE)

set(PY_BLUR_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../python_sdk/examples/blur_plugin.py")
set(PY_INVERT_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../python_sdk/examples/invert_plugin.py")

# python_bridge_blur_test: Test Python bridge with blur plugin
add_verified_test(NAME python_bridge_blur_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -vf oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:python_bridge_plugin>:params=script=${PY_BLUR_SCRIPT}&class=BlurPlugin&ksize=15 -c:v rawvideo -f nut ${TEST_OUT_DIR}/py_blur.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/py_blur.nut"
    REF_FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -c:v rawvideo -f nut ${TEST_OUT_DIR}/py_blur_ref.nut"
    REF_OUTPUT_FILE "${TEST_OUT_DIR}/py_blur_ref.nut"
    EXPECT_DIFFERENT_FROM_REF
)

# python_bridge_multi_test: Two different Python plugins chained (blur → invert)
# Tests that multiple Python bridge instances with different scripts coexist correctly.
add_verified_test(NAME python_bridge_multi_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -vf oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:python_bridge_plugin>:params=script=${PY_BLUR_SCRIPT}&class=BlurPlugin&ksize=15,oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:python_bridge_plugin>:params=script=${PY_INVERT_SCRIPT}&class=InvertPlugin -c:v rawvideo -f nut ${TEST_OUT_DIR}/py_multi.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/py_multi.nut"
    REF_FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -c:v rawvideo -f nut ${TEST_OUT_DIR}/py_multi_ref.nut"
    REF_OUTPUT_FILE "${TEST_OUT_DIR}/py_multi_ref.nut"
    EXPECT_DIFFERENT_FROM_REF
)

# python_bridge_same_plugin_different_params_test: Same plugin, different params
# Tests that two instances of the same Python class with different parameters
# maintain independent state (no shared instance contamination).
add_verified_test(NAME python_bridge_same_plugin_different_params_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -vf oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:python_bridge_plugin>:params=script=${PY_BLUR_SCRIPT}&class=BlurPlugin&ksize=3,oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:python_bridge_plugin>:params=script=${PY_BLUR_SCRIPT}&class=BlurPlugin&ksize=31 -c:v rawvideo -f nut ${TEST_OUT_DIR}/py_same_diff_params.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/py_same_diff_params.nut"
    REF_FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -c:v rawvideo -f nut ${TEST_OUT_DIR}/py_same_diff_params_ref.nut"
    REF_OUTPUT_FILE "${TEST_OUT_DIR}/py_same_diff_params_ref.nut"
    EXPECT_DIFFERENT_FROM_REF
)

endif()

# blend_test: 2:1 blend
add_verified_test(NAME blend_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -f lavfi -i color=c=blue:duration=3:size=320x240:rate=30 -filter_complex [0:v][1:v]oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:blend_plugin>:inputs=2:params=alpha=0.5 -c:v rawvideo -f nut ${TEST_OUT_DIR}/blend.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/blend.nut"
    REF_FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -c:v rawvideo -f nut ${TEST_OUT_DIR}/blend_ref.nut"
    REF_OUTPUT_FILE "${TEST_OUT_DIR}/blend_ref.nut"
    EXPECT_DIFFERENT_FROM_REF
)

# --- ONNX inference plugin tests ---

if(HAVE_ONNX_PLUGINS)

# Copy test model to plugin output directory so ffmpeg arguments use relative
# paths only — avoids Windows drive-letter colon issues (D:\... → key:value).
set(TEST_ONNX_MODEL_REL "test_sharpen_model.onnx")   # relative to PLUGIN_DIR
set(TEST_ONNX_MODEL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../tools/${TEST_ONNX_MODEL_REL}")
if(EXISTS "${TEST_ONNX_MODEL_SRC}")
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${TEST_ONNX_MODEL_REL}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${TEST_ONNX_MODEL_SRC}"
            "${CMAKE_CURRENT_BINARY_DIR}/${TEST_ONNX_MODEL_REL}"
        DEPENDS "${TEST_ONNX_MODEL_SRC}"
        COMMENT "Copying test ONNX model to build directory"
    )
    add_custom_target(copy_test_onnx_model ALL
        DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${TEST_ONNX_MODEL_REL}")

    # onnx_infer_test_ffmpeg: blur then sharpen in a single filter_complex graph,
    # outputting both the sharpened result and the blurred intermediate.
    # The first output (sharp) is compared against an unfiltered reference.
    add_verified_test(NAME onnx_infer_test_ffmpeg
        FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -filter_complex oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:blur_plugin>:params=ksize=15,split[blur_out][to_sharp]{{SEMI}}[to_sharp]oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:onnx_infer_plugin>:params=model=${TEST_ONNX_MODEL_REL}&overlap=0[sharp_out] -map [sharp_out] -c:v rawvideo -f nut ${TEST_OUT_DIR}/onnx_sharp.nut -map [blur_out] -c:v rawvideo -f nut ${TEST_OUT_DIR}/onnx_blur.nut"
        OUTPUT_FILES "${TEST_OUT_DIR}/onnx_sharp.nut;${TEST_OUT_DIR}/onnx_blur.nut"
        REF_FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -vf oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:blur_plugin>:params=ksize=5 -c:v rawvideo -f nut ${TEST_OUT_DIR}/onnx_blur_ref.nut"
        REF_OUTPUT_FILE "${TEST_OUT_DIR}/onnx_blur_ref.nut"
        EXPECT_DIFFERENT_FROM_REF
    )
endif()

endif()

# --- CUDA plugin tests ---

if(HAVE_CUDA_PLUGINS)

# cuda_blur_test: CUDA 1:1 blur
add_verified_test(NAME cuda_blur_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -vf format=bgra,hwupload_cuda,oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:cuda_blur_plugin>:params=ksize=15&scale=1.5,hwdownload,format=bgra -c:v rawvideo -f nut ${TEST_OUT_DIR}/cuda_blur.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/cuda_blur.nut"
    REF_FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -vf format=bgra -c:v rawvideo -f nut ${TEST_OUT_DIR}/cuda_blur_ref.nut"
    REF_OUTPUT_FILE "${TEST_OUT_DIR}/cuda_blur_ref.nut"
    EXPECT_DIFFERENT_FROM_REF
)

# cuda_split_test: CUDA 1:3 split (copyTo pass-through, blur, half-res)
add_verified_test(NAME cuda_split_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -filter_complex format=bgra,hwupload_cuda,oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:cuda_split_plugin>:outputs=3[out0][out1][out2]{{SEMI}}[out0]hwdownload,format=bgra[d0]{{SEMI}}[out1]hwdownload,format=bgra[d1]{{SEMI}}[out2]hwdownload,format=bgra[d2] -map [d0] -c:v rawvideo -f nut ${TEST_OUT_DIR}/cuda_split_pass.nut -map [d1] -c:v rawvideo -f nut ${TEST_OUT_DIR}/cuda_split_blur.nut -map [d2] -c:v rawvideo -f nut ${TEST_OUT_DIR}/cuda_split_half.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/cuda_split_pass.nut;${TEST_OUT_DIR}/cuda_split_blur.nut;${TEST_OUT_DIR}/cuda_split_half.nut"
)

# cuda_blend_test: CUDA 2:1 blend
add_verified_test(NAME cuda_blend_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -f lavfi -i color=c=blue:duration=3:size=320x240:rate=30 -filter_complex [0:v]format=bgra,hwupload_cuda[cuda0]{{SEMI}}[1:v]format=bgra,hwupload_cuda[cuda1]{{SEMI}}[cuda0][cuda1]oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:cuda_blend_plugin>:inputs=2:params=alpha=0.5,hwdownload,format=bgra -c:v rawvideo -f nut ${TEST_OUT_DIR}/cuda_blend.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/cuda_blend.nut"
    REF_FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -vf format=bgra -c:v rawvideo -f nut ${TEST_OUT_DIR}/cuda_blend_ref.nut"
    REF_OUTPUT_FILE "${TEST_OUT_DIR}/cuda_blend_ref.nut"
    EXPECT_DIFFERENT_FROM_REF
)

# cuda_transcode_test: simulate full GPU pipeline with NV12 frames and hw encode.
# NOTE: This does NOT use hardware decode. It generates NV12 with lavfi + format filter,
# then uploads to GPU via hwupload_cuda. This simulates the frame format that a real
# hardware decoder (e.g., -hwaccel cuda) would produce, allowing us to test the
# NV12 CUDA processing path and hevc_nvenc encoding without requiring a real input file.
add_test(NAME cuda_transcode_test
    COMMAND "${TEST_FFMPEG_CMD}" -hide_banner -y -f lavfi -i "${LAVFI_SRC}"
        -vf "format=nv12,hwupload_cuda,oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:cuda_blur_plugin>:params=ksize=15&scale=1.5"
        -c:v hevc_nvenc -b:v 2M
        "${TEST_OUT_DIR}/cuda_transcode.mp4"
    WORKING_DIRECTORY ${PLUGIN_DIR}
)

endif()

# ============================================================================
# Valgrind / Sanitizer support
# ============================================================================
#
# Usage examples:
#
#   # Run all tests under valgrind memcheck:
#   ctest --test-dir build -T memcheck \
#       --overwrite MemoryCheckCommand=/usr/bin/valgrind \
#       --overwrite MemoryCheckCommandOptions="--leak-check=full --track-origins=yes --error-exitcode=1"
#
#   # Or configure once with cmake, then use ctest -T memcheck:
#   cmake -B build -DVALGRIND_CMD=/usr/bin/valgrind
#   cd build && ctest -T memcheck --output-on-failure
#
#   # Build with AddressSanitizer (rebuild ffmpeg and plugins with ASan):
#   cmake -B build -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
#                  -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
#                  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" \
#                  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address"
#   cmake --build build && ctest --test-dir build --output-on-failure
#
#   # Build with ThreadSanitizer:
#   cmake -B build -DCMAKE_CXX_FLAGS="-fsanitize=thread" \
#                  -DCMAKE_C_FLAGS="-fsanitize=thread" \
#                  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" \
#                  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=thread"
#
#   # NVIDIA Compute Sanitizer (for CUDA plugins):
#   compute-sanitizer --tool memcheck \
#       ./ffmpeg -hide_banner -y -f lavfi -i "testsrc=duration=3:size=320x240:rate=30" \
#       -vf "format=nv12,hwupload_cuda,oc_plugin=plugin=./libcuda_blur_plugin.so:params=ksize=15" \
#       -c:v hevc_nvenc -b:v 2M test_output/cuda_transcode.mp4
#
#   # Compute Sanitizer race checking:
#   compute-sanitizer --tool racecheck \
#       ./ffmpeg -hide_banner -y -f lavfi -i "testsrc=duration=3:size=320x240:rate=30" \
#       -vf "format=bgra,hwupload_cuda,oc_plugin=plugin=./libcuda_blur_plugin.so:params=ksize=15,hwdownload,format=bgra" \
#       -c:v rawvideo -f nut test_output/cuda_blur.nut
#
# ============================================================================

# CTest memcheck integration (optional)
if(VALGRIND_CMD)
    set(MEMORYCHECK_COMMAND "${VALGRIND_CMD}" CACHE FILEPATH "" FORCE)
    set(MEMORYCHECK_COMMAND_OPTIONS "--leak-check=full --track-origins=yes --error-exitcode=1" CACHE STRING "" FORCE)
    message(STATUS "Valgrind configured: ${VALGRIND_CMD}")
endif()
