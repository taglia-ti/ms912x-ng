#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Validate production 5:4 filters, cropped damage and scale selection."""
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


def arm32_check(repo, build, cc):
    shim = repo / "tools/simd_bench"
    for neon in (True, False):
        arch = "armv7-a" if neon else "armv6"
        common = ["--target=arm-linux-gnueabi", "-march=" + arch, "-marm",
                  "-mfloat-abi=softfp", "-ffreestanding", "-fno-builtin",
                  "-fno-vectorize", "-fno-slp-vectorize", "-fno-stack-protector",
                  "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-fomit-frame-pointer",
                  "-O2", "-Wall", "-Wextra", "-Werror", "-DCONFIG_ARM",
                  "-include", str(repo / "tools/test_downscale_5to4_arm32_compat.h"),
                  "-I" + str(shim / "include"), "-I" + str(repo)]
        if neon:
            common += ["-DCONFIG_KERNEL_MODE_NEON"]
        names = ["ms912x_downscale_5to4_scalar.c", "ms912x_downscale_5to4.c",
                 "ms912x_scan_methods.c", "tools/test_downscale_5to4_arm32.c"]
        if neon:
            names += ["ms912x_downscale_5to4_neon.c", "ms912x_downscale_5to4_neon_glue.c"]
        objects = []
        for name in names:
            output = build / (arch + "-" + Path(name).stem + ".o")
            fpu = ["-mfpu=neon"] if name.endswith("_neon.c") else (["-mfpu=vfp"] if neon else [])
            subprocess.run([*cc, *common, *fpu, "-c", str(repo / name), "-o", str(output)], check=True)
            objects.append(str(output))
        binary = build / ("verify-5to4-" + arch)
        subprocess.run([*cc, "--target=arm-linux-gnueabi", "-fuse-ld=lld", "-nostdlib",
                        "-static", "-Wl,-e,_start", *objects, "-o", str(binary)], check=True)
        subprocess.run(["qemu-arm", "-cpu", "cortex-a9" if neon else "arm1176", str(binary)], check=True)
        print(f"PASS 5:4 {arch}: 325 geometries, all area phases/channels, in-place/packed, "
              "cropped regions, empty dimensions" + (", NEON guards" if neon else ", no-NEON fallback"), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc")
    arm = parser.add_mutually_exclusive_group()
    arm.add_argument("--arm", action="store_true", help="Validate AArch64 under QEMU")
    arm.add_argument("--arm32", action="store_true", help="Validate ARMv7 NEON and ARMv6 fallback with Clang/QEMU")
    parser.add_argument("--sysroot", type=Path)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--benchmark", action="store_true")
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    if (args.arm or args.arm32) and (args.benchmark or args.sanitize):
        parser.error("Use native execution for benchmarks and sanitizers")
    repo = Path(__file__).resolve().parent.parent
    shim = repo / "tools/simd_bench"
    target = "arm64" if args.arm else platform.machine().lower()
    native_arm32 = target.startswith("arm") and target != "arm64"
    is_arm = target in ("arm64", "aarch64") or native_arm32
    has_neon = is_arm and not target.startswith("armv6")
    if not is_arm and target not in ("x86_64", "amd64"):
        parser.error("Supported targets: x86-64, ARM32 and AArch64")
    cc = shlex.split(args.cc or ("clang" if args.arm32 else "aarch64-linux-gnu-gcc"
                                if args.arm else os.environ.get("CC", "cc")))
    with tempfile.TemporaryDirectory(prefix="ms912x-5to4-") as temporary:
        build = (args.build_dir or Path(temporary)).resolve()
        build.mkdir(parents=True, exist_ok=True)
        if args.arm32:
            arm32_check(repo, build, cc)
            return
        transfer = (repo / "ms912x_transfer.c").read_text()
        hooks = "".join(function(transfer, name) for name in (
            "ms912x_rect_map_output_to_input", "ms912x_capture_pixel_rows",
            "ms912x_snapshot_capture_damage"))
        hooks = """struct mutex { int unused; };
static struct mutex ms912x_scale_lock;
static void mutex_lock(struct mutex *lock) { (void)lock; }
static void mutex_unlock(struct mutex *lock) { (void)lock; }
static int atomic_read(const unsigned int *value) { return (int)*value; }
""" + hooks
        params = (repo / "ms912x_params.c").read_text()
        hooks += function(params, "ms912x_scale_parse")
        hooks += function(params, "ms912x_scale_set")
        hooks += function(params, "ms912x_scale_snapshot")
        (build / "scale-hooks.h").write_text(hooks)
        common = ["-std=gnu11", "-O2", "-g", "-D_GNU_SOURCE",
                  "-fno-tree-vectorize", "-fno-tree-slp-vectorize", "-fno-strict-aliasing",
                  "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare",
                  "-DCONFIG_ARM" if native_arm32 else "-DCONFIG_ARM64" if is_arm else "-DCONFIG_X86_64",
                  "-include", str(shim / "compat.h"), "-I" + str(shim / "include"),
                  "-I" + str(repo), "-I" + str(build)]
        if args.sanitize:
            common += ["-fsanitize=undefined,bounds", "-fno-omit-frame-pointer"]
        if native_arm32 and has_neon:
            common += ["-DCONFIG_KERNEL_MODE_NEON"]
        integer = (["-mfpu=vfp"] if native_arm32 else ["-mgeneral-regs-only"] if is_arm else
                   ["-mno-sse", "-mno-sse2", "-mno-mmx", "-msoft-float"])
        objects = []

        def compile_source(name, flags):
            output = build / (Path(name).stem + ".o")
            subprocess.run([*cc, *common, *flags, "-c", str(repo / name),
                            "-o", str(output)], check=True)
            objects.append(str(output))

        for name in ("ms912x_downscale.c",
                     "ms912x_downscale_5to4.c",
                     "ms912x_downscale_5to4_scalar.c", "ms912x_scan_methods.c"):
            compile_source(name, integer)
        if has_neon:
            compile_source("ms912x_downscale_5to4_neon.c", ["-march=armv7-a", "-mfpu=neon"]
                           if native_arm32 else ["-march=armv8-a+simd"])
            compile_source("ms912x_downscale_5to4_neon_glue.c", integer)
        elif not is_arm:
            compile_source("ms912x_downscale_5to4_sse2.c", ["-msse2", "-mno-ssse3",
                           "-mno-sse4.1", "-mno-avx"])
            compile_source("ms912x_downscale_5to4_ssse3.c", ["-mssse3", "-mno-sse4.1", "-mno-avx"])
            compile_source("ms912x_downscale_5to4_avx2.c", ["-mavx2", "-mno-avx512f"])
        binary = build / "verify-5to4"
        subprocess.run([*cc, *common, str(repo / "tools/test_downscale_5to4.c"),
                        *objects, "-o", str(binary)], check=True)
        command = [str(binary), *(["--benchmark"] if args.benchmark else [])]
        if args.arm:
            sysroot = args.sysroot or Path(subprocess.check_output(
                [*cc, "-print-sysroot"], text=True).strip())
            command = ["qemu-aarch64", "-cpu", "cortex-a53", "-L", str(sysroot), *command]
        os.sync()
        subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
