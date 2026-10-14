# Synthetic benchmark of QtAudioOutput (README.md). Built on its own in the development container:
#   cmake -S tools/audio-bench -B /build/audio-bench -G Ninja -DCMAKE_PREFIX_PATH=/opt/aasdk && cmake --build /build/audio-bench
cmake_minimum_required(VERSION 3.16)
project(audio_bench CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
find_package(Qt6 REQUIRED COMPONENTS Core Multimedia)
find_package(aasdk CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS log)
set(openhu ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(audio_bench audio_bench.cpp
    ${openhu}/src/autoapp/Projection/QtAudioOutput.cpp
    ${openhu}/include/f1x/openauto/autoapp/Projection/QtAudioOutput.hpp)
target_include_directories(audio_bench PRIVATE ${openhu}/include)
target_compile_definitions(audio_bench PRIVATE BOOST_LOG_DYN_LINK)
target_link_libraries(audio_bench Qt6::Core Qt6::Multimedia AASDK::aasdk Boost::log)
