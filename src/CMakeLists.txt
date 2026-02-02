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

function(add_ffmpeg_test TEST_NAME INPUT_COMMAND OUTPUT_FILE)
    add_test(NAME ${TEST_NAME}
            COMMAND ${FFMPEG_CMD} ${INPUT_COMMAND} -y ${OUTPUT_FILE})
endfunction()

list(APPEND FFMPEG_CMD -hide_banner -y)
set(BLUR_TEST_CMD ${FFMPEG_CMD} -f lavfi
    -i testsrc=duration=3:size=640x480:rate=30
    -vf oc_plugin=plugin=$<TARGET_FILE:blur_plugin>:params=ksize=15
    "${CMAKE_BINARY_DIR}/test_blur.mp4")
add_test(NAME blur_plugin_test COMMAND ${BLUR_TEST_CMD})

set(AVGFRAMES_TEST_CMD ${FFMPEG_CMD} -f lavfi
    -i testsrc=duration=3:size=640x480:rate=30
    -vf oc_plugin=plugin=$<TARGET_FILE:avgframes_plugin>:params=frames=5
    "${CMAKE_BINARY_DIR}/test_avgframes.mp4")
add_test(NAME avgframes_plugin_test COMMAND ${AVGFRAMES_TEST_CMD})

set(SPLIT_TEST_CMD ${FFMPEG_CMD} -f lavfi
        -i testsrc=duration=3:size=640x480:rate=30
        -filter_complex oc_plugin=plugin=$<TARGET_FILE:split_plugin>:outputs=3:params=outputs=3[out0][out1][out2]
        -map [out0] ${CMAKE_BINARY_DIR}/test_split_passthrough.mp4
        -map [out1] ${CMAKE_BINARY_DIR}/test_split_gray.mp4
        -map [out2] ${CMAKE_BINARY_DIR}/test_split_edges.mp4)
add_test(NAME split_plugin_test COMMAND ${SPLIT_TEST_CMD})

set(BLEND_TEST_CMD ${FFMPEG_CMD} -f lavfi
        -i testsrc=duration=3:size=640x480:rate=30
        -f lavfi -i color=c=blue:duration=3:size=640x480:rate=30
        -filter_complex [0:v][1:v]oc_plugin=plugin=$<TARGET_FILE:blend_plugin>:inputs=2:params=alpha=0.5
        ${CMAKE_BINARY_DIR}/test_blend.mp4)
add_test(NAME blend_plugin_test COMMAND ${BLEND_TEST_CMD})

set(detect_TEST_CMD ${FFMPEG_CMD} -f lavfi
        -i smptebars=duration=3:size=640x480:rate=30
        -vf oc_plugin=plugin=$<TARGET_FILE:detect_plugin>:params=delay=2,showinfo
        -f null -)
add_test(NAME detect_plugin_immediate_test COMMAND ${detect_TEST_CMD})
