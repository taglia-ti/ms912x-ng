# ARM area versus cubic: native NEON comparison

Measured on 2026-10-19 after the user requested cubic on x86-64 and the former
area filter on NEON if the performance difference was substantial.

## Decision and scope

Keep Catmull-Rom on x86-64 (scalar/SSE2/AVX2), and area on ARM32/ARM64
(NEON and scalar). Filter selection is architectural, so temporarily disabling
SIMD cannot change the image. Both filters retain 1.5× scaling, existing buffer
ownership, replicated borders and four byte channels. The restored area worker
adds source offsets for the current staged-damage API.

These are different filters: the lower area cost comes with the previous
softer contours. This is a quality/performance choice, not an equivalent-output
SIMD optimization. No new full-frame buffer or line cache was introduced.

## Main results

BPI-R3 / MT7986 Cortex-A53, OpenWrt 25.12.5, Linux 6.12.94, OpenWrt GCC
14.3.0, pinned CPU 3. Cross-built with the prepared `/opt/openwrt` toolchain
and executed natively on the board; no emulated throughput is reported.

For 1920×1080 → 1280×720 in the same buffer, matching the frame/cursor callers:

- Pre-selection cubic: **18.723 ms**, 110.8 source Mpixel/s.
- Selected area: **7.090 ms**, 292.4 source Mpixel/s.
- **2.64× throughput**, **62.1% less filter time**, about **11.63 ms saved**.
- At 60 complete downscales/s, filter work alone corresponds to approximately
  112.3% versus 42.5% of one core. These are compute-budget estimates, not
  measured display FPS or end-to-end frame times.

With separate buffers the measured filter time fell from 22.417 to 7.573 ms
(2.96×). In-place 1366×768 fell from 10.008 to 3.875 ms (2.58×).
The full-frame SIMD section count also falls from 113 to 45; the associated
kernel context cost is excluded from these userspace timings.

Before changing the sources, an independent rerun of the original dual-filter
binary observed 18.911 ms cubic versus 7.093 ms area in-place at Full HD
(2.67×). The selected-code run above agrees with this and the original
[CUBIC_RESULTS.md](CUBIC_RESULTS.md) measurements. x86 SIMD arithmetic and
scheduling were not changed by this selection; that report retains its timings.

## Timing method and limitations

Twelve NEON cases, seven samples per filter per case, 168 raw batch durations
in each of two runs. Before changing sources, `before` meant original area
and `after` meant cubic. In the final run, `before` means the preserved cubic
snapshot and `after` means the selected area code. Do not merge those columns
without respecting their labels.

The harness warms both filters, alternates execution order and reports medians.
Six source/output geometries, separate buffers and fixed-stride in-place output
are timed. Both strides are 1,920 pixels. Batch lengths are 24 Full HD calls,
64 for the other large sizes, 2,048 at 128×128 and 32,768 for smaller cases.
An in-place buffer reset precedes each batch and is outside timing; repeated
calls overwrite the same storage. This is steady-state loop timing, not an
independent fresh image for every call. Packed output is tested, not timed.

Mpixel/s counts nominal source geometry pixels. Frequencies, thermals and
background activity were not controlled; medians are observations from these
runs, not confidence intervals. SIMD guards are compiler barriers here.
Real kernel FPU state management, scan, chroma conversion, staging/copies,
allocation, USB and display presentation are excluded. Measurements apply to
this Cortex-A53, not all ARM CPUs. Visual quality on the adapter was not tested.
The module was not installed/reloaded. R3 validation here uses userspace
pixel loops; the previously deferred R3 module deployment remains pending.

- [Selected-code raw samples](results-arm-area-a53.csv): cubic → area.
- [Pre-change repeat raw samples](results-arm-area-recheck-a53.csv): area → cubic.
- [Validation and build logs](ARM_AREA_VALIDATION.txt).

## All final NEON measurements

| Source → output | Storage | Cubic ms | Area ms | Area source Mpixel/s | Cubic / area time |
| --- | --- | ---: | ---: | ---: | ---: |
| 1920×1080 → 1280×720 | separate | 22.417237 | 7.572766 | 273.8 | 2.96× |
| 1920×1080 → 1280×720 | same buffer | 18.722756 | 7.090497 | 292.4 | 2.64× |
| 1366×768 → 912×512 | separate | 11.556018 | 4.164038 | 251.9 | 2.78× |
| 1366×768 → 912×512 | same buffer | 10.007957 | 3.875137 | 270.7 | 2.58× |
| 1280×720 → 856×480 | separate | 10.391528 | 3.699853 | 249.1 | 2.81× |
| 1280×720 → 856×480 | same buffer | 9.104117 | 3.507832 | 262.7 | 2.60× |
| 128×128 → 88×86 | separate | 0.208802 | 0.049349 | 332.0 | 4.23× |
| 128×128 → 88×86 | same buffer | 0.161681 | 0.048679 | 336.6 | 3.32× |
| 33×35 → 24×24 | separate | 0.033544 | 0.004286 | 269.5 | 7.83× |
| 33×35 → 24×24 | same buffer | 0.019808 | 0.004089 | 282.5 | 4.84× |
| 7×5 → 5×4 | separate | 0.002942 | 0.000268 | 130.4 | 10.96× |
| 7×5 → 5×4 | same buffer | 0.001482 | 0.000248 | 141.2 | 5.98× |

## Validation

- Native x86: cubic compared to the preserved cubic snapshot; scalar, SSE2,
  AVX2 and existing scan/chroma/dispatch checks pass. The default Git-baseline
  scalar fallback comparison also passes.
- Native R3: both filters match their respective independent oracles over
  263 geometries; current area passes 261,121 channel patterns, 256 extrema
  patterns, guard pages for individual rows, odd dimensions, fixed/packed
  in-place output and 144 staged crops. SIMD-unavailable fallbacks match area.
- ARM32 Cortex-A7 under QEMU passes the same checks and the freestanding
  NEON-disabled guard. ARM64 Cortex-A53 QEMU also checks the reconstructed
  cubic snapshot against current area. Emulation is used for correctness only.
- Full x86-64 module build passes. Downscale dispatcher, scalar, NEON worker
  and glue compile as ARM64/Linux 6.12.94 and ARM32/Linux 7.2.11 kernel objects.
  These object checks do not establish R3 loadability.
- ARM kernel object inspection confirms integer-only dispatcher/scalar/glue,
  rounded reciprocal multiplication in the area worker and no hardware divide.

## Reproducing the cubic baseline

The cubic experiment was uncommitted. The preserved
[cubic snapshot patch](cubic_3to2_baseline.patch), applied to
`c5b4e9c8236e995c1ca50384a5ea62e3c2b71bc7`, reconstructs its ten downscale
source/header files byte for byte. Hashes are in the validation log. The
existing area snapshot remains available in `area_3to2_baseline.patch`.

```sh
mkdir -p /tmp/ms912x-cubic-baseline
git archive --output=/tmp/ms912x-cubic-baseline.tar c5b4e9c
sync
tar -xf /tmp/ms912x-cubic-baseline.tar -C /tmp/ms912x-cubic-baseline
patch -d /tmp/ms912x-cubic-baseline -p1 -i "$PWD/tools/simd_bench/cubic_3to2_baseline.patch"
sync
make tests SIMD_ARGS='--baseline c5b4e9c --downscale-baseline-dir /tmp/ms912x-cubic-baseline'
```

On native ARM, time NEON only (select an available CPU):

```sh
MS912X_BENCH_DOWNSCALE_ONLY=1 MS912X_BENCH_BACKEND=NEON \
taskset -c 3 make benchmark \
  SIMD_ARGS='--baseline c5b4e9c --downscale-baseline-dir /tmp/ms912x-cubic-baseline --build-dir /tmp/ms912x-arm-area'
```

For the R3, cross-build with `--arm --build-only --cc /path/to/aarch64-gcc`,
copy the printed binary and execute it natively with the environment above,
`MS912X_BENCH_SAMPLES=/root/samples.csv`, and `--bench`. The harness resolves
filter policy through the target compiler's preprocessor, so snapshots with
architecture-dependent policies are checked against the correct oracle.
