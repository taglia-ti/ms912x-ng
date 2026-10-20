#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exercise the production batch planner with userspace kernel shims."""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--sanitize", action="store_true",
                        help="Enable undefined-behavior and bounds checks")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="ms912x-batching-") as temporary:
        build = Path(temporary)
        (build / "compat.h").write_text((repo / "tools/test_rect_compat.h").read_text())
        for name in ("linux/types.h", "linux/minmax.h", "linux/sort.h",
                     "linux/string.h", "drm/drm_rect.h"):
            path = build / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "compat.h"\n')
        os.sync()
        binary = build / "test-batching"
        flags = ["-std=gnu11", "-O2", "-g", "-Wall", "-Wextra", "-Werror"]
        if args.sanitize:
            flags += ["-fsanitize=undefined,bounds", "-fno-omit-frame-pointer"]
        subprocess.run([*shlex.split(args.cc), *flags, "-I" + str(build),
                        "-I" + str(repo), str(repo / "ms912x_batch.c"),
                        str(repo / "tools/test_batching.c"), "-o", str(binary)],
                       check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
