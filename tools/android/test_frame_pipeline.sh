#!/usr/bin/env bash
set -euo pipefail
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary_dir=$(mktemp -d)
trap 'rm -rf "$temporary_dir"' EXIT
sources=(
    "$repo_root/shared/ms912x_scan_core.c" "$repo_root/shared/ms912x_scan_rects_core.c"
    "$repo_root/shared/ms912x_mode3_core.c"
    "$repo_root/shared/ms912x_rgbx_convert.c"
    "$repo_root/shared/ms912x_rgbx_sse2.c"
    "$repo_root/shared/ms912x_rgbx_neon.c"
    "$repo_root/ms912x_mode3.c" "$repo_root/ms912x_batch.c" "$repo_root/shared/ms912x_frame_damage.c"
    "$repo_root/android/app/src/main/cpp/frame_pipeline.c"
)
flags=(-std=c11 -O2 -g -Wall -Wextra -Werror -pthread)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
elif [[ "${SANITIZE:-0}" == undefined ]]; then
    flags+=(-fsanitize=undefined -fno-omit-frame-pointer)
fi
for fixture in frame_pipeline frame_submission frame_boundary frame_fault frame_rects; do
    fixture_flags=()
    if [[ "$fixture" == frame_fault ]]; then
        fixture_flags=(-Wl,--wrap=malloc,--wrap=calloc,--wrap=pthread_mutex_init)
    fi
    "${CC:-cc}" "${flags[@]}" "${fixture_flags[@]}" \
        -I"$repo_root/android/app/src/main/cpp" -I"$repo_root/shared" \
        "$repo_root/tools/android/${fixture}_test.c" "${sources[@]}" \
        -o "$temporary_dir/$fixture"
    "$temporary_dir/$fixture"
    if command -v aarch64-linux-gnu-gcc >/dev/null && command -v qemu-aarch64 >/dev/null; then
        aarch64-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra -Werror -pthread -static \
            "${fixture_flags[@]}" -I"$repo_root/android/app/src/main/cpp" -I"$repo_root/shared" \
            "$repo_root/tools/android/${fixture}_test.c" "${sources[@]}" \
            -o "$temporary_dir/$fixture-arm64"
        qemu-aarch64 "$temporary_dir/$fixture-arm64"
    fi
done
javac -d "$temporary_dir" \
    "$repo_root/android/app/src/main/java/io/github/ms912x/desktop/FrameSubmission.java" \
    "$repo_root/tools/android/FrameSubmissionTest.java"
java -cp "$temporary_dir" io.github.ms912x.desktop.FrameSubmissionTest
