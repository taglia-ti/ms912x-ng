#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compare actual driver pixel loops with a Git baseline; no module reload."""

import argparse
import os
import platform
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", default="9a4a6ad")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--bench", action="store_true")
    arm = parser.add_mutually_exclusive_group()
    arm.add_argument("--arm", action="store_true", help="AArch64 correctness under QEMU")
    arm.add_argument("--arm32", action="store_true", help="ARMv7 NEON correctness under QEMU")
    parser.add_argument("--cc", help="C compiler (default: native GCC or target cross compiler)")
    parser.add_argument("--sysroot", type=Path, help="Target libc directory for QEMU")
    parser.add_argument("--cpu", help="QEMU CPU model (x86 or selected ARM target), correctness only")
    parser.add_argument("--build-only", action="store_true")
    args = parser.parse_args()
    if args.bench and (args.arm or args.arm32 or args.cpu):
        parser.error("Emulation cannot measure native CPU performance")

    here = Path(__file__).resolve().parent
    repo = here.parent.parent
    build = (args.build_dir or Path(tempfile.mkdtemp(prefix="ms912x-simd-"))).resolve()
    build.mkdir(parents=True, exist_ok=True)
    baseline = build / "baseline"
    baseline.mkdir(exist_ok=True)
    def git_output(*arguments):
        try:
            return subprocess.check_output(["git", *arguments], cwd=repo)
        finally:
            os.sync()

    revision = git_output("rev-parse", args.baseline).decode().strip()
    files = git_output("ls-tree", "-r", "--name-only", revision).decode().splitlines()
    for name in files:
        path = Path(name)
        if path.parent == Path(".") and name.startswith("ms912x") and path.suffix in (".c", ".h"):
            (baseline / name).write_bytes(git_output("show", f"{revision}:{name}"))

    machine = platform.machine().lower()
    target = ("arm64" if args.arm else "arm32" if args.arm32 else
              "arm64" if machine in ("aarch64", "arm64") else
              "arm32" if machine.startswith("arm") else
              "x86_64" if machine in ("x86_64", "amd64") else None)
    if target is None:
        parser.error(f"Unsupported native architecture: {machine}")
    cc = args.cc or ("aarch64-linux-gnu-gcc" if args.arm else
                     "arm-linux-gnueabihf-gcc" if args.arm32 else "gcc")
    arch = {"arm64": "CONFIG_ARM64", "arm32": "CONFIG_ARM",
            "x86_64": "CONFIG_X86_64"}[target]
    is_arm = target != "x86_64"
    common = ["-O2", "-g", "-D_GNU_SOURCE", "-fno-tree-vectorize",
              "-fno-tree-slp-vectorize", "-fno-strict-aliasing", "-D" + arch,
              "-include", str(here / "compat.h"), "-I" + str(here / "include"),
              "-Wall", "-Wextra", "-Werror"]
    if target == "arm32":
        common += ["-DCONFIG_KERNEL_MODE_NEON"]
    objects = []

    def compile_source(source, output, extra=(), include=repo):
        subprocess.run([cc, *common, "-I" + str(include), *extra,
                        "-c", str(source), "-o", str(output)], check=True)
        objects.append(str(output))

    integer_flags = (["-mgeneral-regs-only"] if target == "arm64" else
                     ["-mfpu=vfp"] if target == "arm32" else
                     ["-mno-sse", "-mno-sse2", "-mno-mmx", "-msoft-float", "-mno-popcnt"])

    backends = ["neon"] if is_arm else ["sse2", "avx2"]
    for backend in backends:
        flags = (["-march=armv7-a", "-mfpu=neon"] if target == "arm32" else
                 ["-march=armv8-a+simd"] if is_arm else (
            ["-mavx2", "-mno-avx512f"] if backend == "avx2" else
            ["-msse2", "-mno-ssse3", "-mno-sse4.1", "-mno-avx", "-mno-popcnt"]))
        for old in (True, False):
            include = baseline if old else repo
            symbols = [f"ms912x_convert_xrgb8888_to_uyvy_{backend}",
                       f"ms912x_scan_rows_{backend}_blocks",
                       f"ms912x_scan_rows_{backend}_relaxed"]
            if backend == "neon":
                symbols += ["ms912x_neon_convert_rows", "ms912x_scan_rows_neon_blocks_fpu",
                            "ms912x_scan_rows_neon_relaxed_fpu"]
            rename = [f"-D{s}=old_{s}" for s in symbols] if old else []
            # The original backend is AArch64-only. Backport its reductions
            # for an arithmetic reference on ARM32; this is not old ARM32 support.
            worker_rename = list(rename)
            if old and target == "arm32":
                worker_rename += ["-include", str(here / "arm32_baseline.h")]
            for part in ("", "scan_"):
                name = f"ms912x_{part}{backend}"
                output = build / (("old_" if old else "") + name + ".o")
                compile_source(include / (name + ".c"), output, [*flags, *worker_rename], include)
            if old and backend == "neon" and (include / "ms912x_neon_glue.c").exists():
                compile_source(include / "ms912x_neon_glue.c", build / "old_neon_glue.o",
                               [*integer_flags, *rename], include)

    if not is_arm:
        compile_source(repo / "ms912x_ssse3.c", build / "ssse3.o",
                       ["-mssse3", "-mno-sse4.1", "-mno-avx", "-mno-popcnt"])
        compile_source(repo / "ms912x_scan_sse41.c", build / "sse41.o",
                       ["-msse4.1", "-mno-sse4.2", "-mno-avx", "-mno-popcnt"])

    if is_arm:
        compile_source(repo / "ms912x_neon_glue.c", build / "neon_glue.o", integer_flags)
    for label, include in (("baseline", baseline), ("current", repo)):
        wrapper = build / (label + "_memcmp.c")
        wrapper.write_text(
            '#include "ms912x_scan_methods.h"\n'
            f'void {label}_memcmp(const u32 *a,const u32 *b,unsigned w, unsigned stride,'
            'unsigned rows,struct ms912x_scan_row_result *r) {\n'
            'for(unsigned y=0;y<rows;y++)'
            'ms912x_scan_row_memcmp(a+y*stride,b+y*stride,w,r+y);\n}\n'
        )
        compile_source(wrapper, build / (label + "_memcmp.o"),
                       [*integer_flags, "-Dmemcmp=review_kernel_memcmp"], include)
    compile_source(here / "kernel_memcmp.c", build / "kernel_memcmp.o",
                   [*integer_flags, "-fno-builtin"])
    compile_source(here / "scalar.c", build / "scalar.o", integer_flags)
    # Compare each downscale ISA against its actual Git baseline when present.
    # Older baselines predate downscale; use scalar as their explicit reference.
    have_downscale_baseline = "ms912x_downscale_scalar.c" in files
    for old in (True, False):
        include = baseline if old and have_downscale_baseline else repo
        modes = ["scalar", "neon"] if is_arm else ["scalar", "sse2", "avx2"]
        symbols = [f"ms912x_downscale_xrgb8888_2x_{mode}" for mode in modes]
        symbols += ["ms912x_downscale_xrgb8888_2x_neon_rows_fpu"]
        rename = [f"-D{s}=old_{s}" for s in symbols] if old else []
        for mode in modes:
            flags = (integer_flags if mode == "scalar" else
                     ["-march=armv7-a", "-mfpu=neon"] if target == "arm32" else
                     ["-march=armv8-a+simd"] if is_arm else
                     ["-mavx2", "-mno-avx512f"] if mode == "avx2" else
                     ["-msse2", "-mno-ssse3", "-mno-sse4.1", "-mno-avx", "-mno-popcnt"])
            label = ("old_" if old else "") + "downscale_" + mode
            if old and not have_downscale_baseline and mode != "scalar":
                continue
            compile_source(include / f"ms912x_downscale_{mode}.c", build / (label + ".o"),
                           [*flags, *rename], include)
        if is_arm and (not old or have_downscale_baseline):
            compile_source(include / "ms912x_downscale_neon_glue.c",
                           build / (("old_" if old else "") + "downscale_neon_glue.o"),
                           [*integer_flags, *rename], include)
    if not have_downscale_baseline:
        common += ["-DMS912X_DOWNSCALE_SCALAR_BASELINE"]
    compile_source(repo / "ms912x_scan_methods.c", build / "scan_availability.o", integer_flags)

    # Compile the production selectors themselves against synthetic CPUID bits.
    # This exercises unsupported forced modes without executing their SIMD code.
    header = (repo / "ms912x.h").read_text()
    enums = "\n".join(re.search(r"enum " + name + r" \{.*?\};", header, re.S).group()
                      for name in ("ms912x_fast_chroma_backend", "ms912x_damage_scan_method"))
    convert = (repo / "ms912x_convert.c").read_text()
    convert = convert[convert.index("static enum ms912x_fast_chroma_backend ms912x_fast_chroma_select"):
                      convert.index("static void ms912x_xrgb_to_uyvy_line")]
    damage = (repo / "ms912x_damage.c").read_text()
    damage = damage[damage.index("static bool ms912x_damage_scan_simd_method_available"):
                    damage.index("const char *ms912x_damage_scan_selected_backend_name")]
    dispatch = build / "dispatch.c"
    dispatch.write_text(
        '#include <assert.h>\n#include <stdio.h>\n#include <asm/cpufeature.h>\n'
        '#include "ms912x_scan_methods.h"\n' + enums + '\n'
        'unsigned int bench_cpu_features;\n'
        'static struct {enum ms912x_fast_chroma_backend fast_chroma;} ms912x_parameters;\n'
        + convert + damage + (here / "dispatch_checks.c").read_text()
    )
    compile_source(dispatch, build / "dispatch.o", integer_flags)
    binary = build / "verify"
    subprocess.run([cc, *common, "-I" + str(repo), str(here / "verify.c"),
                    *objects, "-o", str(binary)], check=True)
    if target == "arm32":
        guard_binary = build / "arm32-guard"
        subprocess.run([cc, *common, "-I" + str(repo), *integer_flags,
                        "-marm", "-fomit-frame-pointer", "-nostdlib", "-static",
                        "-Wl,-e,_start", str(here / "arm32_guard.c"),
                        str(build / "neon_glue.o"),
                        str(build / "downscale_neon_glue.o"),
                        str(build / "scan_availability.o"),
                        "-o", str(guard_binary)], check=True)

    if not have_downscale_baseline:
        print("Baseline predates downscale: before column uses scalar reference", flush=True)
    print(f"Baseline: {revision}\nBinary: {binary}", flush=True)
    print(subprocess.check_output([cc, "--version"], text=True).splitlines()[0], flush=True)
    if args.build_only:
        return
    command = [str(binary)]
    if args.arm32:
        subprocess.run(["qemu-arm", "-cpu", "cortex-a7,neon=off,vfp-d32=off",
                        str(guard_binary)], check=True)
        print("ARM32 entry guards pass on QEMU with NEON disabled (no libc)", flush=True)
    if args.arm or args.arm32:
        compiler = Path(shutil.which(cc)).resolve()
        triple = subprocess.check_output([cc, "-dumpmachine"], text=True).strip()
        loaders = (("ld-linux-aarch64.so.1", "ld-musl-aarch64.so.1") if args.arm else
                   ("ld-linux-armhf.so.3", "ld-musl-armhf.so.1", "ld-musl-arm.so.1"))
        candidates = ([args.sysroot] if args.sysroot else [
            Path(subprocess.check_output([cc, "-print-sysroot"], text=True).strip()),
            compiler.parent.parent / triple / "libc", Path("/usr") / triple])
        sysroot = next((p for p in candidates
                        if any((p / "lib" / loader).exists() for loader in loaders)), None)
        if sysroot is None:
            parser.error("Cannot find target libc; pass --sysroot")
        command = ["qemu-aarch64" if args.arm else "qemu-arm",
                   "-cpu", args.cpu or ("cortex-a53" if args.arm else "cortex-a7"),
                   "-L", str(sysroot), *command]
    elif args.cpu:
        command = ["qemu-x86_64", "-cpu", args.cpu, *command]
    if args.bench:
        command.append("--bench")
    environment = dict(os.environ)
    if args.bench:
        environment["MS912X_BENCH_SAMPLES"] = str(build / "samples.csv")
    subprocess.run(command, check=True, env=environment)


if __name__ == "__main__":
    main()
