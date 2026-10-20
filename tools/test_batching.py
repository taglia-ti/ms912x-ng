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
        (build / "compat.h").write_text(r"""
#ifndef BATCH_TEST_COMPAT_H
#define BATCH_TEST_COMPAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint64_t u64;
struct drm_rect { int x1, y1, x2, y2; };
#define DRM_RECT_INIT(x, y, w, h) \
    ((struct drm_rect){(x), (y), (x) + (w), (y) + (h)})
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min_t(t, a, b) min((t)(a), (t)(b))
#define max_t(t, a, b) max((t)(a), (t)(b))
static inline int drm_rect_width(const struct drm_rect *r) { return r->x2 - r->x1; }
static inline int drm_rect_height(const struct drm_rect *r) { return r->y2 - r->y1; }
static inline bool drm_rect_equals(const struct drm_rect *a, const struct drm_rect *b)
{ return a->x1 == b->x1 && a->x2 == b->x2 && a->y1 == b->y1 && a->y2 == b->y2; }
static inline void sort(void *base, size_t count, size_t size,
                        int (*compare)(const void *, const void *),
                        void (*swap)(void *, void *, int))
{ (void)swap; qsort(base, count, size, compare); }
#endif
""")
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
