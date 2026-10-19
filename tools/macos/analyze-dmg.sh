#!/usr/bin/env bash
# Static extraction only: never executes the vendor program or package scripts.
set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
dmg=${1:-"$repo_dir/drivers/MSDisplay_MacOS_v1.0.2b_20201125A.dmg"}
output_dir=${2:-"$repo_dir/drivers/macos-analysis"}
mkdir -p -- "$output_dir"
dmg=$(realpath -- "$dmg")
output_dir=$(realpath -- "$output_dir")
mkdir -p -- "$output_dir/native"

7z l "$dmg" > "$output_dir/dmg-contents.txt"
7z x -y "-o$output_dir/dmg" "$dmg" > "$output_dir/extract-dmg.log"
7z x -y "-o$output_dir/package" "$output_dir/dmg/MSDisplay.pkg" > "$output_dir/extract-package.log"
python3 - "$output_dir" <<'PY'
import gzip
import sys
from pathlib import Path

out = Path(sys.argv[1])
payload = out / "package/MSDisplay_App.pkg/Payload"
(out / "Payload.cpio").write_bytes(gzip.decompress(payload.read_bytes()))
PY
7z l "$output_dir/Payload.cpio" > "$output_dir/payload-contents.txt"
7z x -y "-o$output_dir/unpacked" "$output_dir/Payload.cpio" > "$output_dir/extract-payload.log"

binary="$output_dir/unpacked/usr/local/bin/screen_capture_msdisplay"
sha256sum "$dmg" "$binary" > "$output_dir/SHA256SUMS"
file "$binary" > "$output_dir/native/file.txt"
llvm-objdump --macho --private-headers "$binary" > "$output_dir/native/headers.txt"
llvm-objdump --macho --dylibs-used "$binary" > "$output_dir/native/libraries.txt"
llvm-objdump --macho --indirect-symbols "$binary" > "$output_dir/native/indirect-symbols.txt"
llvm-objdump -d --demangle --x86-asm-syntax=intel "$binary" > "$output_dir/native/screen_capture_msdisplay.asm"
llvm-nm --defined-only --demangle "$binary" > "$output_dir/native/symbols.txt"
strings -a -t x "$binary" > "$output_dir/native/strings.txt"
python3 "$repo_dir/tools/macos/extract-evidence.py" "$binary" "$output_dir/native"
printf 'Static macOS artifacts: %s\n' "$output_dir"
