cmake_minimum_required(VERSION 3.22.1)
project(ms912x_adb C)
include(FetchContent)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_USER_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/mbedtls_user_config.h" CACHE STRING "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# The release archive includes generated sources and the framework submodule.
FetchContent_Declare(mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
    URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6)
FetchContent_MakeAvailable(mbedtls)
foreach(crypto_target mbedtls mbedx509 mbedcrypto)
    target_compile_options(${crypto_target} PRIVATE -O2 -fvisibility=hidden)
endforeach()
find_package(Threads REQUIRED)
add_library(ms912x_adb_core STATIC
    adb_crypto.c adb_spake2.c adb_operation.c adb_validation.c
    adb_io.c adb_pairing.c adb_protocol.c adb_handshake.c adb_stream.c
    adb_shell.c adb_sync.c adb_helper.c)
target_compile_features(ms912x_adb_core PUBLIC c_std_11)
target_compile_definitions(ms912x_adb_core PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(ms912x_adb_core PRIVATE -O2 -Wall -Wextra -Werror -fvisibility=hidden)
target_include_directories(ms912x_adb_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ms912x_adb_core PUBLIC mbedtls mbedx509 mbedcrypto Threads::Threads)
if(ANDROID)
    add_library(ms912x_adb_bridge SHARED adb_jni.c adb_jni_helper.c adb_jni_store.c adb_jni_util.c)
    target_compile_options(ms912x_adb_bridge PRIVATE -O2 -Wall -Wextra -Werror -fvisibility=hidden)
    target_link_options(ms912x_adb_bridge PRIVATE -Wl,--exclude-libs,ALL)
    target_link_libraries(ms912x_adb_bridge PRIVATE ms912x_adb_core)
else()
    enable_testing()
    add_executable(adb_test ../../tools/android/adb_test.c)
    target_compile_options(adb_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_link_libraries(adb_test PRIVATE ms912x_adb_core)
    add_test(NAME adb COMMAND adb_test)
    add_executable(adb_protocol_test ../../tools/android/adb_protocol_test.c)
    target_compile_options(adb_protocol_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_link_libraries(adb_protocol_test PRIVATE ms912x_adb_core)
    add_test(NAME adb_protocol COMMAND adb_protocol_test)
    add_executable(adb_edge_test ../../tools/android/adb_edge_test.c)
    target_compile_options(adb_edge_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_link_libraries(adb_edge_test PRIVATE ms912x_adb_core)
    add_test(NAME adb_edges COMMAND adb_edge_test)
    set_tests_properties(adb adb_protocol adb_edges PROPERTIES TIMEOUT 30)
    option(ADB_TEST_JNI "Exercise JNI on the host JVM" ON)
    if(ADB_TEST_JNI)
        find_package(JNI REQUIRED)
        find_package(Java REQUIRED COMPONENTS Development)
        add_library(ms912x_adb_bridge SHARED adb_jni.c adb_jni_helper.c adb_jni_store.c adb_jni_util.c)
        target_include_directories(ms912x_adb_bridge PRIVATE ${JNI_INCLUDE_DIRS})
        target_compile_options(ms912x_adb_bridge PRIVATE -Wall -Wextra -Werror -fvisibility=hidden)
        target_link_libraries(ms912x_adb_bridge PRIVATE ms912x_adb_core)
        set(java_package ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/java/io/github/taglia_ti/ms912x_ng/desktop)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/java/compiled
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/java
            COMMAND ${Java_JAVAC_EXECUTABLE} -d ${CMAKE_CURRENT_BINARY_DIR}/java
                ${java_package}/NativeAdbBridge.java ${java_package}/NativeAdbCall.java ${java_package}/AdbCredentials.java
                ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/android/AdbBridgeTest.java
            COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/java/compiled
            DEPENDS ${java_package}/NativeAdbBridge.java ${java_package}/NativeAdbCall.java ${java_package}/AdbCredentials.java
                ../../tools/android/AdbBridgeTest.java)
        add_custom_target(adb_java_test ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/java/compiled)
        add_test(NAME adb_jni COMMAND ${Java_JAVA_EXECUTABLE} -Xcheck:jni
            -Djava.library.path=$<TARGET_FILE_DIR:ms912x_adb_bridge>
            -cp ${CMAKE_CURRENT_BINARY_DIR}/java io.github.taglia_ti.ms912x_ng.desktop.AdbBridgeTest)
        set_tests_properties(adb_jni PROPERTIES TIMEOUT 30)
    endif()
endif()
