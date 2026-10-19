#!/usr/bin/env bash
# Static extraction only: never executes the installer, DLLs or kernel drivers.
set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
installer=${1:-"$repo_dir/drivers/MSDisplay_MultiDev_v1.0.0.18.0.exe"}
output_dir=${2:-"$repo_dir/drivers/windows-analysis"}
mkdir -p -- "$output_dir"
installer=$(realpath -- "$installer")
output_dir=$(realpath -- "$output_dir")

innoextract --version > "$output_dir/innoextract-version.txt"
innoextract --list "$installer" > "$output_dir/installer-contents.txt"
# Both executables have the same destination name. Keep every variant.
innoextract --extract --collisions rename --output-dir "$output_dir/unpacked" \
    "$installer" > "$output_dir/extraction.log" 2>&1
sha256sum "$installer" > "$output_dir/SHA256SUMS"
python3 "$repo_dir/tools/windows/pe-evidence.py" \
    "$output_dir/unpacked" "$output_dir/pe"
python3 - "$repo_dir" "$installer" "$output_dir" <<'PY'
import runpy
import sys
from pathlib import Path

repo, installer, output = map(Path, sys.argv[1:])
helper = runpy.run_path(str(repo / "tools/windows/pe-evidence.py"))
helper["describe"](installer, installer.parent, output / "installer")
destination = output / "text"
for path in (output / "unpacked").rglob("*"):
    if path.is_file() and path.suffix.lower() in (".inf", ".ini", ".bat", ".txt"):
        data = path.read_bytes()
        encoding = "utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig"
        target = destination / path.relative_to(output / "unpacked")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(data.decode(encoding, errors="replace"))
PY

for name in 'WinUsbDisplay.exe$0' WinUsbDisplay.exe libVMonitor.dll; do
    directory="$output_dir/pe/app/$name"
    llvm-objdump -d --no-show-raw-insn --x86-asm-syntax=intel \
        "$output_dir/unpacked/app/$name" > "$directory/disassembly.asm"
    python3 "$repo_dir/tools/windows/annotate-asm.py" "$directory"
done
printf 'Static Windows artifacts: %s\n' "$output_dir"
