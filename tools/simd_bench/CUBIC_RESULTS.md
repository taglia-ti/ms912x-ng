# Catmull-Rom versus area: native 3-to-2 filter costs

## Current selection

This report records the original all-architecture cubic experiment. The driver
now keeps cubic on x86-64 and area on ARM32/ARM64, including scalar fallbacks.
See [ARM_AREA_RESULTS.md](ARM_AREA_RESULTS.md) for the follow-up measurements
and reproducible cubic snapshot. Historical timings below are unchanged.

## Scope

Measured on 2026-10-19. **These are different filters at the same 1.5× scale.**
The before column is the actual previous 3→2 area implementation, captured
before changing the uncommitted sources. The after column is separable
Catmull-Rom with exact Q7 phases and an 8-bit clipped intermediate. This is a
quality/cost experiment, not an equivalent-output optimization speedup.
Both implementations are checked against their respective independent oracles.

The same output geometry retains the same full-frame pixel count. Real USB
damage volume can still change with the filter and expanded partial-update
support. The experiment does not measure conversion, USB or display latency.
The module was compiled, not installed/reloaded for these measurements.
Visual improvement on the user's actual adapter remains unverified.

- x86-64: AMD Ryzen 9 5900XT, Linux 7.2.11-arch1-1, GCC 16.2.1
  (20260810), pinned CPU 15.
- ARM64: BPI-R3 / MT7986 Cortex-A53, OpenWrt 25.12.5 revision
  r33051-f5dae5ece4, Linux 6.12.94, OpenWrt GCC 14.3.0, pinned CPU 3.
  The binary was cross-built using the prepared `/opt/openwrt` toolchain
  and executed natively on the board.
- ARM32 and older x86: correctness under QEMU and ARM kernel object builds;
  no emulated throughput is reported as native performance.

## Main observations

For 1920×1080 → 1280×720 with the source/destination pointer shared, matching
full-frame and cursor use in the driver:

| Machine / backend | Area ms | Catmull-Rom ms | Cost factor |
| --- | ---: | ---: | ---: |
| Ryzen / scalar | 4.334 | 16.924 | 3.90× |
| Ryzen / SSE2 | 0.492 | 1.961 | 3.99× |
| Ryzen / AVX2 | 0.326 | 0.628 | 1.92× |
| R3 / scalar | 20.707 | 101.567 | 4.91× |
| R3 / NEON | 7.158 | 19.072 | 2.66× |

On this Ryzen, AVX2 adds about 0.30 ms per Full HD image. On the R3, NEON
adds about 11.91 ms and the filter alone exceeds the 16.67 ms budget for 60
updates/s. The latter is a compute-budget comparison, not a measured display
FPS. Real kernel FPU costs and the rest of the driver add further work.

Separate-buffer Full HD costs were 0.497 → 2.907 ms SSE2,
0.333 → 0.951 ms AVX2 and 7.421 → 22.375 ms NEON. The direct separate-buffer
path repeats some horizontal work across neighboring output row pairs to
avoid allocating an intermediate image. In-place input uses two full passes
in the existing storage. Tiny rectangles spend proportionally more time in
scalar border handling; the tables show this rather than hiding those cases.

## Algorithm and implementation

See [DOWNSCALE_3TO2.md](DOWNSCALE_3TO2.md) for the exact contract and scheduling.
The phases use `[-9,111,29,-3]/128` and its reverse. Each axis rounds and clips
to bytes. Negative lobes can strengthen contours and may also introduce
ringing/aliasing; no sharpening or scale-expanded antialiasing pass is added.

SSE2 uses four-pixel vectors with signed 16-bit sums after centering inputs
at 128. AVX2 uses eight-pixel vectors and byte multiply-add pairings whose
individual sums cannot saturate. NEON uses four-pixel vectors with widening
byte multiply/accumulate. No heap allocation or extra full-image copy is
introduced. Shared scheduling preserves same-buffer and packed output, and
staged crops include their leading/trailing halo and source phase offsets.
FPU sections are bounded to 16 rows of the current pass; in-place Full HD
uses 68 horizontal plus 45 vertical sections, versus 45 area sections.

## Timing method

Seven samples per implementation per case, alternating execution order after
warmup. Report independent medians. The 36 x86 and 24 R3 rows each retain 14
raw elapsed nanosecond samples. Frequencies, temperatures and background
activity were not fixed; these are observations from one run, not confidence
intervals. Both filters use identical geometry, buffers, strides and iteration
counts within each comparison.

All timing uses a 1,920-pixel source/destination stride. Timed batches contain
24 calls at 1920×1080, 64 at the other two large sizes, 2,048 at 128×128, and
32,768 at the two smallest sizes. In-place buffers are reset outside each
batch, then repeatedly overwritten. This is warm steady-state timing, not a
fresh independent image for each call. The working-buffer reset is excluded.
Packed output is validated separately but is not the timed in-place layout.

**Mpixel/s counts nominal source geometry pixels**, not individual taps or
memory accesses. Both dimensions shrink by 2/3 before alignment/padding, so
output pixel counts are about 4/9 of the input. SIMD FPU guards are compiler
barriers in the userspace shim: real context save/restore, DMA/USB, conversion,
scanning, allocation by surrounding driver code and display presentation are
excluded. The extra FPU sections of the cubic path therefore have unmeasured
kernel context costs.

- [x86 raw samples](results-cubic-x86.csv)
- [R3 raw samples](results-cubic-a53.csv)
- [validation/build log](CUBIC_VALIDATION.txt)
- [text illustration](cubic-text-comparison.png): actual driver downscalers,
  grayscale DejaVu Sans Mono text, followed by a **simulated bilinear upscale**.
  It excludes UYVY and subpixel rendering and is not an adapter screen capture.

## Validation

- Native x86 and BPI-R3: independent signed scalar oracle, 263 geometries,
  odd dimensions, padded strides, separate and shared source/destination,
  packed output, empty dimensions, all four byte channels.
- 261,121 cubic output patterns; individual source/output row guard pages at
  widths 1..129 and heights 1..5; 144 staged crops with source offsets/halos.
- Additional final native checks: 256 binary extrema patterns, clipping and
  overshoot, all channels and packed in-place output.
- QEMU Conroe/SSE2 and Cortex-A7/ARM32 pass the geometry, rounding, guard-page
  and crop checks. ARM32 also passes the freestanding NEON-disabled guard.
- Existing scan/chroma/dispatch/framing checks and the default pre-downscale
  Git-baseline fallback passed.
- Full x86-64 module build passed. Kbuild cross-compilation of downscale
  dispatcher, scalar, NEON worker and glue passed on ARM64/Linux 6.12.94 and
  ARM32/Linux 7.2.11. These are object checks, not a loadable R3 module claim.
- Kernel object inspection confirms integer-only public/glue entries, FPU
  boundaries, SSE2 without AVX, no AVX-512, AVX2 packed byte multiply-add,
  and ARM64 widening arithmetic/saturating byte packing.

## Reproducing the exact area baseline

The area filter was uncommitted when this experiment started. The checked-in
[area baseline patch](area_3to2_baseline.patch), applied to
`c5b4e9c8236e995c1ca50384a5ea62e3c2b71bc7`, reconstructs all seven relevant
filter files byte for byte. Reconstruction and their SHA-256 hashes are
recorded in the validation log. Historical 2× baselines are not used here.

```sh
mkdir -p /tmp/ms912x-area
git archive --output=/tmp/ms912x-area.tar c5b4e9c
sync
tar -xf /tmp/ms912x-area.tar -C /tmp/ms912x-area
patch -d /tmp/ms912x-area -p1 -i "$PWD/tools/simd_bench/area_3to2_baseline.patch"
sync
make tests SIMD_ARGS='--baseline c5b4e9c --downscale-baseline-dir /tmp/ms912x-area'
MS912X_BENCH_DOWNSCALE_ONLY=1 taskset -c 15 make benchmark \
  SIMD_ARGS='--baseline c5b4e9c --downscale-baseline-dir /tmp/ms912x-area --build-dir /tmp/ms912x-cubic'
```

Choose an available CPU. These commands now compare against the selected
architecture filter; reconstruct the cubic snapshot documented in
[ARM_AREA_RESULTS.md](ARM_AREA_RESULTS.md) to benchmark cubic on ARM. For the R3 use `--arm --build-only` with the OpenWrt
AArch64 musl compiler, copy the resulting binary, then execute it natively
with `MS912X_BENCH_DOWNSCALE_ONLY=1`, `MS912X_BENCH_SAMPLES=/root/samples.csv`
and `taskset -c 3`. QEMU is used for correctness only.

## All measured cases

### Ryzen 9 5900XT, CPU 15

| Backend | Source → output | Storage | Area ms | Cubic ms | Area Mpixel/s | Cubic Mpixel/s | Cubic / area time |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| scalar | 1920×1080 → 1280×720 | separate | 4.342841 | 23.772078 | 477.5 | 87.2 | 5.47× |
| SSE2 | 1920×1080 → 1280×720 | separate | 0.496537 | 2.907360 | 4176.1 | 713.2 | 5.86× |
| AVX2 | 1920×1080 → 1280×720 | separate | 0.332724 | 0.950751 | 6232.2 | 2181.0 | 2.86× |
| scalar | 1920×1080 → 1280×720 | in place | 4.333954 | 16.923796 | 478.5 | 122.5 | 3.90× |
| SSE2 | 1920×1080 → 1280×720 | in place | 0.492427 | 1.960661 | 4211.0 | 1057.6 | 3.98× |
| AVX2 | 1920×1080 → 1280×720 | in place | 0.326204 | 0.627675 | 6356.8 | 3303.6 | 1.92× |
| scalar | 1366×768 → 912×512 | separate | 2.199452 | 12.033978 | 477.0 | 87.2 | 5.47× |
| SSE2 | 1366×768 → 912×512 | separate | 0.257774 | 1.489002 | 4069.8 | 704.6 | 5.78× |
| AVX2 | 1366×768 → 912×512 | separate | 0.174643 | 0.510076 | 6007.0 | 2056.7 | 2.92× |
| scalar | 1366×768 → 912×512 | in place | 2.197787 | 8.585501 | 477.3 | 122.2 | 3.91× |
| SSE2 | 1366×768 → 912×512 | in place | 0.256673 | 1.004227 | 4087.2 | 1044.7 | 3.91× |
| AVX2 | 1366×768 → 912×512 | in place | 0.174032 | 0.335277 | 6028.1 | 3129.0 | 1.93× |
| scalar | 1280×720 → 856×480 | separate | 1.941977 | 10.598606 | 474.6 | 87.0 | 5.46× |
| SSE2 | 1280×720 → 856×480 | separate | 0.233066 | 1.358057 | 3954.3 | 678.6 | 5.83× |
| AVX2 | 1280×720 → 856×480 | separate | 0.160240 | 0.456219 | 5751.4 | 2020.1 | 2.85× |
| scalar | 1280×720 → 856×480 | in place | 1.936125 | 7.571623 | 476.0 | 121.7 | 3.91× |
| SSE2 | 1280×720 → 856×480 | in place | 0.232648 | 0.913894 | 3961.4 | 1008.4 | 3.93× |
| AVX2 | 1280×720 → 856×480 | in place | 0.158867 | 0.305835 | 5801.1 | 3013.4 | 1.93× |
| scalar | 128×128 → 88×86 | separate | 0.036097 | 0.195020 | 453.9 | 84.0 | 5.40× |
| SSE2 | 128×128 → 88×86 | separate | 0.005797 | 0.038350 | 2826.2 | 427.2 | 6.62× |
| AVX2 | 128×128 → 88×86 | separate | 0.004648 | 0.023359 | 3524.9 | 701.4 | 5.03× |
| scalar | 128×128 → 88×86 | in place | 0.035801 | 0.139541 | 457.6 | 117.4 | 3.90× |
| SSE2 | 128×128 → 88×86 | in place | 0.005625 | 0.023589 | 2912.6 | 694.5 | 4.19× |
| AVX2 | 128×128 → 88×86 | in place | 0.004387 | 0.013004 | 3735.1 | 1259.9 | 2.96× |
| scalar | 33×35 → 24×24 | separate | 0.002733 | 0.014875 | 422.7 | 77.6 | 5.44× |
| SSE2 | 33×35 → 24×24 | separate | 0.000536 | 0.006053 | 2155.6 | 190.8 | 11.30× |
| AVX2 | 33×35 → 24×24 | separate | 0.000429 | 0.005198 | 2689.2 | 222.2 | 12.10× |
| scalar | 33×35 → 24×24 | in place | 0.002759 | 0.010775 | 418.6 | 107.2 | 3.91× |
| SSE2 | 33×35 → 24×24 | in place | 0.000545 | 0.003304 | 2121.0 | 349.5 | 6.07× |
| AVX2 | 33×35 → 24×24 | in place | 0.000429 | 0.002618 | 2695.3 | 441.2 | 6.11× |
| scalar | 7×5 → 5×4 | separate | 0.000105 | 0.000540 | 332.5 | 64.9 | 5.13× |
| SSE2 | 7×5 → 5×4 | separate | 0.000037 | 0.000528 | 942.9 | 66.2 | 14.24× |
| AVX2 | 7×5 → 5×4 | separate | 0.000036 | 0.000532 | 969.1 | 65.8 | 14.74× |
| scalar | 7×5 → 5×4 | in place | 0.000102 | 0.000359 | 344.7 | 97.6 | 3.53× |
| SSE2 | 7×5 → 5×4 | in place | 0.000037 | 0.000261 | 938.4 | 134.2 | 6.99× |
| AVX2 | 7×5 → 5×4 | in place | 0.000036 | 0.000372 | 974.7 | 94.0 | 10.37× |

### BPI-R3 Cortex-A53, CPU 3

| Backend | Source → output | Storage | Area ms | Cubic ms | Area Mpixel/s | Cubic Mpixel/s | Cubic / area time |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| scalar | 1920×1080 → 1280×720 | separate | 20.663292 | 138.488253 | 100.4 | 15.0 | 6.70× |
| NEON | 1920×1080 → 1280×720 | separate | 7.421074 | 22.375051 | 279.4 | 92.7 | 3.02× |
| scalar | 1920×1080 → 1280×720 | in place | 20.707122 | 101.567125 | 100.1 | 20.4 | 4.90× |
| NEON | 1920×1080 → 1280×720 | in place | 7.158375 | 19.072189 | 289.7 | 108.7 | 2.66× |
| scalar | 1366×768 → 912×512 | separate | 10.703058 | 70.526292 | 98.0 | 14.9 | 6.59× |
| NEON | 1366×768 → 912×512 | separate | 4.057929 | 11.503325 | 258.5 | 91.2 | 2.83× |
| scalar | 1366×768 → 912×512 | in place | 10.583094 | 51.621276 | 99.1 | 20.3 | 4.88× |
| NEON | 1366×768 → 912×512 | in place | 3.847452 | 9.942147 | 272.7 | 105.5 | 2.58× |
| scalar | 1280×720 → 856×480 | separate | 9.392057 | 61.822575 | 98.1 | 14.9 | 6.58× |
| NEON | 1280×720 → 856×480 | separate | 3.635708 | 10.364220 | 253.5 | 88.9 | 2.85× |
| scalar | 1280×720 → 856×480 | in place | 9.259026 | 45.378142 | 99.5 | 20.3 | 4.90× |
| NEON | 1280×720 → 856×480 | in place | 3.482240 | 9.084767 | 264.7 | 101.4 | 2.61× |
| scalar | 128×128 → 88×86 | separate | 0.145306 | 1.075036 | 112.8 | 15.2 | 7.40× |
| NEON | 128×128 → 88×86 | separate | 0.049109 | 0.207772 | 333.6 | 78.9 | 4.23× |
| scalar | 128×128 → 88×86 | in place | 0.144336 | 0.794842 | 113.5 | 20.6 | 5.51× |
| NEON | 128×128 → 88×86 | in place | 0.048810 | 0.161506 | 335.7 | 101.4 | 3.31× |
| scalar | 33×35 → 24×24 | separate | 0.011150 | 0.081650 | 103.6 | 14.1 | 7.32× |
| NEON | 33×35 → 24×24 | separate | 0.004157 | 0.033433 | 277.9 | 34.5 | 8.04× |
| scalar | 33×35 → 24×24 | in place | 0.011040 | 0.060276 | 104.6 | 19.2 | 5.46× |
| NEON | 33×35 → 24×24 | in place | 0.004046 | 0.019799 | 285.4 | 58.3 | 4.89× |
| scalar | 7×5 → 5×4 | separate | 0.000412 | 0.002917 | 84.9 | 12.0 | 7.07× |
| NEON | 7×5 → 5×4 | separate | 0.000248 | 0.002943 | 141.2 | 11.9 | 11.88× |
| scalar | 7×5 → 5×4 | in place | 0.000414 | 0.002002 | 84.6 | 17.5 | 4.84× |
| NEON | 7×5 → 5×4 | in place | 0.000229 | 0.001480 | 152.7 | 23.6 | 6.46× |
