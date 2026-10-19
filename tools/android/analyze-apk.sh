#!/usr/bin/env bash
# Extract local vendor artifacts. Never installs tools or talks to a USB device.
set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
apk=${1:-"$repo_dir/drivers/MSDisplay_Android_v2.0.0.3.apk"}
output_dir=${2:-"$repo_dir/drivers/android-analysis"}
mkdir -p -- "$output_dir"
apk=$(realpath -- "$apk")
output_dir=$(realpath -- "$output_dir")

python3 - "$apk" "$output_dir" <<'PY'
import hashlib
import sys
import zipfile
from pathlib import Path

apk, out = map(Path, sys.argv[1:])
with zipfile.ZipFile(apk) as archive:
    archive.extractall(out / "unpacked")
    (out / "apk-contents.txt").write_text(
        "".join(f"{item.file_size:10} {item.filename}\n" for item in archive.infolist())
    )
paths = [apk, *sorted((out / "unpacked/lib").glob("*/*.so"))]
(out / "SHA256SUMS").write_text("".join(
    f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path}\n" for path in paths
))
PY

for abi_dir in "$output_dir"/unpacked/lib/*; do
    abi=$(basename -- "$abi_dir")
    mkdir -p -- "$output_dir/native/$abi"
    for library in "$abi_dir"/*.so; do
        name=$(basename -- "$library" .so)
        readelf -h -d -Ws "$library" > "$output_dir/native/$abi/$name-elf.txt"
        strings -a -t x "$library" > "$output_dir/native/$abi/$name-strings.txt"
        llvm-objdump -d --demangle "$library" > "$output_dir/native/$abi/$name.asm"
    done
done

if [[ ${3:-} == --skip-java ]]; then
    printf 'Extraction and native disassembly: %s\n' "$output_dir"
    exit 0
fi
if ! command -v jadx >/dev/null; then
    printf 'JADX is required for Java decompilation; native artifacts are in %s\n' "$output_dir" >&2
    exit 1
fi
export JADX_CONFIG_DIR="$output_dir/jadx-work/config"
export JADX_CACHE_DIR="$output_dir/jadx-work/cache"
export JADX_TMP_DIR="$output_dir/jadx-work/tmp"
jadx --version > "$output_dir/jadx-version.txt"
# JADX can return nonzero after writing sources containing decompilation errors.
# Preserve that status and its log for inspection instead of claiming success.
if jadx --config none --show-bad-code -d "$output_dir/jadx" "$apk" > "$output_dir/jadx.log" 2>&1; then
    :
else
    jadx_status=$?
    printf 'JADX exited with status %s; inspect generated sources and %s/jadx.log\n' \
        "$jadx_status" "$output_dir" >&2
    exit "$jadx_status"
fi
printf 'Java, resources and native artifacts: %s\n' "$output_dir"
