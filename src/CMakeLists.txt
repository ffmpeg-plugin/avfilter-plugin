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
