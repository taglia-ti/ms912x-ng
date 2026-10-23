#!/usr/bin/env bash
set -euo pipefail
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
temporary_dir=$(mktemp -d)
trap 'rm -rf "$temporary_dir"' EXIT
sources=(
    "$repo_root/tools/test_rgbx.c"
    "$repo_root/shared/ms912x_rgbx_convert.c"
    "$repo_root/shared/ms912x_rgbx_sse2.c"
    "$repo_root/shared/ms912x_rgbx_neon.c"
)
flags=(-std=c11 -O3 -g -Wall -Wextra -Werror -I"$repo_root/shared")
if [[ "${SANITIZE:-0}" == 1 ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
elif [[ "${SANITIZE:-0}" == undefined ]]; then
    flags+=(-fsanitize=undefined -fno-omit-frame-pointer)
fi
"${CC:-cc}" "${flags[@]}" "${sources[@]}" -o "$temporary_dir/rgbx-test"
"$temporary_dir/rgbx-test"
if [[ "${1:-}" == --bench ]]; then "$temporary_dir/rgbx-test" --bench; fi
if command -v aarch64-linux-gnu-gcc >/dev/null && command -v qemu-aarch64 >/dev/null; then
    aarch64-linux-gnu-gcc -std=c11 -O3 -Wall -Wextra -Werror -static \
        -I"$repo_root/shared" "${sources[@]}" -o "$temporary_dir/rgbx-test-arm64"
    qemu-aarch64 "$temporary_dir/rgbx-test-arm64"
else
    printf '%s\n' "ARM64 NEON execution skipped: cross compiler/QEMU not available"
fi
