#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Validate the production UYVY converter, scanner and packet path with RGBX/BGRX inputs."""
import argparse
import os
from pathlib import Path
import platform
import shlex
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
from test_damage import function


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC"))
    parser.add_argument("--arm", action="store_true", help="Cross-build and run ARM64 under QEMU")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--benchmark", action="store_true", help="Run the CPU pipeline/compactor microbenchmark instead of tests")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    machine = platform.machine().lower()
    arm = args.arm or machine in ("aarch64", "arm64")
    if not arm and machine not in ("x86_64", "amd64"):
        print("UYVY pipeline fixture skipped on this architecture; test-rgbx covers portable conversion")
        return
    if arm and args.benchmark:
        parser.error("The route comparison benchmark currently requires x86-64 with AVX2")
    args.cc = args.cc or ("aarch64-linux-gnu-gcc" if args.arm else "cc")
    shim = repo / "tools/simd_bench"
    with tempfile.TemporaryDirectory(prefix="ms912x-uyvy-") as directory:
        build = Path(directory)
        (build / "linux").mkdir()
        for name in ("linux/slab.h", "linux/minmax.h", "linux/sort.h", "drm/drm_rect.h"):
            path = build / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("/* Kernel shims are preincluded. */\n")
        damage = (repo / "ms912x_damage.c").read_text()
        first = damage.index("struct ms912x_scan_frame_stats {")
        last = damage.index("static __always_inline void", first)
        header = (repo / "ms912x.h").read_text()
        enum_start = header.index("enum ms912x_damage_scan_method {")
        enum_end = header.index("};", enum_start) + 2
        simd_start = damage.index("#ifdef CONFIG_X86_64")
        simd_end = damage.index("/* Caller holds damage_scan_lock. Resolve", simd_start)
        (build / "scan-backends.h").write_text("".join(function(damage, name) for name in (
            "ms912x_damage_scan_simd_method_available", "ms912x_damage_scan_select_auto",
            "ms912x_damage_scan_select_full_backend_locked",
            "ms912x_damage_scan_select_incremental_backend_locked")) +
            damage[simd_start:simd_end] + function(damage, "ms912x_damage_scan_run_rows"))
        (build / "scan-rows.h").write_text(header[enum_start:enum_end] + "\n" + damage[first:last] + "".join(
            function(damage, name) for name in (
                "ms912x_damage_scan_process_row", "ms912x_damage_scan_extend_bottom")))
        common = [*shlex.split(args.cc), "-O2", "-g", "-std=gnu11", "-D_GNU_SOURCE", "-DCONFIG_ARM64" if arm else "-DCONFIG_X86_64",
                  "-fno-tree-vectorize", "-fno-tree-slp-vectorize", "-fno-strict-aliasing",
                  "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare",
                  "-include", str(repo / "tools/test_uyvy_compat.h"),
                  "-I"+str(build), "-I"+str(shim / "include"), "-I"+str(repo), "-I"+str(repo / "tools")]
        if args.sanitize:
            common += ["-fsanitize=undefined,bounds", "-fno-omit-frame-pointer"]
        integer = ["-mgeneral-regs-only"] if arm else ["-mno-sse", "-mno-sse2", "-mno-mmx", "-msoft-float", "-mno-popcnt"]
        sources = [("ms912x_mode3.c", integer), ("ms912x_convert.c", integer), ("ms912x_scan_methods.c", integer),
                   ("ms912x_scan_rects.c", integer),
                   ("ms912x_batch.c", integer)]
        if arm:
            sources += [("ms912x_neon.c", ["-march=armv8-a+simd"]),
                        ("ms912x_scan_neon.c", ["-march=armv8-a+simd"]),
                        ("ms912x_neon_glue.c", integer)]
        else:
            sources += [("ms912x_sse2.c", ["-msse2", "-mno-avx"]),
                        ("ms912x_ssse3.c", ["-mssse3", "-mno-avx"]),
                        ("ms912x_avx2.c", ["-mavx2", "-mno-avx512f"]),
                        ("ms912x_scan_sse2.c", ["-msse2", "-mno-avx"]),
                        ("ms912x_scan_avx.c", ["-mavx", "-mno-avx2", "-mno-avx512f"]),
                        ("tools/benchmark_uyvy.c", [])]
        sources += [("shared/ms912x_rgbx_convert.c", []), ("shared/ms912x_rgbx_sse2.c", []),
                    ("shared/ms912x_rgbx_neon.c", []), ("tools/test_uyvy_pipeline.c", [])]
        objects = []
        for source, flags in sources:
            output = build / (Path(source).stem+".o")
            subprocess.run([*common, *flags, "-c", str(repo/source), "-o", str(output)], check=True)
            objects.append(str(output))
        binary = build / "pipeline"
        subprocess.run([*shlex.split(args.cc), *( ["-fsanitize=undefined,bounds"] if args.sanitize else []),
                        *(["-static"] if args.arm else []), *objects, "-o", str(binary)], check=True)
        subprocess.run([*(["qemu-aarch64"] if args.arm else []), str(binary), *(["--benchmark"] if args.benchmark else [])], check=True)


if __name__ == "__main__":
    main()
