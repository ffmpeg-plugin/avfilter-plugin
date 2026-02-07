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

# Plugin directory (where DLLs are built)
set(PLUGIN_DIR "$<TARGET_FILE_DIR:blur_plugin>")

# Common test video source
set(TEST_INPUT_LAVFI "testsrc=duration=3:size=640x480:rate=30")

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
    
    # Use local ffmpeg copy
    set(TEST_FFMPEG_CMD "${PLUGIN_DIR}/${FFMPEG_NAME}")
    set(PLUGIN_PATH_PREFIX "")
else()
    set(TEST_FFMPEG_CMD "${FFMPEG_CMD}")
    set(PLUGIN_PATH_PREFIX "./")
endif()

# Macro for simple single-input plugin tests
macro(add_simple_plugin_test TEST_NAME PLUGIN_TARGET PARAMS OUTPUT_NAME)
    add_test(NAME ${TEST_NAME}
        COMMAND ${TEST_FFMPEG_CMD} -hide_banner -y
            -f lavfi -i ${TEST_INPUT_LAVFI}
            -vf "oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:${PLUGIN_TARGET}>:params=${PARAMS}"
            "${CMAKE_BINARY_DIR}/${OUTPUT_NAME}.mp4"
        WORKING_DIRECTORY ${PLUGIN_DIR}
    )
endmacro()

# Macro for null-output plugin tests
macro(add_null_output_test TEST_NAME PLUGIN_TARGET PARAMS INPUT_SRC EXTRA_VF)
    if("${EXTRA_VF}" STREQUAL "")
        set(_VF_ARG "oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:${PLUGIN_TARGET}>:params=${PARAMS}")
    else()
        set(_VF_ARG "oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:${PLUGIN_TARGET}>:params=${PARAMS},${EXTRA_VF}")
    endif()
    add_test(NAME ${TEST_NAME}
        COMMAND ${TEST_FFMPEG_CMD} -hide_banner -y
            -f lavfi -i "${INPUT_SRC}"
            -vf "${_VF_ARG}"
            -f null -
        WORKING_DIRECTORY ${PLUGIN_DIR}
    )
endmacro()

# ----------------------------------------------------------------------------
# Simple plugin tests
# ----------------------------------------------------------------------------
add_simple_plugin_test(blur_plugin_test blur_plugin "ksize=15&scale=0.5" test_blur)
add_simple_plugin_test(avgframes_plugin_test avgframes_plugin "frames=5" test_avgframes)

# ----------------------------------------------------------------------------
# Detection plugin test (null output)
# ----------------------------------------------------------------------------
add_null_output_test(detect_plugin_immediate_test detect_plugin "delay=2" 
    "smptebars=duration=3:size=640x480:rate=30" "showinfo")

# ----------------------------------------------------------------------------
# Complex tests (multi-input/output)
# ----------------------------------------------------------------------------

# Split plugin test (1 input -> 3 outputs)
add_test(NAME split_plugin_test
    COMMAND ${TEST_FFMPEG_CMD} -hide_banner -y
        -f lavfi -i ${TEST_INPUT_LAVFI}
        -filter_complex "oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:split_plugin>:outputs=3:params=outputs=3[out0][out1][out2]"
        -map "[out0]" "${CMAKE_BINARY_DIR}/test_split_passthrough.mp4"
        -map "[out1]" "${CMAKE_BINARY_DIR}/test_split_gray.mp4"
        -map "[out2]" "${CMAKE_BINARY_DIR}/test_split_edges.mp4"
    WORKING_DIRECTORY ${PLUGIN_DIR}
)

# Blend plugin test (2 inputs -> 1 output)
add_test(NAME blend_plugin_test
    COMMAND ${TEST_FFMPEG_CMD} -hide_banner -y
        -f lavfi -i ${TEST_INPUT_LAVFI}
        -f lavfi -i "color=c=blue:duration=3:size=640x480:rate=30"
        -filter_complex "[0:v][1:v]oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:blend_plugin>:inputs=2:params=alpha=0.5"
        "${CMAKE_BINARY_DIR}/test_blend.mp4"
    WORKING_DIRECTORY ${PLUGIN_DIR}
)

# ----------------------------------------------------------------------------
# CUDA plugin tests (if CUDA plugins are available)
# ----------------------------------------------------------------------------
if(HAVE_CUDA_PLUGINS)
    add_test(NAME cuda_blur_plugin_test
            COMMAND ${TEST_FFMPEG_CMD} -hide_banner -y -f lavfi -i ${TEST_INPUT_LAVFI}
            -vf "format=bgra,hwupload_cuda,oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:cuda_blur_plugin>:params=ksize=15&scale=1.5,hwdownload,format=bgra"
            -pix_fmt yuv420p "${CMAKE_BINARY_DIR}/test_cuda_blur.mp4"
            WORKING_DIRECTORY ${PLUGIN_DIR}
    )
    add_test(NAME cuda_transcode_test
            COMMAND
            ${TEST_FFMPEG_CMD} -hide_banner -y
            -hwaccel cuda -hwaccel_output_format cuda -i "${CMAKE_BINARY_DIR}/test_cuda_blur.mp4"
            -vf "oc_plugin=plugin=${PLUGIN_PATH_PREFIX}$<TARGET_FILE_NAME:cuda_blur_plugin>:params=scale=1.5"
            -c:v hevc_nvenc -b:v 2M
            "${CMAKE_BINARY_DIR}/test_cuda_trans.mp4"
            WORKING_DIRECTORY ${PLUGIN_DIR}
    )
    set_tests_properties(cuda_transcode_test PROPERTIES DEPENDS cuda_blur_plugin_test)
endif()
