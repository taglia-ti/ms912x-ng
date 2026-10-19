# NEON ARM64 tuning and ARM32 support

Measured on 2026-10-19 against `2469f89a97c0d71d596f2167359566c03c1eee27`.
Measured driver: 07eb4f12829b4c0bf55ad5efb531f2fc9f12c4ae.

## Results

- Exact NEON scanning: 1.17-1.19x at widths 1920/640/33; 1.05-1.07x at width 16.
- Relaxed NEON scanning: 1.09-1.12x at widths 1920/640, 1.04-1.05x at width 33,
  and 1.10x at width 16.
- Conversion: 1920x1080 299.0 -> 319.6 Mpixel/s (1.07x);
  640x128 310.9 -> 307.2 (0.99x); 6x64 110.3 -> 119.8 (1.09x).
  The middle case is about 1.2% slower; conversion does not improve every workload.
- Scalar and memcmp control implementations are unchanged and remain near 1.00x.

## Measurement scope

- Native Banana Pi BPI-R3, MediaTek MT7986/Cortex-A53, Linux 6.12.94,
  OpenWrt 25.12.5 r33051-f5dae5ece4, pinned to logical CPU 3 with taskset.
- Cross compiler: aarch64-openwrt-linux-musl-gcc, OpenWrt GCC 14.3.0
  r32933-4ccb782af7, from the user's prebuilt OpenWrt tree.
- Seven timings per implementation, before/after order alternated, median duration.
  Warm/reused buffers; clock, temperature and other router activity were not fixed.
  The kernel exposes no cpu3 cpufreq governor/frequency files, so no operating
  frequency is claimed.
- [Raw samples](results-neon-a53.csv): all 66 measurements and all 14 durations
  per case. Final NEON measurements are from the second native invocation;
  unchanged scalar/memcmp controls are from the first full native invocation.
  Invocations ran sequentially on CPU 3. No rejected NEON candidate is included.
- Actual driver loop sources, -O2, no LTO or automatic vectorization;
  NEON objects use -march=armv8-a+simd. Scalar/memcmp wrappers are integer-only.
- FPU guards are userspace compiler barriers. Timings exclude kernel FPU state
  management, snapshot ownership/copies, damage merging, USB and display refresh.
  These numbers do not establish driver FPS or end-to-end latency.
- The memcmp control calls a generic kernel-style word comparison implemented
  in C. It is not the ARM64 kernel's assembly memcmp. Production code continues
  to call the real kernel memcmp without direct SIMD.
- Scan rows: widths 1920/640/33/16, 16 rows, stride 5760 pixels, tolerance 64,
  unchanged/sparse/dense RGB patterns; 1024 * floor(1920/width) batch calls.
  Exact NEON damage blocks remain 64 bytes; relaxed bounds remain per pixel.
- Conversion: 1920x1080 at (0,0), 640x128 at (2,2), 6x64 at (2,0),
  respectively 32/512/32768 calls per timing; source stride 1920 pixels.
- No native ARM32 throughput is claimed. The R3 kernel has CONFIG_COMPAT disabled.
  ARM32 correctness is checked with QEMU Cortex-A7.

## Implementation decisions

- Exact scans keep counters and bounds in local 32-bit variables and publish
  once per row. This avoids per-block 64-bit bookkeeping, particularly on ARM32.
- Relaxed scans overlap two independent four-pixel vectors for wide rows.
  Widths below 32 retain a single accumulator to avoid setup/merge overhead.
- Four-pixel conversion uses widening 16-bit MACs and combines two narrowing
  shifts while preserving the original Q16 bits. Eight-pixel conversion retains
  halfword UV interleaving and ST2 output: a compact byte packing candidate
  regressed on A53 and was rejected for the main loop.
- NEON workers and FPU wrappers are separate objects; LTO is disabled for both.
  Use the kernel's neon-intrinsics header and freestanding compiler headers,
  avoiding libc type conflicts in real kernel builds.
- ARM32 requires CONFIG_KERNEL_MODE_NEON and runtime cpu_has_neon/may_use_simd.
  Missing CPU capability, disabled kernel support and unusable contexts disable
  NEON, keeping scalar conversion and the existing scalar/memcmp scan fallbacks. ARM32 uses pairwise reductions; ARM64 uses native whole-vector reductions.

## Validation

- Native A53 and QEMU AArch64/Cortex-A53, ARM32/Cortex-A7:
  1792 differential scan cases/backend across all 256 tolerances; RGB/X patterns,
  bounds, counts and snapshot selection; wide widths through 65535.
- 960 conversion rectangles, all 2^24 RGB triples as identical adjacent pixels,
  random distinct pixel pairs, and guard-page row tails. Q16 output is byte-exact;
  all possible pairs of colors are not exhaustively enumerated.
- 320 dispatch cases per target; missing NEON and unavailable SIMD conversion
  guards return before touching buffers. Freestanding ARM32 entry guard passes
  with QEMU NEON and VFP-D32 disabled.
- Real Linux 7.2.10 Kbuild cross-compiles and links the full driver object for
  ARM64 defconfig, ARM32 multi_v7_defconfig with NEON, and BCM2835/ARMv6 without
  NEON. Host x86-64 module build and correctness checks pass.
- The harness also passes when the new split NEON implementation is used as
  its Git baseline, for both ARM64 and ARM32.
- Object inspection checks that SIMD stays in the worker objects and glue uses
  integer registers, with calls to kernel FPU entry/exit around each worker.
- The user's OpenWrt 6.12.94 configuration disables DRM/DMA-BUF and also needs
  older DRM/timer API compatibility. A loadable R3 module is deferred by request;
  no firmware, kernel or loaded driver was changed. The benchmark is userspace.

## Full native results

All rates are million input pixels per second. Higher is better.

| Backend | Workload | Before | After | Ratio |
| --- | --- | ---: | ---: | ---: |
| NEON | exact unchanged w=1920 | 562.5 | 664.6 | 1.18x |
| NEON | relaxed unchanged w=1920 | 238.9 | 259.6 | 1.09x |
| NEON | exact sparse w=1920 | 575.4 | 673.0 | 1.17x |
| NEON | relaxed sparse w=1920 | 233.7 | 260.8 | 1.12x |
| NEON | exact dense w=1920 | 575.4 | 676.5 | 1.18x |
| NEON | relaxed dense w=1920 | 237.9 | 263.7 | 1.11x |
| NEON | exact unchanged w=640 | 542.3 | 642.8 | 1.19x |
| NEON | relaxed unchanged w=640 | 251.1 | 277.4 | 1.10x |
| NEON | exact sparse w=640 | 543.2 | 635.6 | 1.17x |
| NEON | relaxed sparse w=640 | 250.7 | 278.0 | 1.11x |
| NEON | exact dense w=640 | 543.7 | 642.2 | 1.18x |
| NEON | relaxed dense w=640 | 251.3 | 277.8 | 1.11x |
| NEON | exact unchanged w=33 | 426.6 | 506.2 | 1.19x |
| NEON | relaxed unchanged w=33 | 218.5 | 228.3 | 1.04x |
| NEON | exact sparse w=33 | 426.4 | 503.8 | 1.18x |
| NEON | relaxed sparse w=33 | 218.4 | 228.5 | 1.05x |
| NEON | exact dense w=33 | 426.4 | 498.1 | 1.17x |
| NEON | relaxed dense w=33 | 218.0 | 228.5 | 1.05x |
| NEON | exact unchanged w=16 | 425.9 | 453.7 | 1.07x |
| NEON | relaxed unchanged w=16 | 200.9 | 220.4 | 1.10x |
| NEON | exact sparse w=16 | 425.6 | 447.7 | 1.05x |
| NEON | relaxed sparse w=16 | 201.0 | 220.7 | 1.10x |
| NEON | exact dense w=16 | 424.4 | 453.8 | 1.07x |
| NEON | relaxed dense w=16 | 200.6 | 220.8 | 1.10x |
| NEON | conversion 1920x1080 | 299.0 | 319.6 | 1.07x |
| NEON | conversion 640x128 | 310.9 | 307.2 | 0.99x |
| NEON | conversion 6x64 | 110.3 | 119.8 | 1.09x |
| scalar | exact unchanged w=1920 | 148.9 | 148.6 | 1.00x |
| scalar | relaxed unchanged w=1920 | 68.0 | 67.6 | 0.99x |
| scalar | exact sparse w=1920 | 149.0 | 148.7 | 1.00x |
| scalar | relaxed sparse w=1920 | 67.7 | 67.4 | 1.00x |
| scalar | exact dense w=1920 | 148.5 | 148.5 | 1.00x |
| scalar | relaxed dense w=1920 | 67.6 | 67.6 | 1.00x |
| scalar | exact unchanged w=640 | 147.5 | 147.5 | 1.00x |
| scalar | relaxed unchanged w=640 | 69.3 | 69.3 | 1.00x |
| scalar | exact sparse w=640 | 147.4 | 147.6 | 1.00x |
| scalar | relaxed sparse w=640 | 69.3 | 69.3 | 1.00x |
| scalar | exact dense w=640 | 147.4 | 147.4 | 1.00x |
| scalar | relaxed dense w=640 | 69.3 | 69.3 | 1.00x |
| scalar | exact unchanged w=33 | 138.6 | 138.6 | 1.00x |
| scalar | relaxed unchanged w=33 | 67.2 | 67.2 | 1.00x |
| scalar | exact sparse w=33 | 138.5 | 138.5 | 1.00x |
| scalar | relaxed sparse w=33 | 67.3 | 67.3 | 1.00x |
| scalar | exact dense w=33 | 138.6 | 138.7 | 1.00x |
| scalar | relaxed dense w=33 | 67.3 | 67.3 | 1.00x |
| scalar | exact unchanged w=16 | 125.6 | 125.5 | 1.00x |
| scalar | relaxed unchanged w=16 | 63.7 | 63.7 | 1.00x |
| scalar | exact sparse w=16 | 125.5 | 125.5 | 1.00x |
| scalar | relaxed sparse w=16 | 63.7 | 63.7 | 1.00x |
| scalar | exact dense w=16 | 125.4 | 125.6 | 1.00x |
| scalar | relaxed dense w=16 | 63.8 | 63.8 | 1.00x |
| scalar | conversion 1920x1080 | 105.1 | 105.3 | 1.00x |
| scalar | conversion 640x128 | 105.5 | 105.8 | 1.00x |
| scalar | conversion 6x64 | 104.3 | 104.1 | 1.00x |
| memcmp | exact unchanged w=1920 | 503.9 | 503.6 | 1.00x |
| memcmp | exact sparse w=1920 | 442.2 | 442.6 | 1.00x |
| memcmp | exact dense w=1920 | 1939.1 | 1938.9 | 1.00x |
| memcmp | exact unchanged w=640 | 481.5 | 481.2 | 1.00x |
| memcmp | exact sparse w=640 | 419.2 | 420.2 | 1.00x |
| memcmp | exact dense w=640 | 1672.4 | 1674.2 | 1.00x |
| memcmp | exact unchanged w=33 | 315.0 | 315.1 | 1.00x |
| memcmp | exact sparse w=33 | 241.0 | 241.1 | 1.00x |
| memcmp | exact dense w=33 | 507.0 | 507.3 | 1.00x |
| memcmp | exact unchanged w=16 | 260.5 | 260.4 | 1.00x |
| memcmp | exact sparse w=16 | 278.8 | 279.2 | 1.00x |
| memcmp | exact dense w=16 | 476.4 | 476.7 | 1.00x |
