#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compare actual driver pixel loops with a Git baseline; no module reload."""

import argparse
import os
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
    parser.add_argument("--arm", action="store_true", help="AArch64 correctness under QEMU")
    parser.add_argument("--cpu", help="QEMU x86 CPU model, correctness only")
    parser.add_argument("--build-only", action="store_true")
    args = parser.parse_args()
    if args.bench and (args.arm or args.cpu):
        parser.error("Emulation cannot measure native CPU performance")

    here = Path(__file__).resolve().parent
    repo = here.parent.parent
    build = (args.build_dir or Path(tempfile.mkdtemp(prefix="ms912x-simd-"))).resolve()
    build.mkdir(parents=True, exist_ok=True)
    baseline = build / "baseline"
    baseline.mkdir(exist_ok=True)
    revision = subprocess.check_output(
        ["git", "rev-parse", args.baseline], cwd=repo, text=True
    ).strip()
    files = subprocess.check_output(
        ["git", "ls-tree", "-r", "--name-only", revision], cwd=repo, text=True
    ).splitlines()
    for name in files:
        path = Path(name)
        if path.parent == Path(".") and name.startswith("ms912x") and path.suffix in (".c", ".h"):
            (baseline / name).write_bytes(subprocess.check_output(
                ["git", "show", f"{revision}:{name}"], cwd=repo
            ))

    cc = "aarch64-linux-gnu-gcc" if args.arm else "gcc"
    arch = "CONFIG_ARM64" if args.arm else "CONFIG_X86_64"
    common = ["-O2", "-g", "-D_GNU_SOURCE", "-fno-tree-vectorize",
              "-fno-tree-slp-vectorize", "-fno-strict-aliasing", "-D" + arch,
              "-include", str(here / "compat.h"), "-I" + str(here / "include"),
              "-Wall", "-Wextra", "-Werror"]
    objects = []

    def compile_source(source, output, extra=(), include=repo):
        subprocess.run([cc, *common, "-I" + str(include), *extra,
                        "-c", str(source), "-o", str(output)], check=True)
        objects.append(str(output))

    backends = ["neon"] if args.arm else ["sse2", "avx2"]
    for backend in backends:
        flags = ["-march=armv8-a+simd"] if args.arm else (
            ["-mavx2", "-mno-avx512f"] if backend == "avx2" else
            ["-msse2", "-mno-ssse3", "-mno-sse4.1", "-mno-avx", "-mno-popcnt"])
        for old in (True, False):
            include = baseline if old else repo
            symbols = [f"ms912x_convert_xrgb8888_to_uyvy_{backend}",
                       f"ms912x_scan_rows_{backend}_blocks",
                       f"ms912x_scan_rows_{backend}_relaxed"]
            rename = [f"-D{s}=old_{s}" for s in symbols] if old else []
            for part in ("", "scan_"):
                name = f"ms912x_{part}{backend}"
                output = build / (("old_" if old else "") + name + ".o")
                compile_source(include / (name + ".c"), output, [*flags, *rename], include)

    if not args.arm:
        compile_source(repo / "ms912x_ssse3.c", build / "ssse3.o",
                       ["-mssse3", "-mno-sse4.1", "-mno-avx", "-mno-popcnt"])
        compile_source(repo / "ms912x_scan_sse41.c", build / "sse41.o",
                       ["-msse4.1", "-mno-sse4.2", "-mno-avx", "-mno-popcnt"])

    integer_flags = ["-mgeneral-regs-only"] if args.arm else [
        "-mno-sse", "-mno-sse2", "-mno-mmx", "-msoft-float", "-mno-popcnt"]
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
    compile_source(repo / "ms912x_scan_methods.c", build / "scan_availability.o")

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
    compile_source(dispatch, build / "dispatch.o")
    binary = build / "verify"
    subprocess.run([cc, *common, "-I" + str(repo), str(here / "verify.c"),
                    *objects, "-o", str(binary)], check=True)
    print(f"Baseline: {revision}\nBinary: {binary}", flush=True)
    print(subprocess.check_output([cc, "--version"], text=True).splitlines()[0], flush=True)
    if args.build_only:
        return
    command = [str(binary)]
    if args.arm:
        compiler = Path(shutil.which(cc)).resolve()
        sysroot = Path(subprocess.check_output([cc, "-print-sysroot"], text=True).strip())
        if not (sysroot / "lib/ld-linux-aarch64.so.1").exists():
            sysroot = compiler.parent.parent / "aarch64-none-linux-gnu/libc"
        command = ["qemu-aarch64", "-L", str(sysroot), *command]
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
