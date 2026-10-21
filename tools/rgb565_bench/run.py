#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Validate packed RGB565 and benchmark native RGB565 versus XRGB-to-UYVY CPU paths."""
import argparse
import hashlib
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
        protocol = (repo / "ms912x_protocol.h").read_text()
        header = re.search(r"struct ms912x_frame_update_header \{.*?\} __packed;", protocol, re.S).group()
        mask = re.search(r"^#define MS912X_RGB565_WIRE_MASK\s+.*$", (repo / "ms912x_rgb565.h").read_text(), re.M).group()
        trailer = re.search(r"static const u8 ms912x_end_of_buffer\[\] = \{.*?\};", source, re.S).group()
        (build / "production-layout.h").write_text(header + "\n" + mask + "\n" + trailer + "\n")
        frame = function(source, "ms912x_convert_rgb565_rect")
        frame = frame.replace("static int ms912x_convert_rgb565_rect", "__attribute__((noinline)) int RGB565_NAME", 1)
        # Normalize the public benchmark ABI without changing the production row body.
        frame = frame.replace("const u16 *src,", "const void *pixels,")
        frame = frame.replace("{\n", "{\n\tconst u16 *src = pixels;\n", 1)
        scalar = frame.replace("RGB565_NAME", "rgb565_scalar")
        control = frame.replace("RGB565_NAME", "rgb565_nomask").replace(
            "ms912x_rgb565_mask_row(out, line + rect->x1, width);",
            "memcpy(out, line + rect->x1, width * 2);")
        (build / "scalar.c").write_text('#include "bench.h"\n#include "ms912x_rgb565.h"\n' + scalar + control)
        (build / "frame-template.h").write_text(frame.replace("ms912x_rgb565_mask_row", "mask_row"))
        loop = re.compile(r"\t\tms912x_rgb565_mask_row\(out, line \+ rect->x1, width\);\n\t\tout \+= width \* 2;")
        common = ["-std=gnu11", "-O2", "-g", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror",
                  "-Wno-sign-compare", "-fno-tree-vectorize", "-fno-tree-slp-vectorize",
                  "-fno-strict-aliasing", "-fno-lto", "-I" + str(here), "-I" + str(repo), "-I" + str(build)]
        if args.sanitize:
            common += ["-fsanitize=undefined,bounds", "-fno-omit-frame-pointer"]
        integer = (["-mno-sse", "-mno-sse2", "-mno-mmx", "-msoft-float"] if arch == "x86_64" else
                   ["-mgeneral-regs-only"] if arch == "arm64" else ["-mfpu=vfp"])
        objects = []

        def compile_source(path, name, flags):
            output = build / (name + ".o")
            subprocess.run([*cc, *common, *flags, "-c", str(path), "-o", str(output)], check=True)
            objects.append(str(output))

        (build / "linux").mkdir(exist_ok=True)
        (build / "linux/unaligned.h").write_text('/* Helpers are supplied by bench.h. */\n')
        compile_source(build / "scalar.c", "scalar", integer)
        for name in ("scalar_row",):
            compile_source(here / "kernels.c", name,
                           [*integer, "-DBACKEND_" + name.upper(), "-DRGB565_NAME=rgb565_" + name])
        if arch == "x86_64":
            variants = {
                "sse2": ["-msse2", "-mno-ssse3", "-mno-sse4.1", "-mno-avx"],
                "avx2": ["-mavx2", "-mno-avx512f"],
            }
        else:
            flags = ["-march=armv8-a+simd"] if arch == "arm64" else ["-march=armv7-a", "-mfpu=neon"]
            variants = {"neon": flags}
        for name, flags in variants.items():
            compile_source(here / "kernels.c", name,
                           [*flags, "-DBACKEND_" + name.upper(), "-DRGB565_NAME=rgb565_" + name])

        # Compare UYVY pixel workers in exactly the same framing/stride harness.
        color = (repo / "ms912x_color.h").read_text()
        scalar_color = "".join(function(color, name) for name in (
            "ms912x_rgb_to_y", "ms912x_rgb_pair_to_u", "ms912x_rgb_pair_to_v",
            "ms912x_xrgb_pair_to_uyvy"))
        scalar_color += function(source, "ms912x_xrgb_to_uyvy_line")
        uyvy_frame = frame.replace("const u16 *src", "const u32 *src").replace("const u16 *line", "const u32 *line")
        uyvy_scalar, replacements = loop.subn(
            "\t\tms912x_xrgb_to_uyvy_line(out, (const __le32 *)line, rect->x1, width);\n"
            "\t\tout += width * 2;", uyvy_frame)
        assert replacements == 1
        uyvy_scalar = uyvy_scalar.replace("unsigned int x, y, width;", "unsigned int y, width;")
        (build / "uyvy-scalar.c").write_text('#include "bench.h"\n' + scalar_color +
            uyvy_scalar.replace("RGB565_NAME", "uyvy_scalar"))
        compile_source(build / "uyvy-scalar.c", "uyvy-scalar", integer)
        shim = repo / "tools/simd_bench"
        workers = ("sse2", "avx2") if arch == "x86_64" else ("neon",)
        rows = re.compile(r"\tfor \(y = rect->y1; y < rect->y2; y\+\+\) \{.*?\n\t\}", re.S)
        for name in workers:
            flags = variants[name]
            compile_source(repo / ("ms912x_" + name + ".c"), "production-uyvy-" + name,
                [*flags, "-DCONFIG_X86_64" if arch == "x86_64" else
                 "-DCONFIG_ARM64" if arch == "arm64" else "-DCONFIG_ARM",
                 *(["-DCONFIG_KERNEL_MODE_NEON"] if arch == "arm32" else []),
                 "-include", str(shim / "compat.h"), "-I" + str(shim / "include")])
            symbol = "ms912x_convert_xrgb8888_to_uyvy_" + name
            if name == "neon":
                declaration = ("void ms912x_neon_convert_rows(void *, const __le32 *, "
                               "const struct drm_rect *, unsigned int, unsigned int);\n")
                call = ("\tfor (unsigned int row = rect->y1; row < (unsigned int)rect->y2;) {\n"
                        "\t\tunsigned int end = row + 16 < (unsigned int)rect->y2 ? row + 16 : rect->y2;\n"
                        "\t\tms912x_neon_convert_rows(out, (const __le32 *)src, rect, row, end);\n"
                        "\t\trow = end;\n\t}\n")
            else:
                declaration = (("bool " if name == "sse2" else "void ") + symbol +
                               "(void *, const __le32 *, const struct drm_rect *);\n")
                call = "\t" + symbol + "(out, (const __le32 *)src, rect);\n"
            simd_frame, replacements = rows.subn(call +
                "\tout += width * 2 * drm_rect_height(rect);", uyvy_frame)
            assert replacements == 1
            simd_frame = simd_frame.replace("unsigned int y, width;", "unsigned int width;")
            wrapper = build / ("uyvy-" + name + "-wrapper.c")
            wrapper.write_text('#include "bench.h"\n' + declaration +
                               simd_frame.replace("RGB565_NAME", "uyvy_" + name))
            compile_source(wrapper, "uyvy-" + name + "-wrapper", integer)
        # Production comparison, selective baseline, sampled-row margins and rectangle index.
        shim_include = shim / "include"
        config = ["-DCONFIG_X86_64"] if arch == "x86_64" else ["-DCONFIG_ARM64"] if arch == "arm64" else ["-DCONFIG_ARM", "-DCONFIG_KERNEL_MODE_NEON"]
        scan_workers = ("sse2", "avx") if arch == "x86_64" else ("neon",)
        for worker in scan_workers:
            flags = variants.get(worker, ["-mavx", "-mno-avx2"])
            compile_source(repo / ("ms912x_scan_" + worker + ".c"), "production-scan-" + worker,
                [*flags, *config, "-include", str(shim / "compat.h"), "-I" + str(shim_include)])
        if arch != "x86_64":
            compile_source(repo / "ms912x_neon_glue.c", "neon-glue",
                [*integer, *config, "-include", str(shim / "compat.h"), "-I" + str(shim_include)])
        driver_header = (repo / "ms912x.h").read_text()
        enum = re.search(r"enum ms912x_damage_scan_method \{.*?\};", driver_header, re.S).group()
        compat = r'''#include "bench.h"
#include <assert.h>
#include <stdlib.h>
#define MS912X_H
#define MS912X_MAX_HEIGHT 1200
#define MS912X_MAX_DAMAGE_RECTS 2048
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define min_t(t,a,b) min((t)(a),(t)(b))
#define max_t(t,a,b) max((t)(a),(t)(b))
#define max3(a,b,c) max(max(a,b),c)
#define DIV_ROUND_UP(a,b) (((a)+(b)-1)/(b))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define DRM_RECT_INIT(x,y,w,h) ((struct drm_rect){(x),(y),(x)+(w),(y)+(h)})
#define GFP_KERNEL 0
#define kvzalloc(n,f) calloc(1,n)
#define kvfree(p) free(p)
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
        (build / "pipeline-compat.h").write_text(compat+enum)
        for name in ("linux/slab.h",):
            (build / name).write_text('/* Supplied by pipeline-compat.h. */\n')
        compile_source(repo / "ms912x_scan_rects.c", "production-index",
            [*integer, *config, "-include", str(build / "pipeline-compat.h"), "-I"+str(shim_include)])
        damage = (repo / "ms912x_damage.c").read_text()
        structures = "".join(re.search(r"struct "+name+r" \{.*?\};", damage, re.S).group()+"\n"
            for name in ("ms912x_scan_frame_stats", "ms912x_scan_work", "ms912x_scan_config"))
        available = "static bool ms912x_damage_scan_simd_method_available(enum ms912x_damage_scan_method m) { (void)m; return true; }\n"
        functions = "".join(function(damage,name) for name in (
            "ms912x_damage_scan_run_simd_blocks", "ms912x_damage_scan_run_simd_relaxed",
            "ms912x_damage_scan_process_row", "ms912x_damage_scan_extend_bottom",
            "ms912x_damage_scan_prepare_baseline", "ms912x_damage_scan_run_rows"))
        method = "MS912X_DAMAGE_SCAN_AVX" if arch == "x86_64" else "MS912X_DAMAGE_SCAN_NEON"
        (build / "pipeline-functions.h").write_text(structures+available+functions+
            function((repo / "ms912x_transfer.c").read_text(), "ms912x_capture_pixel_rows"))
        compile_source(here / "pipeline.c", "pipeline", [*integer, *config,
            "-DPIPELINE_SCAN_METHOD="+method, "-Wno-unused-parameter", "-I"+str(shim_include)])
        compile_source(here / "pipeline-timing.c", "pipeline-timing", [])
        binary = build / "rgb565-bench"
        subprocess.run([*cc, *common, str(here / "main.c"), *objects, "-o", str(binary)], check=True)
        print("Compiler:", subprocess.check_output([*cc, "--version"], text=True).splitlines()[0], flush=True)
        manifest = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in sorted(repo.glob("ms912x*.[ch]"))
                    if not path.name.endswith(".mod.c")}
        import json
        (build / "source-sha256.json").write_text(json.dumps(manifest, indent=2) + "\n")
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
