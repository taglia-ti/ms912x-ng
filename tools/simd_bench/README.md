# Driver SIMD checks and microbenchmarks

This harness compiles the actual driver pixel loops with userspace kernel
shims. It compares the current sources with a Git baseline and checks against
an independent scalar oracle. Build products go into a temporary directory;
the loaded driver and module parameters are not changed.

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
make tests SIMD_ARGS='--arm'
```

Requirements: Python 3, Git, GCC, and optionally `qemu-x86_64`,
`aarch64-linux-gnu-gcc`, and `qemu-aarch64`. The ARM toolchain must include
the target libc. Emulated timing is deliberately rejected; `--bench` measures
native execution only. SIMD FPU guards are compiler barriers in userspace,
so these measurements exclude kernel context management and driver/USB work.

See [RESULTS.md](RESULTS.md) for the saved benchmark results, methods,
validation coverage and hardware limitations.
