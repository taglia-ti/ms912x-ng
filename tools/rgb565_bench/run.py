#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Validate packed RGB565 and benchmark native RGB565 versus XRGB-to-UYVY CPU paths."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from test_damage import function


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--arch", choices=("x86_64", "arm64", "arm32"))
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--bench", action="store_true")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--csv", type=Path)
    args = parser.parse_args()
    machine = platform.machine().lower()
    native = "arm64" if machine in ("arm64", "aarch64") else "arm32" if machine.startswith("arm") else machine
    arch = args.arch or native
    if arch not in ("x86_64", "arm64", "arm32"):
        parser.error("Supported targets: x86_64, arm64, arm32")
    if arch != native and not args.build_only:
        parser.error("Cross builds require --build-only; execute on native hardware or QEMU for correctness")
    here = Path(__file__).resolve().parent
    repo = here.parents[1]
    cc = shlex.split(args.cc)
    with tempfile.TemporaryDirectory(prefix="ms912x-rgb565-") as temporary:
        if args.build_dir is not None:
            build = args.build_dir.resolve()
        elif args.bench:
            build = Path(tempfile.mkdtemp(prefix="ms912x-rgb565-results-"))
        else:
            build = Path(temporary)
        if args.build_only and args.build_dir is None:
            parser.error("--build-only requires --build-dir")
        build.mkdir(parents=True, exist_ok=True)
        source = (repo / "ms912x_convert.c").read_text()
        driver_header = (repo / "ms912x.h").read_text()
        protocol = (repo / "ms912x_protocol.h").read_text()
        header = re.search(r"struct ms912x_frame_update_header \{.*?\} __packed;", protocol, re.S).group()
        trailer = re.search(r"static const u8 ms912x_end_of_buffer\[\] = \{.*?\};", source, re.S).group()
        (build / "production-layout.h").write_text(header + "\n" + trailer + "\n")
        enums = "\n".join(re.search(r"enum " + name + r" \{.*?\};", driver_header, re.S).group()
                          for name in ("ms912x_fast_chroma_backend", "ms912x_damage_scan_method"))
        (build / "production-enums.h").write_text(enums + "\n")
        api = "\n".join(re.search(r"\b(?:bool|void)\s+" + name + r"\([^;{}]*\);", driver_header, re.S).group()
                        for name in ("ms912x_convert_xrgb8888_to_uyvy_sse2", "ms912x_convert_xrgb8888_to_uyvy_ssse3",
                                     "ms912x_convert_xrgb8888_to_uyvy_avx2", "ms912x_convert_xrgb8888_to_uyvy_neon"))
        (build / "production-api.h").write_text(api + "\n")
        constants = []
        lines = driver_header.splitlines()
        for name in ("MS912X_MAX_WIDTH", "MS912X_MAX_HEIGHT", "MS912X_MAX_DAMAGE_RECTS",
                     "MS912X_MAX_BATCH_RECTS", "MS912X_INITIAL_DAMAGE_RECTS", "MS912X_DAMAGE_RECTS_GROWTH"):
            index = next(i for i, line in enumerate(lines) if re.match(r"#define\s+" + name + r"\b", line))
            constants.append(lines[index])
            while lines[index].endswith("\\"):
                index += 1
                constants.append(lines[index])
        (build / "production-limits.h").write_text("\n".join(constants) + "\n")

        # The generated translation unit contains verbatim project functions.
        # Backend selection is controlled through platform shims and parameters,
        # never by replacing a pixel loop, framing code or dispatch function.
        extracted = {}

        def extract(path, names):
            contents = (repo / path).read_text()
            functions = []
            for name in names:
                body = function(contents, name)
                extracted[name] = {"source": path, "sha256": hashlib.sha256(body.encode()).hexdigest()}
                functions.append(body)
            return "\n".join(functions)

        converters = extract("ms912x_convert.c", (
            "ms912x_fast_chroma_select", "ms912x_xrgb_to_uyvy_line",
            "ms912x_rgb565_mask_rect", "ms912x_convert_rgb565_rect",
            "ms912x_convert_rgb565_batch", "ms912x_convert_xrgb8888_to_uyvy_rect",
            "ms912x_convert_xrgb8888_to_uyvy_batch"))
        (build / "production-functions.h").write_text(converters)
        shim = repo / "tools/simd_bench"
        shim_include = shim / "include"
        config = (["-DCONFIG_X86_64"] if arch == "x86_64" else ["-DCONFIG_ARM64"]
                  if arch == "arm64" else ["-DCONFIG_ARM", "-DCONFIG_KERNEL_MODE_NEON"])
        common = ["-std=gnu11", "-O2", "-g", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror",
                  "-Wno-sign-compare", "-Wno-unused-parameter", "-fno-tree-vectorize",
                  "-fno-tree-slp-vectorize", "-fno-strict-aliasing", "-fno-lto",
                  "-I" + str(here), "-I" + str(repo), "-I" + str(build), "-I" + str(shim_include)]
        if args.sanitize:
            common += ["-fsanitize=undefined,bounds", "-fno-omit-frame-pointer"]
        integer = (["-mno-sse", "-mno-sse2", "-mno-mmx", "-msoft-float"] if arch == "x86_64" else
                   ["-mgeneral-regs-only"] if arch == "arm64" else ["-mfpu=vfp"])
        objects = []

        def compile_source(path, name, flags):
            output = build / (name + ".o")
            subprocess.run([*cc, *common, *flags, "-c", str(path), "-o", str(output)], check=True)
            objects.append(str(output))

        compile_source(here / "production.c", "production-conversion", [*integer, *config])
        variants = ({"sse2": ["-msse2", "-mno-ssse3", "-mno-sse4.1", "-mno-avx"],
                     "ssse3": ["-mssse3", "-mno-sse4.1", "-mno-avx"],
                     "avx2": ["-mavx2", "-mno-avx512f"]} if arch == "x86_64" else
                    {"neon": ["-march=armv8-a+simd"] if arch == "arm64" else ["-march=armv7-a", "-mfpu=neon"]})
        for name, flags in variants.items():
            compile_source(repo / ("ms912x_" + name + ".c"), "production-" + name,
                           [*flags, *config, "-include", str(shim / "compat.h")])
        compile_source(repo / "ms912x_scan_methods.c", "production-availability",
                       [*integer, *config, "-include", str(shim / "compat.h")])
        # Production comparison, selective baseline, sampled-row margins and rectangle index.
        scan_workers = ("sse2", "avx") if arch == "x86_64" else ("neon",)
        for worker in scan_workers:
            flags = variants.get(worker, ["-mavx", "-mno-avx2"])
            compile_source(repo / ("ms912x_scan_" + worker + ".c"), "production-scan-" + worker,
                [*flags, *config, "-include", str(shim / "compat.h"), "-I" + str(shim_include)])
        if arch != "x86_64":
            compile_source(repo / "ms912x_neon_glue.c", "neon-glue",
                [*integer, *config, "-include", str(shim / "compat.h"), "-I" + str(shim_include)])
        compat = r'''#include "bench.h"
#include <assert.h>
#include <stdlib.h>
#define MS912X_H
#define max_t(t,a,b) max((t)(a),(t)(b))
#define DRM_RECT_INIT(x,y,w,h) ((struct drm_rect){(x),(y),(x)+(w),(y)+(h)})
#define GFP_KERNEL 0
#define kvzalloc(n,f) calloc(1,n)
#define kvfree(p) free(p)
#define kvrealloc(p,n,f) realloc(p,n)
static inline bool drm_rect_equals(const struct drm_rect *a,const struct drm_rect *b)
{ return a->x1==b->x1 && a->y1==b->y1 && a->x2==b->x2 && a->y2==b->y2; }
struct drm_rect *ms912x_rect_vector_grow(struct drm_rect *,unsigned *,unsigned,unsigned);
#define DRM_FORMAT_RGB565 1
struct drm_format_info { unsigned format; };
struct drm_framebuffer { const struct drm_format_info *format; unsigned pitches[1]; };
struct iosys_map { const void *data; };
static inline void iosys_map_memcpy_from(void *dst,const struct iosys_map *map,size_t off,size_t n)
{ memcpy(dst,(const u8 *)map->data+off,n); }
'''
        (build / "pipeline-compat.h").write_text(compat)
        for name in ("linux/slab.h",):
            (build / name).parent.mkdir(parents=True, exist_ok=True)
            (build / name).write_text('/* Supplied by pipeline-compat.h. */\n')
        compile_source(repo / "ms912x_scan_rects.c", "production-index",
            [*integer, *config, "-include", str(build / "pipeline-compat.h"), "-I"+str(shim_include)])
        damage = (repo / "ms912x_damage.c").read_text()
        structures = "".join(re.search(r"struct "+name+r" \{.*?\};", damage, re.S).group()+"\n"
            for name in ("ms912x_scan_frame_stats", "ms912x_scan_work", "ms912x_scan_config"))
        available = extract("ms912x_damage.c", ("ms912x_damage_scan_simd_method_available",))
        functions = extract("ms912x_damage.c", (
            "ms912x_damage_scan_run_simd_blocks", "ms912x_damage_scan_run_simd_relaxed",
            "ms912x_damage_scan_process_row", "ms912x_damage_scan_extend_bottom",
            "ms912x_damage_scan_prepare_baseline", "ms912x_damage_scan_run_rows"))
        method = "MS912X_DAMAGE_SCAN_AVX" if arch == "x86_64" else "MS912X_DAMAGE_SCAN_NEON"
        (build / "pipeline-functions.h").write_text(structures+available+functions+
            extract("ms912x_transfer.c", ("ms912x_rect_vector_grow", "ms912x_capture_pixel_rows")))
        compile_source(here / "pipeline.c", "pipeline", [*integer, *config,
            "-DPIPELINE_SCAN_METHOD="+method, "-Wno-unused-parameter", "-I"+str(shim_include)])
        compile_source(here / "pipeline-timing.c", "pipeline-timing", [])
        binary = build / "rgb565-bench"
        subprocess.run([*cc, *common, str(here / "main.c"), *objects, "-o", str(binary)], check=True)
        print("Compiler:", subprocess.check_output([*cc, "--version"], text=True).splitlines()[0], flush=True)
        manifest = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in sorted(repo.glob("ms912x*.[ch]"))
                    if not path.name.endswith(".mod.c")}
        (build / "source-sha256.json").write_text(json.dumps(manifest, indent=2) + "\n")
        (build / "extracted-functions.json").write_text(json.dumps(extracted, indent=2) + "\n")
        print("Production source SHA256:", hashlib.sha256(source.encode()).hexdigest(), flush=True)
        print("Source manifest:", build / "source-sha256.json", flush=True)
        print("Binary:", binary, flush=True)
        if not args.build_only:
            csv = (args.csv or build / "samples.csv").resolve()
            csv.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run([str(binary), *(["--bench", "--csv", str(csv)] if args.bench else [])], check=True)
            if args.bench:
                print("Raw samples:", csv, flush=True)


if __name__ == "__main__":
    main()
