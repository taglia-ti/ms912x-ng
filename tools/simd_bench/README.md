# Driver SIMD checks and microbenchmarks

This harness compiles the actual driver pixel loops with userspace kernel
shims. It compares the current sources with a Git baseline and checks against
an independent scalar oracle. Build products go into a temporary directory;
the loaded driver and module parameters are not changed.

The native architecture is detected automatically: x86-64, ARM64 or ARM32.

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

Check older x86 instruction sets using QEMU user emulation:

```sh
make tests SIMD_ARGS='--cpu Conroe'
make tests SIMD_ARGS='--cpu Penryn'
make tests SIMD_ARGS='--cpu phenom'
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

Downscale correctness checks run with `make tests`: exact rounding, all four
byte channels, odd edges, padded strides, in-place and packed output, empty
sizes and guard pages at individual row boundaries.

Choose a baseline that includes downscale to compare each backend with its
previous implementation. The default `9a4a6ad` predates downscale; for that
baseline the harness explicitly labels the downscale before column as a scalar
reference. To reproduce the downscale optimization comparison:

```sh
MS912X_BENCH_DOWNSCALE_ONLY=1 taskset -c 15 make benchmark \
  SIMD_ARGS='--baseline 8d431ef --build-dir /tmp/ms912x-downscale'
```

`MS912X_BENCH_DOWNSCALE_ONLY=1` limits timing to downscale, while retaining all
correctness checks. `MS912X_BENCH_BACKEND=SSE2`, `AVX2`, `NEON`, or `scalar`
selects a single timing backend. Select a CPU available on your machine.

The benchmark measures six sizes using separate buffers and fixed-stride
in-place output, with seven samples before/after each change. It reports
source Mpixel/s and filter milliseconds per call; neither includes kernel
FPU context management or USB/display work. `pixels` in the raw CSV counts
source geometry pixels across all calls in a timed batch. Detailed methods,
results, raw samples and validation are in
[DOWNSCALE_RESULTS.md](DOWNSCALE_RESULTS.md).
