#!/usr/bin/env bash
set -euo pipefail

# Run the production C workers under ASan/UBSan on an explicit Android device.
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
sdk=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [[ -z "$sdk" || $# != 1 ]]; then
    printf '%s\n' 'Usage: ANDROID_HOME=/path/to/sdk test_native_android.sh DEVICE_SERIAL' >&2
    exit 2
fi
adb=("$sdk/platform-tools/adb" -s "$1")
abi=$("${adb[@]}" shell getprop ro.product.cpu.abi | tr -d '\r')
case "$abi" in
    arm64-v8a) arch=aarch64 ;;
    x86_64) arch=x86_64 ;;
    *) printf 'Unsupported device ABI: %s\n' "$abi" >&2; exit 2 ;;
esac
ndk=${ANDROID_NDK_HOME:-$sdk/ndk/30.0.16248370}
toolchain=$ndk/toolchains/llvm/prebuilt/linux-x86_64
compiler=$toolchain/bin/$arch-linux-android30-clang
runtimes=("$toolchain"/lib/clang/*/lib/linux/libclang_rt.asan-"$arch"-android.so)
[[ -x "$compiler" && ${#runtimes[@]} == 1 && -f "${runtimes[0]}" ]]
temporary_dir=$(mktemp -d)
guest_dir=/data/local/tmp/ms912x-native-${temporary_dir##*/}
cleanup() {
    "${adb[@]}" shell rm -rf "$guest_dir" >/dev/null 2>&1 || true
    rm -rf "$temporary_dir"
}
trap cleanup EXIT
flags=(-std=c11 -O2 -g -Wall -Wextra -Werror -pthread
       -fsanitize=address,undefined -fno-omit-frame-pointer
       -I"$repo_root/shared" -I"$repo_root/android/app/src/main/cpp")
conversion=("$repo_root/shared/ms912x_rgbx_convert.c"
            "$repo_root/shared/ms912x_rgbx_sse2.c"
            "$repo_root/shared/ms912x_rgbx_neon.c")
pipeline=("${conversion[@]}" "$repo_root/ms912x_mode3.c" "$repo_root/ms912x_batch.c" "$repo_root/shared/ms912x_frame_damage.c"
          "$repo_root/shared/ms912x_scan_core.c" "$repo_root/shared/ms912x_scan_rects_core.c" "$repo_root/shared/ms912x_mode3_core.c"
          "$repo_root/android/app/src/main/cpp/frame_pipeline.c")
"$compiler" "${flags[@]}" "$repo_root/tools/android/rgbx_conversion_test.c" \
    "${conversion[@]}" -o "$temporary_dir/rgbx"
for fixture in frame_pipeline frame_submission frame_boundary frame_fault frame_rects; do
    fixture_flags=()
    if [[ "$fixture" == frame_fault ]]; then
        fixture_flags=(-Wl,--wrap=malloc,--wrap=calloc,--wrap=pthread_mutex_init)
    fi
    "$compiler" "${flags[@]}" "${fixture_flags[@]}" "$repo_root/tools/android/${fixture}_test.c" \
        "${pipeline[@]}" -o "$temporary_dir/$fixture"
done
"${adb[@]}" shell mkdir "$guest_dir"
"${adb[@]}" push "${runtimes[0]}" "$temporary_dir/rgbx" \
    "$temporary_dir/frame_pipeline" "$temporary_dir/frame_submission" "$temporary_dir/frame_boundary" "$temporary_dir/frame_fault" "$temporary_dir/frame_rects" "$guest_dir/"
for fixture in rgbx frame_pipeline frame_submission frame_boundary frame_fault frame_rects; do
    "${adb[@]}" shell "cd $guest_dir && LD_LIBRARY_PATH=. ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./$fixture"
done
