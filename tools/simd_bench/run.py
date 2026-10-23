#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compare actual driver pixel loops with a source baseline; no module reload."""

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
    parser.add_argument("--baseline-dir", type=Path,
                        help="Use a local source snapshot instead of the Git baseline")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--downscale-baseline-dir", type=Path,
                        help="Snapshot for downscale only; allows area/cubic cost comparisons")
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
    def git_output(*arguments):
        try:
            return subprocess.check_output(["git", *arguments], cwd=repo)
        finally:
            os.sync()

    if args.baseline_dir:
        baseline = args.baseline_dir.resolve()
        revision = f"source snapshot {baseline}"
        files = [path.name for path in baseline.iterdir() if path.is_file()]
    else:
        baseline = build / "baseline"
        baseline.mkdir(exist_ok=True)
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
    baseline_masked = "MS912X_SCAN_MASKED_BLOCK_BYTES" in (
        baseline / "ms912x_scan_methods.h").read_text()
    common += ["-DMS912X_BENCH_BASELINE_MASKED=" + str(int(baseline_masked))]
    baseline_avx2 = "ms912x_scan_avx2.c" in files
    common += ["-DMS912X_BENCH_BASELINE_AVX2=" + str(int(baseline_avx2))]
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
            scan_backend = backend
            if backend == "avx2":
                scan_backend = "avx2" if old and baseline_avx2 else "avx"
            scan_label = "avx" if backend == "avx2" else backend
            symbols = [f"ms912x_convert_xrgb8888_to_uyvy_{backend}",
                       f"ms912x_scan_rows_{scan_backend}_blocks",
                       f"ms912x_scan_rows_{scan_backend}_relaxed"]
            destinations = [f"old_ms912x_convert_xrgb8888_to_uyvy_{backend}",
                            f"old_ms912x_scan_rows_{scan_label}_blocks",
                            f"old_ms912x_scan_rows_{scan_label}_relaxed"]
            if backend == "neon":
                symbols += ["ms912x_neon_convert_rows", "ms912x_scan_rows_neon_blocks_fpu",
                            "ms912x_scan_rows_neon_relaxed_fpu"]
                destinations += [f"old_{s}" for s in symbols[3:]]
            rename = [f"-D{s}={d}" for s, d in zip(symbols, destinations)] if old else []
            # The original backend is AArch64-only. Backport its reductions
            # for an arithmetic reference on ARM32; this is not old ARM32 support.
            worker_rename = list(rename)
            if old and target == "arm32":
                worker_rename += ["-include", str(here / "arm32_baseline.h")]
            for part in ("", "scan_"):
                name = f"ms912x_{part}{scan_backend if part else backend}"
                output = build / (("old_" if old else "") + name + ".o")
                part_flags = flags
                if backend == "avx2" and part and scan_backend == "avx":
                    part_flags = ["-mavx", "-mno-avx2", "-mno-avx512f"]
                compile_source(include / (name + ".c"), output,
                               [*part_flags, *worker_rename], include)
            if old and backend == "neon" and (include / "ms912x_neon_glue.c").exists():
                compile_source(include / "ms912x_neon_glue.c", build / "old_neon_glue.o",
                               [*integer_flags, *rename], include)

    if not is_arm:
        compile_source(repo / "ms912x_ssse3.c", build / "ssse3.o",
                       ["-mssse3", "-mno-sse4.1", "-mno-avx", "-mno-popcnt"])

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
    compile_source(here / "scalar.c", build / "old_scalar.o",
                   [*integer_flags, "-Dscalar_blocks=old_scalar_blocks",
                    "-Dscalar_relaxed=old_scalar_relaxed", "-Dscalar_convert=old_scalar_convert"],
                   baseline)
    downscale_baseline = (args.downscale_baseline_dir.resolve()
                          if args.downscale_baseline_dir else baseline)
    downscale_header = next((downscale_baseline / name for name in
                             ("ms912x_downscale_3to2.h", "ms912x_downscale.h")
                             if (downscale_baseline / name).is_file()), None)
    if args.downscale_baseline_dir and downscale_header is None:
        parser.error("Downscale snapshot has no downscale header")
    header_available = (args.downscale_baseline_dir is not None or
                        "ms912x_downscale.h" in files or
                        "ms912x_downscale_3to2.h" in files)
    header_text = downscale_header.read_text() if header_available else ""
    have_downscale_baseline = "ms912x_downscale_xrgb8888_3to2_scalar" in header_text
    def downscale_uses_area(include):
        # Resolve the target's policy, including architecture-dependent filters.
        macros = subprocess.check_output(
            [cc, *common, "-I" + str(include), "-dM", "-E", "-x", "c",
             str(next(include / name for name in
                      ("ms912x_downscale_3to2.h", "ms912x_downscale.h")
                      if (include / name).is_file()))], text=True)
        return "#define MS912X_DOWNSCALE_CATMULL_ROM_Q7 " not in macros

    current_area_downscale = downscale_uses_area(repo)
    area_downscale_baseline = (downscale_uses_area(downscale_baseline)
                               if have_downscale_baseline else current_area_downscale)
    common += ["-DMS912X_BENCH_DOWNSCALE_BASELINE_AREA=" +
               str(int(area_downscale_baseline))]
    for old in (True, False):
        include = downscale_baseline if old and have_downscale_baseline else repo
        modes = ["scalar", "neon"] if is_arm else ["scalar", "sse2", "avx2"]
        symbols = [f"ms912x_downscale_xrgb8888_3to2_{mode}" for mode in modes]
        symbols += [f"ms912x_downscale_xrgb8888_3to2_{mode}_region" for mode in modes]
        symbols += ["ms912x_downscale_xrgb8888_3to2_neon_rows_fpu"]
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
            source = next((include / name for name in
                           (f"ms912x_downscale_3to2_{mode}.c",
                            f"ms912x_downscale_{mode}.c")
                           if (include / name).is_file()),
                          repo / f"ms912x_downscale_3to2_{mode}.c")
            compile_source(source, build / (label + ".o"),
                           [*flags, *rename], include)
        if is_arm and (not old or have_downscale_baseline):
            glue = next((include / name for name in
                         ("ms912x_downscale_3to2_neon_glue.c",
                          "ms912x_downscale_neon_glue.c")
                         if (include / name).is_file()),
                        repo / "ms912x_downscale_3to2_neon_glue.c")
            compile_source(glue,
                           build / (("old_" if old else "") + "downscale_neon_glue.o"),
                           [*integer_flags, *rename], include)
    if not is_arm:
        compile_source(repo / "ms912x_downscale_3to2_ssse3.c",
                       build / "downscale_ssse3.o",
                       ["-mssse3", "-mno-sse4.1", "-mno-avx", "-mno-popcnt"])
        compile_source(repo / "ms912x_downscale_3to2.c",
                       build / "downscale_dispatch.o", integer_flags)
    if not have_downscale_baseline:
        common += ["-DMS912X_DOWNSCALE_SCALAR_BASELINE"]
    compile_source(repo / "ms912x_scan_methods.c", build / "scan_availability.o", integer_flags)

    # Compile the production selectors themselves against synthetic CPUID bits.
    # This exercises unsupported forced modes without executing their SIMD code.
    header = (repo / "ms912x.h").read_text()
    enums = "\n".join(re.search(r"enum " + name + r" \{.*?\};", header, re.S).group()
                      for name in ("ms912x_fast_chroma_backend", "ms912x_damage_scan_method"))
    convert = (repo / "ms912x_convert.c").read_text()
    convert = convert[convert.index("#ifdef CONFIG_X86_64", convert.index('#include "ms912x_pixels.h"')):
                      convert.index("static void ms912x_xrgb_to_uyvy_line")]
    damage = (repo / "ms912x_damage.c").read_text()
    damage = damage[re.search(r"static\s+bool\s+ms912x_damage_scan_simd_method_available\b", damage).start():
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

    before_filter = "area" if area_downscale_baseline else "Catmull-Rom"
    after_filter = "area" if current_area_downscale else "Catmull-Rom"
    if have_downscale_baseline:
        print(f"Downscale before: {before_filter} snapshot {downscale_baseline}; "
              f"after: {after_filter}" +
              (" (different filters)" if before_filter != after_filter else ""), flush=True)
    else:
        print(f"Baseline has no 3->2 downscale: before column uses current "
              f"{after_filter} scalar reference", flush=True)
    print(f"Baseline: {revision}\nBinary: {binary}", flush=True)
    if not is_arm:
        print(f"Scan before: {'AVX2' if baseline_avx2 else 'AVX'}; after: AVX", flush=True)
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
