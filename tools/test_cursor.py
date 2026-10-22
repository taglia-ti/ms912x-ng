#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compile and run the production cursor compositor in userspace."""

import argparse
import os
import re
from pathlib import Path
import shlex
import subprocess
import tempfile


def function(source, name):
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", source)
    if match is None:
        raise ValueError(f"Function definition not found: {name}")
    start = source.rfind("\n", 0, match.start()) + 1
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="ms912x-cursor-") as temporary:
        build = Path(temporary)
        (build / "compat.h").write_text(
            (repo / "tools/test_cursor_compat.h").read_text())
        for name in ("asm/byteorder.h", "linux/compiler.h", "linux/types.h",
                     "linux/kernel.h", "linux/math64.h", "linux/string.h"):
            path = build / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "compat.h"\n')

        source = (repo / "ms912x_convert.c").read_text()
        production = "".join(function(source, name) for name in (
            "ms912x_blend_u8", "ms912x_blend_pair_chroma",
            "ms912x_cursor_composite_uyvy"))
        (build / "cursor_production.c").write_text(
            '#include "ms912x_color.h"\n'
            '#include "ms912x_cursor.h"\n' + production)

        binary = build / "test-cursor"
        flags = ["-std=gnu11", "-O2", "-g", "-Wall", "-Wextra", "-Werror"]
        if args.sanitize:
            flags += ["-fsanitize=undefined,bounds", "-fno-omit-frame-pointer"]
        subprocess.run([*shlex.split(args.cc), *flags,
                        "-include", str(build / "compat.h"),
                        "-I" + str(build), "-I" + str(repo),
                        str(build / "cursor_production.c"),
                        str(repo / "tools/test_cursor.c"), "-o", str(binary)],
                       check=True)
        subprocess.run([str(binary)], check=True)

        transfer = (repo / "ms912x_transfer.c").read_text()
        (build / "cursor-snapshot.h").write_text(
            function(transfer, "ms912x_cursor_snapshot_request_locked"))
        binary = build / "test-cursor-isolation"
        subprocess.run([*shlex.split(args.cc), *flags,
                        "-include", str(repo / "tools" / "test_cursor_entry_compat.h"),
                        "-I" + str(build), "-I" + str(repo),
                        "-I" + str(repo / "tools"),
                        str(repo / "tools" / "test_cursor_isolation.c"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

        transfer = (repo / "ms912x_transfer.c").read_text()
        image_start = transfer.index("struct ms912x_cursor_image {")
        image_end = transfer.index("\n};", image_start) + len("\n};")
        (build / "cursor-prepare.h").write_text(transfer[image_start:image_end] + "\n" +
            function(transfer, "ms912x_cursor_prepare_image"))
        entry = "".join(function(transfer, name) for name in (
            "ms912x_rect_valid_for_frame", "ms912x_rect_clip_to_frame",
            "ms912x_rect_align_x4", "ms912x_cursor_bounds",
            "ms912x_cursor_state_position", "ms912x_cursor_scaled_coordinate",
            "ms912x_cursor_snapshot_request_locked",
            "ms912x_cursor_plane_submit", "ms912x_cursor_reconfigure"))
        (build / "cursor-entry.h").write_text(entry)
        driver = (repo / "ms912x_drv.c").read_text()
        (build / "cursor-buffers.h").write_text("".join(function(driver, name) for name in (
            "ms912x_free_cursor_buffers", "ms912x_init_cursor_buffers")))
        usb = (repo / "ms912x_usb_transfer.c").read_text()
        stop = function(usb, "ms912x_transfer_stop")
        stop = stop.split("\n\tcancel_work_sync(&ms912x->scan_work);", 1)[0]
        (build / "cursor-stop.h").write_text(stop + "\n (void)i; }\n")
        binary = build / "test-cursor-entry"
        subprocess.run([*shlex.split(args.cc), *flags, "-pthread", "-Wno-unused-parameter",
                        "-Wno-sign-compare", "-include", str(build / "compat.h"),
                        "-I" + str(build), "-I" + str(repo), "-I" + str(repo / "tools"),
                        str(build / "cursor_production.c"),
                        str(repo / "tools/test_cursor_entry.c"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
