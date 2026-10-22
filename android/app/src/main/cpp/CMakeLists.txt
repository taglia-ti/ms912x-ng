cmake_minimum_required(VERSION 3.22.1)
project(ms912x_android_capture C)

add_library(ms912x_capture SHARED
    frame_pipeline.c
    jni_capture.c)

find_library(android_log log)
find_package(Threads REQUIRED)
target_compile_features(ms912x_capture PRIVATE c_std_11)
target_compile_options(ms912x_capture PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ms912x_capture PRIVATE Threads::Threads ${android_log})
