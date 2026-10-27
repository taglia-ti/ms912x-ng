#!/usr/bin/env bash
set -euo pipefail
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_name=adbHostTests
case "${SANITIZE:-0}" in
    0) ;;
    1) build_name=adbHostAsan ;;
    undefined) build_name=adbHostUbsan ;;
    *) printf '%s\n' 'SANITIZE must be 0, 1, or undefined.' >&2; exit 2 ;;
esac
build_dir=${ADB_TEST_BUILD_DIR:-"$repo_root/android/build/$build_name"}
cmake_args=(-DCMAKE_BUILD_TYPE=RelWithDebInfo)
if [[ -n "${MBEDTLS_SOURCE_DIR:-}" ]]; then
    cmake_args+=("-DFETCHCONTENT_SOURCE_DIR_MBEDTLS=$MBEDTLS_SOURCE_DIR")
fi
if [[ "${SANITIZE:-0}" == 1 ]]; then
    cmake_args+=(-DCMAKE_C_FLAGS=-fsanitize=address,undefined\ -fno-omit-frame-pointer
                -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -DADB_TEST_JNI=OFF)
elif [[ "${SANITIZE:-0}" == undefined ]]; then
    cmake_args+=(-DCMAKE_C_FLAGS=-fsanitize=undefined\ -fno-sanitize-recover=all\ -fno-omit-frame-pointer
                -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=undefined -DADB_TEST_JNI=OFF)
fi
cmake -S "$repo_root/android/adb-c" -B "$build_dir" "${cmake_args[@]}"
cmake --build "$build_dir" --parallel "${JOBS:-4}"
ctest --test-dir "$build_dir" --output-on-failure
