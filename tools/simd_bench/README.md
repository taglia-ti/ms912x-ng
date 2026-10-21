# Driver SIMD checks and microbenchmarks

This harness compiles the actual driver pixel loops with userspace kernel
shims. It compares the current sources with a Git baseline or source snapshot
and checks against an independent scalar oracle. Build products go into a
temporary directory; the loaded driver and module parameters are not changed.

The native architecture is detected automatically: x86-64, ARM64 or ARM32.

The AVX scan compatibility change and native measurements are recorded in
[AVX_RESULTS.md](AVX_RESULTS.md).

Run native correctness checks:

```sh
make tests
```

Run all native throughput measurements and preserve the seven timing samples
per implementation in `/tmp/ms912x-bench/samples.csv`:

```sh
taskset -c 15 make benchmark SIMD_ARGS='--build-dir /tmp/ms912x-bench'
```

Choose an allowed CPU for `taskset`; the harness pins itself to the first CPU
in its inherited affinity mask. Avoid concurrent benchmarks on that CPU.
The default comparison baseline is `9a4a6ad`. Pass harness options through
`SIMD_ARGS`, for example `make tests SIMD_ARGS='--baseline REF'` to choose
another compatible driver revision. `--build-only` prints the executable path
without running it. Set `PYTHON` to choose another Python interpreter. The
script can also be invoked directly as `python3 tools/simd_bench/run.py`.

Use `--baseline-dir /path/to/snapshot` to compare against saved source files,
including changes that have not been committed. The snapshot must contain the
driver's root C and header files. This option overrides `--baseline`.

The `AVX` scan row compares the previous 256-bit scanner with the current AVX
implementation. The separate `AVX2` row measures chroma conversion. On an AVX-only
CPU, an AVX2 scan baseline is skipped while the current scanner is still checked
against the independent oracle.

Check older x86 instruction sets using QEMU user emulation:

```sh
make tests SIMD_ARGS='--cpu Conroe'
make tests SIMD_ARGS='--cpu Penryn'
make tests SIMD_ARGS='--cpu phenom'
make tests SIMD_ARGS='--cpu SandyBridge'
make tests SIMD_ARGS='--cpu IvyBridge'
```

The binary skips instruction sets not exposed by the CPU model. The dispatch
matrix separately checks the actual production selectors with synthetic
feature combinations, unsupported forced modes, and SIMD availability.

Check AArch64 NEON with an installed GNU cross compiler and QEMU:

```sh
make tests SIMD_ARGS='--arm --cpu cortex-a53'
make tests SIMD_ARGS='--arm32 --cpu cortex-a7'
```

ARM32 also runs a freestanding guard check on an emulated Cortex-A7 with
NEON disabled. It verifies that the actual integer-only availability and
conversion wrappers return without entering a worker. Avoiding libc is needed
because some cross toolchains build libc itself with NEON. Native ARM32 tests
skip the NEON backend when the OS does not advertise `HWCAP_NEON`.

The old ARM64 NEON code has no ARM32 implementation. For ARM32 comparison only,
`arm32_baseline.h` supplies equivalent pairwise reductions for its A64-only
intrinsics; it is an arithmetic reference, not evidence of previous ARM32
kernel support. The current backend is compiled without that adapter.

Use `--cc` to select a cross compiler and `--sysroot` to locate its libc for
QEMU. To cross-build a benchmark for execution on actual ARM hardware:

```sh
make tests SIMD_ARGS='--arm --build-only --cc /path/to/aarch64-gcc --baseline 2469f89 --build-dir /tmp/ms912x-arm'
scp /tmp/ms912x-arm/verify root@board:/root/ms912x-simd-verify
ssh root@board 'MS912X_BENCH_SAMPLES=/root/samples.csv taskset -c 3 /root/ms912x-simd-verify --bench'
```

Choose a CPU present on the board. `--arm32 --build-only` builds for ARM32.
`MS912X_BENCH_BACKEND=NEON` restricts timing to NEON while retaining all
correctness checks; omit it to measure scalar and memcmp controls as well.
`make benchmark` works directly on native ARM64/ARM32 with a native compiler.

Requirements: Python 3, Git, GCC, and optionally `qemu-x86_64`,
`aarch64-linux-gnu-gcc`, `arm-linux-gnueabihf-gcc`, `qemu-aarch64`, and `qemu-arm`. The ARM toolchain must include
the target libc. Emulated timing is deliberately rejected; `--bench` measures
native execution only. SIMD FPU guards are compiler barriers in userspace,
so these measurements exclude kernel context management and driver/USB work.

See [RESULTS.md](RESULTS.md) for the saved benchmark results, methods,
validation coverage and hardware limitations.

See [NEON_RESULTS.md](NEON_RESULTS.md) for native Cortex-A53 results and
ARM32/ARM64 compatibility checks.

## Downscale checks and timing

The 3→2 (1.5×) filter is fixed-phase Catmull-Rom on x86-64 and area on
ARM32/ARM64, including scalar fallbacks. The harness checks all four channels
against an independent scalar oracle, the selected filter's rounding/clipping, odd edges, strides, in-place and packed output, empty sizes,
guard pages, extrema and staged crops with their leading/trailing halo.

`--baseline REF` still selects the scan and chroma Git baseline. If that
revision has the 3→2 filter, its downscale implementation is also compared;
otherwise the downscale before column explicitly uses current scalar. A 2×
filter cannot serve as a matching output reference. Each implementation is
validated against its own area or cubic oracle, using the target architecture's
preprocessed header policy. Such timing comparisons measure cost of different
filters, not equivalent-output optimization speedup.

Use `--downscale-baseline-dir /path/to/snapshot` to compare with a saved
source tree for downscale only. The preserved `area_3to2_baseline.patch`,
applied to `c5b4e9c`, reconstructs the exact pre-cubic area sources used here.
See [CUBIC_RESULTS.md](CUBIC_RESULTS.md) for reconstruction commands.
[ARM_AREA_RESULTS.md](ARM_AREA_RESULTS.md) provides the pre-selection cubic
snapshot, repeat measurements and the final ARM selection.

```sh
MS912X_BENCH_DOWNSCALE_ONLY=1 taskset -c 15 make benchmark \
  SIMD_ARGS='--baseline c5b4e9c --downscale-baseline-dir /tmp/area --build-dir /tmp/ms912x-downscale'
```

`MS912X_BENCH_DOWNSCALE_ONLY=1` limits timing to downscale, retaining all
correctness checks. `MS912X_BENCH_BACKEND=SSE2`, `AVX2`, `NEON`, or `scalar`
selects one timing backend. Select an available CPU. Six sizes, separate buffers
and fixed-stride in-place output are measured with seven before/after samples.
Source Mpixel/s and filter milliseconds per call exclude kernel FPU context
management and USB/display work. `pixels` in raw CSVs counts nominal source
geometry pixels across a batch, not individual filter taps.

On x86-64, SSSE3 downscale kernels use `PMADDUBSW` for both the 3→2 and 5→4
Catmull-Rom filters. AVX2 remains the preferred backend; SSSE3 is selected
before SSE2 when AVX2 is unavailable. The harness compares 3→2 SSSE3 directly
with SSE2 and checks 5→4 SSSE3 against the independent filter oracle. See
[SSSE3_DOWNSCALE_RESULTS.md](SSSE3_DOWNSCALE_RESULTS.md) for native timings.

[DOWNSCALE_RESULTS.md](DOWNSCALE_RESULTS.md) and its saved CSV/log artifacts
record the historical 2× filter. [DOWNSCALE_3TO2.md](DOWNSCALE_3TO2.md) describes
the current algorithm, and [CUBIC_RESULTS.md](CUBIC_RESULTS.md) records the
original cubic experiment relative to the 1.5× area filter; [ARM_AREA_RESULTS.md](ARM_AREA_RESULTS.md)
records the selected ARM area implementation.
