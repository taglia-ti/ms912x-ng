# Current driver pixel checks

`run.py` builds the current production sources. It does not fetch Git snapshots,
compile historical implementations, substitute a local scalar conversion loop,
or replace the kernel's comparison with a hand-written `memcmp` approximation.
The saved result documents and CSV files in this directory describe historical
experiments; their baseline options belong to the harness at those revisions.

```sh
make -j"$(nproc)" tests
python3 tools/simd_bench/run.py --arm
python3 tools/test_uyvy_pipeline.py --arm
python3 tools/test_downscale_5to4.py --arm32
```

The pixel helper fixture checks the production CPU selectors with synthetic
feature flags and validates the current 3:2 region entry points against an
independent area/Catmull-Rom oracle. It retains crop halos, padding, in-place
output, guard pages, extrema, rounding, scale geometry and cursor bounds cases.
Full-image fixture adapters only supply zero offsets to the same region API used
by capture; they perform no filtering.

`tools/test_uyvy_pipeline.py` checks the production DRM converter, shared scan
backend dispatcher, run construction and packet serializer. Every RGB triple is
checked in both byte orders through the DRM entry point. Wide scan reductions,
row-tail guard pages, all four UYVY lanes, retained baselines, backend fallback
and the 16-row FPU batch boundary exercise the current scanner.

`--cc`, `--sysroot`, `--cpu`, `--build-only`, `--build-dir` and `--sanitize`
remain available. ARM cross checks require the corresponding compiler, target
libc and QEMU. FPU guards are compiler barriers in these userspace fixtures;
they do not establish kernel scheduling or physical display behavior.

Use `python3 tools/simd_bench/run.py --bench` for current 3:2 backend timing.
It reports seven-sample median nanoseconds and input megapixels per second,
without a historical before/after column. Emulated timing is rejected.
The separate `tools/test_uyvy_pipeline.py --benchmark` route experiment is
explicitly opt-in and is not compiled by the correctness suite.

See [the testing map](../../docs/testing.md) for real driver call sites,
fixture boundaries and hardware limitations.
