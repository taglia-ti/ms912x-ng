#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compile small direct-call fixtures for the DRM-facing entry points."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def function(source, name):
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", source)
    if match is None:
        raise ValueError(f"Function definition not found: {name}")
    start = source.rfind("\n", 0, match.start()) + 1
    brace = source.index("{", match.start())
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def compile_and_run(cc, flags, repo, build, name, source):
    binary = build / name
    subprocess.run([*shlex.split(cc), *flags, "-I" + str(repo / "tools"),
                    "-I" + str(build), str(repo / "tools" / source),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent

    transfer = (repo / "ms912x_transfer.c").read_text()
    damage = (repo / "ms912x_damage.c").read_text()
    with tempfile.TemporaryDirectory(prefix="ms912x-entrypoints-") as temporary:
        build = Path(temporary)
        (build / "submit-entry.h").write_text("".join(
            function(transfer, name) for name in (
                "ms912x_submit_end_cpu_access",
                "ms912x_submit_wait_for_present",
                "ms912x_framebuffer_submit")))
        (build / "scan-entry.h").write_text(function(
            damage, "ms912x_damage_scan_find_snapshot_changes"))
        flags = ["-std=gnu11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-parameter"]
        compile_and_run(args.cc, flags, repo, build, "submit-entry",
                        "test_submit_entry.c")
        compile_and_run(args.cc, flags, repo, build, "scan-entry",
                        "test_scan_entry.c")


if __name__ == "__main__":
    main()
