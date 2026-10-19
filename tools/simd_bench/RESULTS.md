# SSE2, SSSE3 and SSE4.1 benchmark report

Measured on 2026-10-19 against baseline `9a4a6ad83cf5ffe9883701dfc7b5c2c7a6280238`.
The after column measures driver commit `4500b57b4a9a291225103258b12924f08e226d8d`.

## Results that determine dispatch

- SSE2 relaxed scanning now keeps counts and bounds in SIMD registers. Across
  all tested widths/patterns it gains **1.37-2.28x** over baseline SSE2.
- SSE4.1 relaxed scanning gains **1.53-2.51x** over baseline SSE2. A direct
  comparison against the new SSE2 implementation measures **1.06-1.12x**.
- SSSE3 conversion measures **1.49x** at 1920x1080 and **1.48x** at 640x128.
  Six-pixel rows delegate to the SSE2 converter and measure **0.99x**.
- SSE4.1 exact scanning with PTEST measures **0.92-1.00x** versus baseline
  SSE2, and **0.94-1.02x** versus new SSE2. It remains explicitly selectable
  as `damage_scan_method=sse41`, but automatic selection uses SSE2 for exact
  scanning when AVX2 is unavailable. Native Penryn measurements may justify
  a different choice later.
- The SSE2 exact worker has the same generated instructions as the baseline;
  its measured ratios are **0.97-1.00x**. Other unchanged algorithm controls
  (AVX2, scalar and memcmp) also show timing/layout variation; their ratios
  should not be interpreted as new algorithmic gains or losses.

## Hardware and measurement scope

- CPU: AMD Ryzen 9 5900XT, Zen 3, 16 cores / 32 threads. Native timing pinned
  to logical CPU 15. Other desktop activity and CPU frequency were not fixed.
- GCC: `16.2.1 20260810`. Kernel module built against `7.2.10-arch1-1`.
- Seven timed rounds per implementation, alternating baseline/current order;
  each throughput is computed from the median duration. All fourteen raw
  durations per case are preserved in [results-native.csv](results-native.csv).
- These are userspace microbenchmarks of the actual driver loop sources and
  headers, compiled in separate translation units. FPU/NEON guards become
  compiler barriers. Kernel context-save costs, instrumentation, snapshots,
  damage-region merging, USB traffic and display refresh are excluded.
- Results measure these ISA implementations on Zen 3. **They do not measure
  native Core 2 Duo or Phenom II performance.** QEMU checks below establish
  correctness with older instruction sets, and are not performance evidence.
- Exact scan backends have different damage granularities: SSE2/SSE4.1 use
  64-byte blocks, AVX2 uses 256-byte blocks, memcmp uses 128-byte blocks, and
  scalar checks individual RGB pixels. Raw rates across these backends are
  not an equal-granularity comparison or a prediction of driver FPS.

## Build flags and workloads

Common flags: `-O2 -fno-tree-vectorize -fno-tree-slp-vectorize
-fno-strict-aliasing`, no LTO. The SSE2 loops explicitly disable SSSE3,
SSE4.1, AVX and POPCNT; SSSE3 disables SSE4.1, AVX and POPCNT; SSE4.1 disables
SSE4.2, AVX and POPCNT. AVX2 uses `-mavx2 -mno-avx512f`. These are benchmark
flags; production Kbuild uses a separate object for each supported ISA and
`-mno-avx` for all SSE objects.

Scan workloads use 16 rows, widths 1920/640/33/16, a 5760-pixel row stride,
and `1024 * floor(1920 / width)` batch calls per timed round. Relaxed tolerance
is 64. Unchanged buffers are identical; sparse inputs flip RGB every 67th
pixel of the backing image; dense inputs flip RGB in every pixel. X is kept
unchanged. Conversion uses a 1920-pixel source stride:

| Rectangle | Origin | Calls per round |
| --- | --- | ---: |
| 1920x1080 | 0,0 | 32 |
| 640x128 | 2,2 | 512 |
| 6x64 | 2,0 | 32768 |

Scalar and memcmp wrappers are compiled without SIMD. The benchmark-only
memcmp comparator reproduces the generic kernel word-at-a-time algorithm
previously checked against this running kernel's vmlinux, rather than calling
glibc memcmp. Production memcmp scanning still calls the kernel's memcmp and
has no direct SIMD implementation.

## Correctness and compatibility

- AVX2, SSE2 and SSE4.1: each passes 1792 differential scan cases spanning all
  256 tolerances, unchanged/X-only/sparse/dense/threshold-boundary patterns,
  varied offsets/strides, exact result bounds, counters and baseline selection.
- Wide-row checks at 32767, 32768, 32769 and 65535 pixels validate SSE2's
  signed 16-bit position limit and scalar fallback. Actual driver rows are
  at most 1920 pixels.
- SSE2, SSSE3 and AVX2 conversion: each passes 960 rectangles of even widths
  2..1920 and varied origins/heights/output alignments. All 2^24 individual
  RGB colors are also tested as identical adjacent pixels, with arbitrary X.
  Random distinct pairs are covered by the rectangles; all possible pixel
  pairs are not exhaustively enumerated. Output is byte-exact against the
  original Q16 arithmetic.
- Guard pages cover row tails 1..129 pixels for supported operations.
- 320 dispatch matrix cases compile the actual production selectors against
  synthetic feature masks, testing forced modes, unavailable SIMD contexts,
  auto selection, backend naming and unstable X-byte handling.
- QEMU x86 correctness profiles: Conroe (SSE2 and SSSE3), Penryn (also SSE4.1),
  and Phenom/K10 (SSE2, no SSSE3/SSE4.1). This Phenom model covers the relevant
  instruction-set restrictions of Phenom II; it does not simulate its speed.
- AArch64 NEON, scalar and memcmp correctness pass under qemu-aarch64 10.2.2,
  built with Arm GNU Toolchain 15.2.Rel1, GCC 15.2.1 20251203. No native ARM
  throughput is reported. [Emulated validation logs](validation.txt).
- Kernel module build passes. Object inspection confirms that vector work
  stays within the protected workers, SSE objects use no AVX, relaxed SSE2
  scanning no longer performs per-vector scalar bit scans, and the new
  SSSE3/SSE4.1 instructions are present only in their respective objects.

## Automatic selection on older CPUs

Assuming SIMD can be used, and stable X bytes for exact scans:

| CPU family | Exact scan | Relaxed scan | Conversion |
| --- | --- | --- | --- |
| Core 2 Duo Conroe/Merom | SSE2 | SSE2 | SSSE3 |
| Core 2 Duo Penryn/Wolfdale | SSE2 | SSE4.1 | SSSE3 |
| Phenom II / K10 | SSE2 | SSE2 | SSE2 |

The current Ryzen continues selecting AVX2 automatically. Unsupported forced
SIMD modes fall back to scalar. `sse4.1` is accepted as an alias of `sse41`.

## Full native results

All rates below are million input pixels per second (Mpixel/s). Higher is
better. SSE4.1 and SSSE3 rows compare against baseline SSE2; rows named
SSE4.1-vs-SSE2 compare directly against the new SSE2 implementation. AVX2,
SSE2 and memcmp compare their own baseline/current implementations. Scalar
rows use the same current scalar helper for both timings as a control.

### AVX2

| Workload | Before | After | Speedup |
| --- | ---: | ---: | ---: |
| exact unchanged w=1920 | 10515.2 | 10526.9 | 1.00x |
| relaxed unchanged w=1920 | 6894.1 | 6813.9 | 0.99x |
| exact sparse w=1920 | 12677.4 | 12669.0 | 1.00x |
| relaxed sparse w=1920 | 6885.3 | 6808.0 | 0.99x |
| exact dense w=1920 | 12675.9 | 12669.4 | 1.00x |
| relaxed dense w=1920 | 6858.7 | 6855.8 | 1.00x |
| exact unchanged w=640 | 12137.3 | 12091.3 | 1.00x |
| relaxed unchanged w=640 | 6104.6 | 6144.9 | 1.01x |
| exact sparse w=640 | 12082.7 | 12186.5 | 1.01x |
| relaxed sparse w=640 | 6099.3 | 6103.5 | 1.00x |
| exact dense w=640 | 12127.2 | 12127.2 | 1.00x |
| relaxed dense w=640 | 6106.7 | 6088.4 | 1.00x |
| exact unchanged w=33 | 8469.1 | 8360.9 | 0.99x |
| relaxed unchanged w=33 | 3901.4 | 3975.2 | 1.02x |
| exact sparse w=33 | 8484.8 | 8384.3 | 0.99x |
| relaxed sparse w=33 | 3905.2 | 3982.3 | 1.02x |
| exact dense w=33 | 8461.7 | 8361.1 | 0.99x |
| relaxed dense w=33 | 3903.9 | 3985.5 | 1.02x |
| exact unchanged w=16 | 5163.2 | 4940.8 | 0.96x |
| relaxed unchanged w=16 | 3883.6 | 3883.2 | 1.00x |
| exact sparse w=16 | 5162.6 | 4944.8 | 0.96x |
| relaxed sparse w=16 | 3883.7 | 3880.1 | 1.00x |
| exact dense w=16 | 5159.1 | 4946.4 | 0.96x |
| relaxed dense w=16 | 3883.0 | 3878.6 | 1.00x |
| conversion 1920x1080 | 5324.0 | 5330.9 | 1.00x |
| conversion 640x128 | 4702.5 | 4596.9 | 0.98x |
| conversion 6x64 | 918.3 | 978.1 | 1.07x |

### SSE2

| Workload | Before | After | Speedup |
| --- | ---: | ---: | ---: |
| exact unchanged w=1920 | 7075.7 | 6963.4 | 0.98x |
| relaxed unchanged w=1920 | 2183.8 | 3419.4 | 1.57x |
| exact sparse w=1920 | 7083.1 | 6990.2 | 0.99x |
| relaxed sparse w=1920 | 2003.7 | 3404.0 | 1.70x |
| exact dense w=1920 | 7071.9 | 6956.9 | 0.98x |
| relaxed dense w=1920 | 1493.0 | 3411.1 | 2.28x |
| exact unchanged w=640 | 6910.6 | 6888.6 | 1.00x |
| relaxed unchanged w=640 | 2157.6 | 3336.4 | 1.55x |
| exact sparse w=640 | 6868.5 | 6831.5 | 0.99x |
| relaxed sparse w=640 | 2002.3 | 3338.3 | 1.67x |
| exact dense w=640 | 6926.6 | 6867.6 | 0.99x |
| relaxed dense w=640 | 1480.0 | 3361.8 | 2.27x |
| exact unchanged w=33 | 4163.8 | 4018.1 | 0.97x |
| relaxed unchanged w=33 | 1635.9 | 2350.2 | 1.44x |
| exact sparse w=33 | 4162.2 | 4030.4 | 0.97x |
| relaxed sparse w=33 | 1613.8 | 2350.1 | 1.46x |
| exact dense w=33 | 4161.2 | 4027.9 | 0.97x |
| relaxed dense w=33 | 1288.8 | 2350.1 | 1.82x |
| exact unchanged w=16 | 4651.4 | 4670.0 | 1.00x |
| relaxed unchanged w=16 | 1714.7 | 2345.1 | 1.37x |
| exact sparse w=16 | 4621.1 | 4615.6 | 1.00x |
| relaxed sparse w=16 | 1673.8 | 2325.5 | 1.39x |
| exact dense w=16 | 4632.5 | 4594.0 | 0.99x |
| relaxed dense w=16 | 1221.6 | 2202.3 | 1.80x |
| conversion 1920x1080 | 1838.3 | 1872.5 | 1.02x |
| conversion 640x128 | 1794.7 | 1830.6 | 1.02x |
| conversion 6x64 | 964.3 | 965.5 | 1.00x |

### SSSE3

| Workload | Before | After | Speedup |
| --- | ---: | ---: | ---: |
| conversion 1920x1080 | 1854.0 | 2766.0 | 1.49x |
| conversion 640x128 | 1750.7 | 2598.2 | 1.48x |
| conversion 6x64 | 929.7 | 921.0 | 0.99x |

### SSE4.1

| Workload | Before | After | Speedup |
| --- | ---: | ---: | ---: |
| exact unchanged w=1920 | 6580.4 | 6419.1 | 0.98x |
| relaxed unchanged w=1920 | 2083.5 | 3498.2 | 1.68x |
| exact sparse w=1920 | 6504.3 | 6479.8 | 1.00x |
| relaxed sparse w=1920 | 1947.0 | 3494.3 | 1.79x |
| exact dense w=1920 | 6859.9 | 6668.4 | 0.97x |
| relaxed dense w=1920 | 1450.7 | 3631.1 | 2.50x |
| exact unchanged w=640 | 6363.9 | 6139.7 | 0.96x |
| relaxed unchanged w=640 | 2059.5 | 3458.6 | 1.68x |
| exact sparse w=640 | 6356.8 | 6220.7 | 0.98x |
| relaxed sparse w=640 | 1844.1 | 3463.1 | 1.88x |
| exact dense w=640 | 6576.8 | 6472.9 | 0.98x |
| relaxed dense w=640 | 1430.2 | 3587.2 | 2.51x |
| exact unchanged w=33 | 4066.9 | 3781.7 | 0.93x |
| relaxed unchanged w=33 | 1567.7 | 2456.6 | 1.57x |
| exact sparse w=33 | 3983.1 | 3776.4 | 0.95x |
| relaxed sparse w=33 | 1519.8 | 2494.3 | 1.64x |
| exact dense w=33 | 4027.5 | 3727.0 | 0.93x |
| relaxed dense w=33 | 1251.1 | 2423.2 | 1.94x |
| exact unchanged w=16 | 4540.6 | 4317.3 | 0.95x |
| relaxed unchanged w=16 | 1667.4 | 2554.7 | 1.53x |
| exact sparse w=16 | 4579.1 | 4215.9 | 0.92x |
| relaxed sparse w=16 | 1633.3 | 2549.5 | 1.56x |
| exact dense w=16 | 4542.7 | 4316.5 | 0.95x |
| relaxed dense w=16 | 1259.2 | 2569.3 | 2.04x |

### SSE4.1-vs-SSE2

| Workload | Before | After | Speedup |
| --- | ---: | ---: | ---: |
| exact unchanged w=1920 | 6812.0 | 6738.8 | 0.99x |
| relaxed unchanged w=1920 | 3409.4 | 3618.1 | 1.06x |
| exact sparse w=1920 | 6781.2 | 6732.4 | 0.99x |
| relaxed sparse w=1920 | 3398.2 | 3586.1 | 1.06x |
| exact dense w=1920 | 6771.0 | 6733.5 | 0.99x |
| relaxed dense w=1920 | 3229.7 | 3501.1 | 1.08x |
| exact unchanged w=640 | 6496.2 | 6333.1 | 0.97x |
| relaxed unchanged w=640 | 3185.8 | 3429.5 | 1.08x |
| exact sparse w=640 | 6337.6 | 6148.9 | 0.97x |
| relaxed sparse w=640 | 3222.0 | 3413.5 | 1.06x |
| exact dense w=640 | 6345.1 | 6498.8 | 1.02x |
| relaxed dense w=640 | 3213.5 | 3607.1 | 1.12x |
| exact unchanged w=33 | 3833.4 | 3751.7 | 0.98x |
| relaxed unchanged w=33 | 2185.2 | 2351.5 | 1.08x |
| exact sparse w=33 | 3933.7 | 3752.4 | 0.95x |
| relaxed sparse w=33 | 2278.8 | 2438.7 | 1.07x |
| exact dense w=33 | 3916.7 | 3773.6 | 0.96x |
| relaxed dense w=33 | 2251.9 | 2391.1 | 1.06x |
| exact unchanged w=16 | 4252.5 | 4061.7 | 0.96x |
| relaxed unchanged w=16 | 2254.6 | 2517.5 | 1.12x |
| exact sparse w=16 | 4433.7 | 4226.4 | 0.95x |
| relaxed sparse w=16 | 2290.2 | 2497.5 | 1.09x |
| exact dense w=16 | 4164.6 | 3931.0 | 0.94x |
| relaxed dense w=16 | 2238.3 | 2410.7 | 1.08x |

### scalar

| Workload | Before | After | Speedup |
| --- | ---: | ---: | ---: |
| exact unchanged w=1920 | 975.0 | 971.4 | 1.00x |
| relaxed unchanged w=1920 | 403.7 | 403.8 | 1.00x |
| exact sparse w=1920 | 972.7 | 985.8 | 1.01x |
| relaxed sparse w=1920 | 397.7 | 402.0 | 1.01x |
| exact dense w=1920 | 967.1 | 966.2 | 1.00x |
| relaxed dense w=1920 | 392.8 | 392.9 | 1.00x |
| exact unchanged w=640 | 907.4 | 911.3 | 1.00x |
| relaxed unchanged w=640 | 371.7 | 367.3 | 0.99x |
| exact sparse w=640 | 892.9 | 886.3 | 0.99x |
| relaxed sparse w=640 | 376.7 | 371.7 | 0.99x |
| exact dense w=640 | 930.9 | 922.8 | 0.99x |
| relaxed dense w=640 | 381.9 | 384.8 | 1.01x |
| exact unchanged w=33 | 916.2 | 940.3 | 1.03x |
| relaxed unchanged w=33 | 377.7 | 377.8 | 1.00x |
| exact sparse w=33 | 923.0 | 934.0 | 1.01x |
| relaxed sparse w=33 | 382.3 | 383.0 | 1.00x |
| exact dense w=33 | 954.3 | 955.1 | 1.00x |
| relaxed dense w=33 | 387.8 | 388.6 | 1.00x |
| exact unchanged w=16 | 918.5 | 919.0 | 1.00x |
| relaxed unchanged w=16 | 386.4 | 386.6 | 1.00x |
| exact sparse w=16 | 898.4 | 900.5 | 1.00x |
| relaxed sparse w=16 | 401.3 | 401.6 | 1.00x |
| exact dense w=16 | 946.9 | 947.4 | 1.00x |
| relaxed dense w=16 | 379.6 | 378.9 | 1.00x |
| conversion 1920x1080 | 397.2 | 397.7 | 1.00x |
| conversion 640x128 | 397.3 | 398.8 | 1.00x |
| conversion 6x64 | 402.6 | 402.1 | 1.00x |

### memcmp

| Workload | Before | After | Speedup |
| --- | ---: | ---: | ---: |
| exact unchanged w=1920 | 6349.2 | 6341.0 | 1.00x |
| exact sparse w=1920 | 5322.9 | 5379.2 | 1.01x |
| exact dense w=1920 | 18283.3 | 18513.6 | 1.01x |
| exact unchanged w=640 | 5955.2 | 5940.8 | 1.00x |
| exact sparse w=640 | 5115.7 | 5042.5 | 0.99x |
| exact dense w=640 | 18600.5 | 18690.6 | 1.00x |
| exact unchanged w=33 | 3415.8 | 3324.6 | 0.97x |
| exact sparse w=33 | 2487.1 | 2494.2 | 1.00x |
| exact dense w=33 | 4185.8 | 4197.9 | 1.00x |
| exact unchanged w=16 | 2873.7 | 2995.7 | 1.04x |
| exact sparse w=16 | 3146.5 | 3120.6 | 0.99x |
| exact dense w=16 | 4655.7 | 4642.9 | 1.00x |
