#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compile and run the production cursor compositor in userspace."""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def function(source, name):
    start = source.index(name + "(")
    start = source.rfind("\n", 0, start) + 1
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
        for name in ("asm/byteorder.h", "linux/compiler.h", "linux/types.h"):
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


if __name__ == "__main__":
    main()