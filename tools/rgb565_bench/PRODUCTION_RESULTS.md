# RGB565 production encoder and pipeline measurements

Measured on 2026-10-21 from the working tree based on `aab94b1`, including the
scalar/SSE2/NEON mask paths, bounded inline tails, incremental row pointers
and the corrected benchmark. Driver functions are compiled directly or
extracted verbatim; selection, rectangle validation,
framing and public batch encoding are included. No synthetic mask candidates
are timed. Function and source hashes accompany each result set.

Seven samples per case; medians of elapsed time / iterations. CPUs are pinned,
clocks are not fixed. **CPU timings only**, excluding actual kernel FPU state
save/restore, locks, scheduling, USB, rendering and display refresh. Memory
operations use userspace libc. All pipeline cases use **scale=100**.

## Encoding only

Times include the real batch encoder for one rectangle. `RGB565-auto` selects
SSE2 on x86-64 and NEON on A53; `UYVY-auto` selects AVX2 and NEON respectively.
The RGB565 scalar control disables SIMD; UYVY scalar sets `fast_chroma=scalar`.
RGB565 stays packed at 16 bits/pixel. Both wire formats use two bytes/pixel.

| Target | Cache | Rectangle | RGB565 scalar (µs) | RGB565 auto (µs) | Mask CPU time reduction | UYVY auto (µs) |
|---|---|---|---:|---:|---:|---:|
| x86 | reused-1 | 1920x1080 | 135.142 | 79.004 | 41.5% | 379.514 |
| x86 | reused-1 | 1280x720 | 71.883 | 40.832 | 43.2% | 175.568 |
| x86 | reused-1 | 640x128 | 7.511 | 3.619 | 51.8% | 16.960 |
| x86 | reused-1 | 64x64 | 0.404 | 0.208 | 48.5% | 1.171 |
| x86 | rotating-8 | 1920x1080 | 247.054 | 229.993 | 6.9% | 458.189 |
| x86 | rotating-8 | 1280x720 | 177.067 | 103.286 | 41.7% | 285.553 |
| a53 | reused-1 | 1920x1080 | 2933.146 | 3103.900 | -5.8% | 6535.538 |
| a53 | reused-1 | 1280x720 | 1568.858 | 1693.038 | -7.9% | 3280.685 |
| a53 | reused-1 | 640x128 | 70.070 | 41.192 | 41.2% | 256.960 |
| a53 | reused-1 | 64x64 | 4.535 | 2.774 | 38.8% | 12.811 |
| a53 | rotating-8 | 1920x1080 | 2904.476 | 3078.000 | -6.0% | 6541.015 |
| a53 | rotating-8 | 1280x720 | 1571.354 | 1684.440 | -7.2% | 3271.504 |

SSE2 reduces the x86 mask cost substantially when the data is reused; the gain
shrinks for the rotating working set. On A53, NEON helps cache-resident medium
rectangles but does not consistently improve full-frame memory traffic. Negative
reductions denote a slower SIMD median. Real kernel FPU overhead can further
change the crossover for small rectangles.

## CPU pipeline: capture, scan/baseline, staging and batch encoding

The formats receive matched images and update the same areas. XRGB tolerance is
3; RGB565 tolerance is 0, retaining the mandatory wire-mask bit exclusions.
Automatic paths use SIMD scan: AVX1 on x86, NEON on A53. The separate
`RGB565-all-scalar` CSV path disables SIMD for **both scan and masking** and
is a scalar control rather than an isolated mask comparison.

Reduction = `1 - RGB565_auto / XRGB_UYVY_auto`.

### AMD Ryzen 9 5900XT

x86-64, CPU 15 (SMT sibling 31), performance governor, GCC 16.2.1; Linux 7.2.11-arch1-1.

| Workload | XRGB → UYVY auto (ms) | RGB565 → RGB565 auto (ms) | Less CPU time |
|---|---:|---:|---:|
| 1080p-full-scan-off | 0.872 | 0.229 | 73.7% |
| 1080p-full-scan-1 | 1.805 | 0.470 | 73.9% |
| 1080p-video60-scan-2 | 1.611 | 0.390 | 75.8% |
| 1080p-cursor-scan-2 | 0.773 | 0.237 | 69.3% |
| 1080p-idle-scan-2 | 0.756 | 0.239 | 68.4% |
| 720p-full-scan-off | 0.356 | 0.121 | 65.8% |
| 720p-video60-scan-2 | 0.509 | 0.190 | 62.8% |

Video cases alternate one-row scan margins by sampling phase; every timed
sample balances both phases and formats produce the same byte counts.

Raw data: [encoding](production-x86.csv), [pipeline](production-x86-pipeline.csv),
[native log](production-x86.txt), [source hashes](production-x86-source-sha256.json),
[extracted function hashes](production-x86-functions.json).

### Banana Pi R3 / MT7986 Cortex-A53

Native aarch64, CPU 3; OpenWrt musl GCC 14.3.0; Linux 6.12.94.

| Workload | XRGB → UYVY auto (ms) | RGB565 → RGB565 auto (ms) | Less CPU time |
|---|---:|---:|---:|
| 1080p-full-scan-off | 18.367 | 8.958 | 51.2% |
| 1080p-full-scan-1 | 32.173 | 15.556 | 51.6% |
| 1080p-video60-scan-2 | 28.742 | 14.610 | 49.2% |
| 1080p-cursor-scan-2 | 18.065 | 9.397 | 48.0% |
| 1080p-idle-scan-2 | 18.053 | 9.274 | 48.6% |
| 720p-full-scan-off | 9.414 | 5.321 | 43.5% |
| 720p-video60-scan-2 | 15.163 | 8.650 | 43.0% |

Video cases alternate one-row scan margins by sampling phase; every timed
sample balances both phases and formats produce the same byte counts.

Raw data: [encoding](production-a53.csv), [pipeline](production-a53-pipeline.csv),
[native log](production-a53.txt), [source hashes](production-a53-source-sha256.json),
[extracted function hashes](production-a53-functions.json).

## Refinement versus the preceding uncommitted implementation

The follow-up changes use a shared inline tail of at most seven pixels
(fixed 4/2/1-pixel loads/stores), native-width scalar indices, and incremental
source/destination row pointers. The SIMD loops retain two independent full
128-bit vectors and the existing bounded FPU sections. Mask semantics and
backend selection are unchanged.

These are successive runs on the same pinned CPUs with unchanged compiler
flags and seven samples per case (median reported). Clocks are not fixed; small differences
should not be treated as established improvements. Full-frame SIMD times
remain approximately unchanged.

| Target | Reused rectangle | Backend | Before (µs) | After (µs) | Less CPU time |
|---|---|---|---:|---:|---:|
| x86 | 1920x1080 | scalar-mask | 231.698 | 135.142 | 41.7% |
| x86 | 640x128 | RGB565-auto | 3.905 | 3.619 | 7.3% |
| x86 | 64x64 | RGB565-auto | 0.252 | 0.208 | 17.3% |
| x86 | 32x32 | RGB565-auto | 0.084 | 0.069 | 17.5% |
| x86 | 1920x1080 | RGB565-auto | 78.720 | 79.004 | -0.4% |
| a53 | 1920x1080 | scalar-mask | 3180.939 | 2933.146 | 7.8% |
| a53 | 640x128 | RGB565-auto | 42.654 | 41.192 | 3.4% |
| a53 | 64x64 | RGB565-auto | 3.231 | 2.774 | 14.2% |
| a53 | 32x32 | RGB565-auto | 1.085 | 0.849 | 21.7% |
| a53 | 1920x1080 | RGB565-auto | 3075.938 | 3103.900 | -0.9% |

Before data: [x86](mask-before-x86.csv), [A53](mask-before-a53.csv);
source snapshots: [x86](mask-before-x86-source-sha256.json),
[A53](mask-before-a53-source-sha256.json). The production files above contain
the updated implementation and its source hashes.

## Validation

- Native x86: 6,013 rectangle cases, all 65,536 RGB565 values per backend,
  826 batch API cases and 322 packed scan/capture pipeline cases.
- Native A53: 4,295 rectangle cases, all native values per backend, 590 batch
  API cases and the same 322 pipeline cases.
- Invalid arguments, maximum batches, rejected later rectangles, multi-rectangle
  trailer placement, 16-row FPU boundaries, unaligned destinations, bounded
  tails, guard pages and SIMD-unavailable fallback are checked.
- x86 correctness also passed under QEMU Conroe, Phenom and Sandy Bridge.
- Full `make tests`, `make test-capabilities`, UBSan/bounds validation and kernel
  module compilation passed. The module was not installed or reloaded.

These tests establish byte correctness of CPU processing and framed payloads.
They do not validate native RGB565 selection by a compositor or hardware scanout.
Historical synthetic-candidate measurements remain in [PACKED_RESULTS.md](PACKED_RESULTS.md);
its scalar pipeline used SIMD scan and is not the current all-scalar control.
