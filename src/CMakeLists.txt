macro(add_plugin name)
    add_library(${name}_plugin SHARED ${name}_plugin.cpp)
    target_link_libraries(${name}_plugin PRIVATE quink_oc_plugin ${OpenCV_LIBS})
    target_include_directories(${name}_plugin PRIVATE ${OpenCV_INCLUDE_DIRS})
    set_target_properties(${name}_plugin PROPERTIES
        PREFIX "lib"
        OUTPUT_NAME "${name}_plugin"
    )
    install(TARGETS ${name}_plugin LIBRARY DESTINATION lib)
endmacro()

add_plugin(blur)
add_plugin(blend)
add_plugin(avgframes)
add_plugin(split)
add_plugin(detect)

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
    set(PLUGIN_PATH_PREFIX "")
else()
    set(TEST_FFMPEG_CMD "${FFMPEG_CMD}")
    set(PLUGIN_PATH_PREFIX "./")
endif()

# Output directory for test artifacts (nut files for visual inspection, mp4 for transcode)
set(TEST_OUT_DIR "${CMAKE_BINARY_DIR}/test_output")
file(MAKE_DIRECTORY "${TEST_OUT_DIR}")

# Common lavfi test source
set(LAVFI_SRC "testsrc=duration=1:size=640x480:rate=30")

# Path to the verification script
set(VERIFY_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/verify_output.cmake")

# ----------------------------------------------------------------------------
# Test macros
#
# Each test runs ffmpeg directly (no bash/shell dependency, works on Windows).
# Tests output .nut files with rawvideo codec (zero encoding overhead,
# playable with ffplay, supports any pixel format including BGR24/BGRA).
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

    set(_cmd ${CMAKE_COMMAND} -P "${VERIFY_SCRIPT}"
        -DFFMPEG_CMD=${TEST_FFMPEG_CMD}
        -DWORK_DIR=${PLUGIN_DIR}
        -DCHECK_FRAMEMD5=TRUE
    )

    add_test(NAME ${VT_NAME}
        COMMAND ${CMAKE_COMMAND}
            -DFFMPEG_CMD=${TEST_FFMPEG_CMD}
            -DFFMPEG_ARGS=${VT_FFMPEG_ARGS}
            -DOUTPUT_FILES=${VT_OUTPUT_FILES}
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
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -vf oc_plugin=plugin=${PLUGIN_PATH_PREFIX}libblur_plugin.so:params=ksize=15&scale=0.5 -c:v rawvideo -f nut ${TEST_OUT_DIR}/blur.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/blur.nut"
    REF_FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -c:v rawvideo -f nut ${TEST_OUT_DIR}/blur_ref.nut"
    REF_OUTPUT_FILE "${TEST_OUT_DIR}/blur_ref.nut"
    EXPECT_DIFFERENT_FROM_REF
)

# avgframes_test: buffered averaging
add_verified_test(NAME avgframes_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -vf oc_plugin=plugin=${PLUGIN_PATH_PREFIX}libavgframes_plugin.so:params=frames=5 -c:v rawvideo -f nut ${TEST_OUT_DIR}/avgframes.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/avgframes.nut"
)

# detect_test: detection with delay
add_verified_test(NAME detect_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i smptebars=duration=1:size=640x480:rate=30 -vf oc_plugin=plugin=${PLUGIN_PATH_PREFIX}libdetect_plugin.so:params=delay=2 -c:v rawvideo -f nut ${TEST_OUT_DIR}/detect.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/detect.nut"
)

# split_test: 1:3 split (pass-through, grayscale, edges)
add_verified_test(NAME split_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -filter_complex oc_plugin=plugin=${PLUGIN_PATH_PREFIX}libsplit_plugin.so:outputs=3:params=outputs=3[out0][out1][out2] -map [out0] -c:v rawvideo -f nut ${TEST_OUT_DIR}/split_pass.nut -map [out1] -c:v rawvideo -f nut ${TEST_OUT_DIR}/split_gray.nut -map [out2] -c:v rawvideo -f nut ${TEST_OUT_DIR}/split_edges.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/split_pass.nut;${TEST_OUT_DIR}/split_gray.nut;${TEST_OUT_DIR}/split_edges.nut"
)

# blend_test: 2:1 blend
add_verified_test(NAME blend_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -f lavfi -i color=c=blue:duration=1:size=640x480:rate=30 -filter_complex [0:v][1:v]oc_plugin=plugin=${PLUGIN_PATH_PREFIX}libblend_plugin.so:inputs=2:params=alpha=0.5 -c:v rawvideo -f nut ${TEST_OUT_DIR}/blend.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/blend.nut"
    REF_FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -c:v rawvideo -f nut ${TEST_OUT_DIR}/blend_ref.nut"
    REF_OUTPUT_FILE "${TEST_OUT_DIR}/blend_ref.nut"
    EXPECT_DIFFERENT_FROM_REF
)

# --- CUDA plugin tests ---

if(HAVE_CUDA_PLUGINS)

# cuda_blur_test: CUDA 1:1 blur
add_verified_test(NAME cuda_blur_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -vf format=bgra,hwupload_cuda,oc_plugin=plugin=${PLUGIN_PATH_PREFIX}libcuda_blur_plugin.so:params=ksize=15&scale=1.5,hwdownload,format=bgra -c:v rawvideo -f nut ${TEST_OUT_DIR}/cuda_blur.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/cuda_blur.nut"
    REF_FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -vf format=bgra -c:v rawvideo -f nut ${TEST_OUT_DIR}/cuda_blur_ref.nut"
    REF_OUTPUT_FILE "${TEST_OUT_DIR}/cuda_blur_ref.nut"
    EXPECT_DIFFERENT_FROM_REF
)

# cuda_split_test: CUDA 1:3 split (copyTo pass-through, blur, half-res)
add_verified_test(NAME cuda_split_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -filter_complex format=bgra,hwupload_cuda,oc_plugin=plugin=${PLUGIN_PATH_PREFIX}libcuda_split_plugin.so:outputs=3[out0][out1][out2]{{SEMI}}[out0]hwdownload,format=bgra[d0]{{SEMI}}[out1]hwdownload,format=bgra[d1]{{SEMI}}[out2]hwdownload,format=bgra[d2] -map [d0] -c:v rawvideo -f nut ${TEST_OUT_DIR}/cuda_split_pass.nut -map [d1] -c:v rawvideo -f nut ${TEST_OUT_DIR}/cuda_split_blur.nut -map [d2] -c:v rawvideo -f nut ${TEST_OUT_DIR}/cuda_split_half.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/cuda_split_pass.nut;${TEST_OUT_DIR}/cuda_split_blur.nut;${TEST_OUT_DIR}/cuda_split_half.nut"
)

# cuda_blend_test: CUDA 2:1 blend
add_verified_test(NAME cuda_blend_test
    FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -f lavfi -i color=c=blue:duration=1:size=640x480:rate=30 -filter_complex [0:v]format=bgra,hwupload_cuda[cuda0]{{SEMI}}[1:v]format=bgra,hwupload_cuda[cuda1]{{SEMI}}[cuda0][cuda1]oc_plugin=plugin=${PLUGIN_PATH_PREFIX}libcuda_blend_plugin.so:inputs=2:params=alpha=0.5,hwdownload,format=bgra -c:v rawvideo -f nut ${TEST_OUT_DIR}/cuda_blend.nut"
    OUTPUT_FILES "${TEST_OUT_DIR}/cuda_blend.nut"
    REF_FFMPEG_ARGS "-hide_banner -y -f lavfi -i ${LAVFI_SRC} -vf format=bgra -c:v rawvideo -f nut ${TEST_OUT_DIR}/cuda_blend_ref.nut"
    REF_OUTPUT_FILE "${TEST_OUT_DIR}/cuda_blend_ref.nut"
    EXPECT_DIFFERENT_FROM_REF
)

# cuda_transcode_test: full hw decode -> plugin -> hw encode pipeline
# This test specifically tests the complete CUDA pipeline with video encoding
add_test(NAME cuda_transcode_test
    COMMAND "${TEST_FFMPEG_CMD}" -hide_banner -y -f lavfi -i "${LAVFI_SRC}"
        -vf "format=nv12,hwupload_cuda,oc_plugin=plugin=${PLUGIN_PATH_PREFIX}libcuda_blur_plugin.so:params=ksize=15&scale=1.5"
        -c:v hevc_nvenc -b:v 2M
        "${TEST_OUT_DIR}/cuda_transcode.mp4"
    WORKING_DIRECTORY ${PLUGIN_DIR}
)

endif()
