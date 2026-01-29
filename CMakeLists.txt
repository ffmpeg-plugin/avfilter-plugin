cmake_minimum_required(VERSION 3.16)
project(ffmpeg_oc_plugins VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_PLUGINS "Build example plugins" ON)

# Header-only interface library
add_library(quink_oc_plugin INTERFACE)
target_include_directories(quink_oc_plugin INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Install header
install(FILES include/quink_oc_plugin.h DESTINATION include)

if(BUILD_PLUGINS)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc)
    add_subdirectory(src)
endif()
