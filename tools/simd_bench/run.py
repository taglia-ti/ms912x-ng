#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exercise current production dispatch and scaling code; never build historical snapshots."""
import argparse
import os
import platform
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--bench", action="store_true", help="Time current 3:2 region backends")
    parser.add_argument("--sanitize", action="store_true")
    arm = parser.add_mutually_exclusive_group()
    arm.add_argument("--arm", action="store_true", help="AArch64 correctness under QEMU")
    arm.add_argument("--arm32", action="store_true", help="ARMv7 correctness under QEMU")
    parser.add_argument("--cc")
    parser.add_argument("--sysroot", type=Path)
    parser.add_argument("--cpu", help="QEMU CPU model, correctness only")
    parser.add_argument("--build-only", action="store_true")
    args = parser.parse_args()
    if args.bench and (args.arm or args.arm32 or args.cpu):
        parser.error("Emulation cannot measure native CPU performance")
    here = Path(__file__).resolve().parent
    repo = here.parent.parent
    machine = platform.machine().lower()
    target = ("arm64" if args.arm else "arm32" if args.arm32 else
              "arm64" if machine in ("aarch64", "arm64") else
              "arm32" if machine.startswith("arm") else
              "x86_64" if machine in ("x86_64", "amd64") else None)
    if target is None:
        parser.error(f"Unsupported native architecture: {machine}")
    cc = shlex.split(args.cc or os.environ.get("CC") or (
        "aarch64-linux-gnu-gcc" if args.arm else
        "arm-linux-gnueabihf-gcc" if args.arm32 else "cc"))
    build = args.build_dir or Path(tempfile.mkdtemp(prefix="ms912x-current-pixels-"))
    build.mkdir(parents=True, exist_ok=True)
    arch = {"arm64": "CONFIG_ARM64", "arm32": "CONFIG_ARM", "x86_64": "CONFIG_X86_64"}[target]
    common = ["-O2", "-g", "-D_GNU_SOURCE", "-fno-tree-vectorize",
              "-fno-tree-slp-vectorize", "-fno-strict-aliasing", "-D"+arch,
              "-include", str(here/"compat.h"), "-include", str(repo/"ms912x_neon.h"),
              "-I"+str(here/"include"), "-I"+str(repo),
              "-Wall", "-Wextra", "-Werror"]
    if target == "arm32":
        common += ["-DCONFIG_KERNEL_MODE_NEON"]
    if args.sanitize:
        common += ["-fsanitize=undefined,bounds", "-fno-omit-frame-pointer"]
    integer = (["-mgeneral-regs-only"] if target == "arm64" else
               ["-mfpu=vfp"] if target == "arm32" else
               ["-mno-sse", "-mno-sse2", "-mno-mmx", "-msoft-float"])
    objects = []

    def compile_source(source, flags):
        output = build/(source.stem+".o")
        subprocess.run([*cc, *common, *flags, "-c", str(source), "-o", str(output)], check=True)
        objects.append(str(output))

    sources = [("ms912x_downscale_3to2.c", integer),
               ("ms912x_downscale_3to2_scalar.c", integer),
               ("ms912x_scan_methods.c", integer),
               ("shared/ms912x_scan_backend.c", [*integer, "-D__KERNEL__", "-DMS912X_PLATFORM_H"])]
    if target == "x86_64":
        sources += [("ms912x_downscale_3to2_sse2.c", ["-msse2", "-mno-avx"]),
                    ("ms912x_downscale_3to2_ssse3.c", ["-mssse3", "-mno-avx"]),
                    ("ms912x_downscale_3to2_avx2.c", ["-mavx2", "-mno-avx512f"]),
                    ("ms912x_scan_sse2.c", ["-msse2", "-mno-avx"]),
                    ("ms912x_scan_avx.c", ["-mavx", "-mno-avx2"])]
    else:
        neon = ["-march=armv8-a+simd"] if target == "arm64" else ["-march=armv7-a", "-mfpu=neon"]
        sources += [("ms912x_downscale_3to2_neon.c", neon),
                    ("ms912x_downscale_3to2_neon_glue.c", integer),
                    ("ms912x_neon.c", neon), ("ms912x_scan_neon.c", neon),
                    ("ms912x_neon_glue.c", integer)]
    for name, flags in sources:
        compile_source(repo/name, flags)

    # Compile the production selector text unchanged against synthetic hardware flags.
    header = (repo/"ms912x.h").read_text()
    enums = "\n".join(re.search(r"enum "+name+r" \{.*?\};", header, re.S).group()
                      for name in ("ms912x_fast_chroma_backend", "ms912x_damage_scan_method"))
    convert = (repo/"ms912x_convert.c").read_text()
    start = convert.index("#ifdef CONFIG_X86_64", convert.index('#include "ms912x_pixels.h"'))
    end = convert.index("static __always_inline void ms912x_rgbx_to_uyvy_line")
    dispatch = build/"dispatch.c"
    dispatch.write_text(
        '#include <assert.h>\n#include <stdio.h>\n#include <asm/cpufeature.h>\n'
        '#include "shared/ms912x_scan_backend.h"\n'+enums+'\n'
        'unsigned int bench_cpu_features;\n'
        'static struct {enum ms912x_fast_chroma_backend fast_chroma;} ms912x_parameters;\n'
        +convert[start:end]+(here/"dispatch_checks.c").read_text())
    compile_source(dispatch, integer)
    binary = build/"verify"
    subprocess.run([*cc, *common, str(here/"verify.c"), *objects, "-o", str(binary)], check=True)
    if args.build_only:
        print(binary)
        return
    command = [str(binary)]
    if args.arm or args.arm32:
        compiler = Path(shutil.which(cc[0])).resolve()
        triple = subprocess.check_output([*cc, "-dumpmachine"], text=True).strip()
        loaders = (("ld-linux-aarch64.so.1", "ld-musl-aarch64.so.1") if args.arm else
                   ("ld-linux-armhf.so.3", "ld-musl-armhf.so.1", "ld-musl-arm.so.1"))
        candidates = ([args.sysroot] if args.sysroot else [
            Path(subprocess.check_output([*cc, "-print-sysroot"], text=True).strip()),
            compiler.parent.parent/triple/"libc", Path("/usr")/triple])
        sysroot = next((p for p in candidates if any((p/"lib"/loader).exists() for loader in loaders)), None)
        if sysroot is None:
            parser.error("Cannot find target libc; pass --sysroot")
        command = ["qemu-aarch64" if args.arm else "qemu-arm", "-cpu",
                   args.cpu or ("cortex-a53" if args.arm else "cortex-a7"), "-L", str(sysroot), *command]
    elif args.cpu:
        command = ["qemu-x86_64", "-cpu", args.cpu, *command]
    subprocess.run([*command, *(["--bench"] if args.bench else [])], check=True)


if __name__ == "__main__":
    main()
