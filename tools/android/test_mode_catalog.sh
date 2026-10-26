#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
python3 -B - "$repo_root" "$test_dir" <<'PY'
import importlib.util
from pathlib import Path
import sys

root, output = map(Path, sys.argv[1:])
spec = importlib.util.spec_from_file_location("edid", root / "tools/gen_fake_edid.py")
edid = importlib.util.module_from_spec(spec)
spec.loader.exec_module(edid)
modes, data = edid.generate()
(output / "monitor.edid").write_bytes(data)
keys = ("vic", "width", "height", "hz", "clock", "htotal", "vtotal",
        "hoffset", "voffset", "hsync", "vsync", "polarity")
(output / "modes.csv").write_text("\n".join(
    ",".join(str(mode[key]) for key in keys) for mode in modes))
PY
javac -d "$test_dir" \
    "$repo_root/android/app/src/main/java/io/github/taglia_ti/ms912x_ng/desktop/DisplayModeOption.java" \
    "$repo_root/android/app/src/main/java/io/github/taglia_ti/ms912x_ng/desktop/EdidModeMatcher.java" \
    "$repo_root/tools/android/DisplayModeOptionTest.java"
java -cp "$test_dir" io.github.taglia_ti.ms912x_ng.desktop.DisplayModeOptionTest \
    "$test_dir/monitor.edid" "$test_dir/modes.csv"
