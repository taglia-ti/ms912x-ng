# SSSE3 downscale measurements

The 3:2 and 5:4 x86-64 Catmull-Rom filters now have SSSE3 kernels using
`PMADDUBSW`. Their coefficients, Q7 rounding, clipping, crop behavior and
in-place staging remain the same. Runtime dispatch prefers AVX2, then SSSE3,
then SSE2.

## Method

- Native AMD Ryzen 9 5900XT, pinned to logical CPU 15; GCC 16.2.1 at `-O2`.
- Three runs per ratio. Each 3:2 run alternated SSE2 and SSSE3 seven times per
  case; values below use medians pooled across 21 timings. Each 5:4 run reports
  the median of seven timings; values below use the median of those three run
  medians.
- Both source and destination buffers were measured. In-place runs include the
  implementation's horizontal staging and row packing.
- 3:2 throughput is counted in source Mpixel/s. 5:4 throughput is counted in
  output Mpixel/s by the existing benchmark. Ratios are comparable within each
  workload; these are pixel-loop measurements, not end-to-end frame rates.
- Raw timings: [3:2 CSV](results-downscale-ssse3-3to2.csv) and
  [5:4 CSV](results-downscale-ssse3-5to4.csv).

## 3:2 downscale

| Input → output | Layout | SSE2 source Mpixel/s | SSSE3 source Mpixel/s | Change |
| --- | --- | ---: | ---: | ---: |
| 1920×1080 → 1280×720 | separate | 704.7 | 1644.2 | +133.3% |
| 1920×1080 → 1280×720 | in-place | 1042.4 | 2481.1 | +138.0% |
| 1366×768 → 912×512 | separate | 689.1 | 1593.2 | +131.2% |
| 1366×768 → 912×512 | in-place | 1027.5 | 2394.5 | +133.1% |
| 1280×720 → 856×480 | separate | 667.9 | 1461.7 | +118.8% |
| 1280×720 → 856×480 | in-place | 991.2 | 2214.0 | +123.4% |
| 128×128 → 88×86 | separate | 416.7 | 614.1 | +47.4% |
| 128×128 → 88×86 | in-place | 675.4 | 1052.4 | +55.8% |

## 5:4 downscale

This benchmark reports output Mpixel/s. AVX2 remains faster than SSSE3 on these
workloads, while SSSE3 improves substantially over SSE2.

| Input → output | Layout | SSE2 | SSSE3 | AVX2 | SSSE3 change vs SSE2 |
| --- | --- | ---: | ---: | ---: | ---: |
| 1920×1080 → 1536×864 | separate | 517.5 | 964.9 | 1378.3 | +86.4% |
| 1920×1080 → 1536×864 | in-place | 650.8 | 1244.3 | 1828.7 | +91.2% |
| 1280×720 → 1024×576 | separate | 502.0 | 911.1 | 1265.9 | +81.5% |
| 1280×720 → 1024×576 | in-place | 629.4 | 1168.0 | 1668.5 | +85.6% |
| 640×128 → 512×103 | separate | 466.8 | 784.8 | 1024.9 | +68.1% |
| 640×128 → 512×103 | in-place | 600.2 | 1042.0 | 1452.8 | +73.6% |
| 128×128 → 104×103 | separate | 288.7 | 374.5 | 416.0 | +29.7% |
| 128×128 → 104×103 | in-place | 414.2 | 583.4 | 710.9 | +40.8% |

## Validation

- `make modules` passed against Linux 7.2.11-arch1-1.
- The 3:2 native harness passed 261,121 rounding patterns, guard pages,
  staged crops, in-place and packed outputs, and 263 geometries. QEMU Penryn
  also passed, exercising SSSE3 without AVX.
- The 5:4 harness passed 1,959 geometries, crops and guard-page cases, including
  all phases/channels, in-place output, damage mapping and scale routing.
- Object inspection confirms `PMADDUBSW` in both SSSE3 objects. The kernel
  build uses `-mno-sse4.1 -mno-avx` for these files.
