cmake_minimum_required(VERSION 3.22.1)
project(ms912x_android_capture C)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../shared)

add_library(ms912x_capture SHARED
    ../../../../../shared/ms912x_rgbx_convert.c
    ../../../../../shared/ms912x_rgbx_sse2.c
    ../../../../../shared/ms912x_rgbx_neon.c
    ../../../../../shared/ms912x_frame_damage.c
    ../../../../../shared/ms912x_scan_rects_core.c
    ../../../../../shared/ms912x_scan_backend.c
    ../../../../../shared/ms912x_scan_core.c
    ../../../../../shared/ms912x_mode3_core.c
    ../../../../../shared/ms912x_modes_core.c
    ../../../../../ms912x_mode3.c
    ../../../../../ms912x_batch.c
    frame_pipeline.c
    jni_capture.c
    jni_adapter.c)

# Keep the pixel workers optimized even in an installable debug/test APK.
set_source_files_properties(
    ../../../../../shared/ms912x_rgbx_convert.c
    ../../../../../shared/ms912x_rgbx_sse2.c
    ../../../../../shared/ms912x_rgbx_neon.c
    PROPERTIES COMPILE_OPTIONS "-O3")
set_source_files_properties(
    ../../../../../shared/ms912x_scan_rects_core.c
    ../../../../../shared/ms912x_scan_backend.c
    ../../../../../shared/ms912x_scan_core.c
    ../../../../../shared/ms912x_mode3_core.c
    ../../../../../shared/ms912x_frame_damage.c
    frame_pipeline.c
    PROPERTIES COMPILE_OPTIONS "-O2")

find_library(android_log log)
find_package(Threads REQUIRED)
target_compile_features(ms912x_capture PRIVATE c_std_11)
target_compile_options(ms912x_capture PRIVATE -Wall -Wextra -Werror -fvisibility=hidden)
target_link_libraries(ms912x_capture PRIVATE Threads::Threads ${android_log})
