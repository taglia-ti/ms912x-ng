# Historical RGB565 packing, masking and UYVY comparison

These are pre-correction measurements of widened 32-bit working slots.
See [PACKED_RESULTS.md](PACKED_RESULTS.md) for the current packed implementation
and full CPU pipeline comparison.

Measured on 2026-10-21, from a working tree based on `72ee469` with the
uncommitted RGB565 implementation. `ms912x_convert.c` SHA256:
`17530dbbe54d735eb79be5d684be153bb61bcf56f2dc9be7807cfe6d89760e95`.

## The storage layout matters more than the mask alone

The current RGB565 capture path widens each native 16-bit word into a 32-bit
working slot without changing its color components. The encoder then narrows
these slots back into the 16-bit USB payload while applying `0xF7FE`.
These results measure that whole pack/copy step, including rectangle framing,
not an isolated bitwise AND.

At 1920x1080 on the BPi-R3, the current scalar RGB565 encoder takes 7.27 ms,
while the existing UYVY NEON converter takes 6.54 ms. This comparison mixes
scalar and SIMD implementations. With matched backends, RGB565 takes 7.27 ms
versus 11.51 ms for scalar UYVY, and 5.11 ms versus 6.54 ms for NEON UYVY.
RGB565 with NEON saves about 22% of this processing time on the A53.

Removing only the AND from the current RGB565 loop still takes 6.34 ms on
the A53 and 0.583 ms on x86. Caching the row width and retaining the AND takes
6.35 ms and 0.506 ms respectively. The current loop reloads the rectangle's
horizontal bound inside the pixel loop; the generated assembly confirms that
the local-width variant removes that load. Thus the gap between the two
original loops also includes compiler scheduling and loop effects.

Keeping RGB565 packed throughout capture, snapshots, scanning and staging is
the first structural improvement to make. The existing masked scan reductions
can compare two native RGB565 pixels per 32-bit word by repeating the mask
(`0xF7FEF7FE` at tolerance zero). Strides, bounds, baseline copies and counters
must translate between word units and pixel coordinates. The SIMD comparison
algorithms need not be rewritten. This accepts coarser damage boundaries,
consistent with the existing block scan policy.

This benchmark does not yet measure packed 16-bit working buffers. Its SIMD
speedups should not be treated as predictions for that future storage layout.

## Native targets and method

- x86-64: AMD Ryzen 9 5900XT, Linux 7.2.11-arch1-1, pinned CPU 15, observed
  governor `performance`, GCC 16.2.1. Frequency was not fixed.
- ARM64: Banana Pi BPi-R3, MT7986/Cortex-A53, OpenWrt 25.12.5
  r33051-f5dae5ece4, Linux 6.12.94, pinned CPU 3. OpenWrt GCC 14.3.0 from
  `/opt/openwrt`. No cpufreq governor file was exposed; frequency is not claimed.
- `-O2`, no LTO, no automatic vectorization. Scalar objects use integer
  registers only. SIMD objects use explicit ISA flags from [run.py](run.py).
- Seven samples per case, calibrated to roughly 30 ms each, median duration.
  Backend execution order rotates between samples.
- Nine rectangle sizes reuse one source/destination pair. The two largest
  also rotate across eight pairs, about 100 MB allocated. Input row stride is
  fixed at 1920 working slots. Buffers are disjoint.
- UYVY controls use the current production scalar color helpers and SSE2,
  AVX2 or NEON pixel workers. The harness preserves the same rectangle framing
  and 16-row SIMD chunks. Kernel FPU operations are userspace compiler barriers.
- Timing excludes capture, scan, snapshot/staging copies, kernel SIMD context
  management, scheduling, cursor composition, USB and display refresh. These
  values are processing latency and throughput, not display FPS.

Raw samples: [x86 CSV](results-x86.csv), [A53 CSV](results-a53.csv).
Build metadata, validation and complete timing output:
[x86 log](benchmark-x86.txt), [A53 log](benchmark-a53.txt),
[x86 sanitizer log](validation-x86.txt),
[older x86 correctness log](validation-older-x86.txt).
The CSV files contain 924 x86 and 616 A53 samples.

## Throughput with one reused buffer pair

All rates are Mpixel/s, higher is better. Names without `UYVY` operate on the
current RGB565 working slots. `scalar-copy` omits the mandatory wire mask and
is a diagnostic control only.

### Ryzen 9 5900XT

| Backend | 1920x1080 | 1280x720 | 640x128 | 64x64 |
| --- | ---: | ---: | ---: | ---: |
| scalar-copy | 3,556.9 | 3,464.5 | 3,301.0 | 3,272.8 |
| scalar-mask, current | 2,277.6 | 2,266.1 | 2,243.0 | 2,269.6 |
| scalar-row | 4,099.6 | 3,819.1 | 3,519.2 | 3,354.1 |
| scalar-pair | 3,839.8 | 3,701.6 | 3,554.2 | 3,072.1 |
| UYVY-scalar | 514.3 | 510.5 | 507.2 | 504.6 |
| SSE2 | 14,313.4 | 11,762.0 | 9,793.9 | 10,544.6 |
| SSSE3 | 16,534.0 | 12,941.8 | 10,711.2 | 11,133.7 |
| SSE4.1 | 16,973.9 | 13,419.4 | 11,261.6 | 11,781.0 |
| AVX1-128 | 17,166.5 | 13,979.6 | 12,038.7 | 12,197.3 |
| AVX2 | 20,261.0 | 16,560.2 | 14,887.6 | 16,610.6 |
| UYVY-SSE2 | 1,916.2 | 1,886.5 | 1,853.5 | 1,743.7 |
| UYVY-AVX2 | 5,437.0 | 5,185.9 | 4,800.9 | 3,606.6 |

### BPi-R3 / Cortex-A53

| Backend | 1920x1080 | 1280x720 | 640x128 | 64x64 |
| --- | ---: | ---: | ---: | ---: |
| scalar-copy | 327.2 | 296.7 | 339.8 | 329.0 |
| scalar-mask, current | 285.1 | 262.1 | 295.1 | 283.5 |
| scalar-row | 326.8 | 299.5 | 347.1 | 333.6 |
| scalar-pair | 353.1 | 319.3 | 377.7 | 362.1 |
| UYVY-scalar | 180.2 | 164.1 | 172.1 | 170.7 |
| NEON | 406.0 | 355.6 | 699.7 | 870.1 |
| NEON-LD2 | 405.3 | 349.3 | 715.5 | 994.1 |
| UYVY-NEON | 317.2 | 281.5 | 308.6 | 319.1 |

## 1080p latency and reduced cache reuse

Times are milliseconds per rectangle, lower is better. The rotating test
shows how much the reused-buffer result depends on caching.

| Target / backend | Reused pair | Eight rotating pairs |
| --- | ---: | ---: |
| x86 RGB565 current scalar | 0.910 | 0.938 |
| x86 RGB565 scalar-row | 0.506 | 0.602 |
| x86 RGB565 SSE2 | 0.145 | 0.431 |
| x86 RGB565 SSSE3 | 0.125 | 0.408 |
| x86 RGB565 SSE4.1 | 0.122 | 0.399 |
| x86 RGB565 AVX1-128 | 0.121 | 0.393 |
| x86 RGB565 AVX2 | 0.102 | 0.407 |
| x86 UYVY AVX2 | 0.381 | 0.520 |
| A53 RGB565 current scalar | 7.274 | 7.347 |
| A53 RGB565 scalar-pair | 5.872 | 5.840 |
| A53 RGB565 NEON | 5.108 | 5.127 |
| A53 RGB565 NEON-LD2 | 5.116 | 5.107 |
| A53 UYVY NEON | 6.536 | 6.524 |

On x86 the wide SIMD paths converge as cache reuse falls. Close rankings in
the rotating test vary with memory behavior and should not determine a new
ISA tier. On the A53 the two NEON forms are effectively tied at 1080p;
LD2 is more useful for cached smaller rectangles. CPU frequency, temperature
and background workload were not controlled, so small differences are not
universal architecture claims.

## Which SIMD backends make sense

After correcting packed storage, start with SSE2, AVX2 and one NEON backend,
and remeasure the simpler packed mask/copy operation. For the layout measured
here, these three provide the main compatibility and throughput choices.

| Candidate | Packing approach in this experiment | Assessment |
| --- | --- | --- |
| SSE2 | Bias masked dwords into signed range, `PACKSSDW`, undo packed bias | Useful x86 baseline; validated under Conroe and Phenom CPU models. |
| SSSE3 | `PSHUFB` selects low halfwords, join them, mask the packed output | Modest gain over SSE2 here; optional if native older-CPU measurements justify it. |
| SSE4.1 | Mask dwords and use `PACKUSDW` | Small gain over SSSE3; does not justify a separate tier from these results. |
| AVX1-128 | VEX-encoded 128-bit mask and `PACKUSDW` | No 256-bit integer packing in this path; small gain over SSE4.1 does not justify another tier. |
| AVX2 | 256-bit mask, `VPACKUSDW`, `VPERMQ` restores pixel order | Best reused-buffer throughput; useful wide x86 path. |
| NEON | Two loads and narrowing, compiled as `LDP` + `UZP1`, then mask/store | Useful ARM path; native A53 validation and timings. |
| NEON-LD2 | `LD2` extracts low halfwords directly, then mask/store | Simpler alternative; faster on some smaller A53 rectangles, tied for full frames. |

The current AVX2 candidate falls back to scalar below 16 pixels per row; its
8x8 result therefore loses to the 128-bit candidates. A production worker
should handle an eight-pixel remainder or choose scalar before entering SIMD
for tiny jobs. Six-pixel-wide rows do not execute the vector loops here.
Kernel SIMD entry/exit costs must inform any size threshold; this userspace
benchmark cannot establish that threshold. Avoid entering SIMD once per row.

Native ARM32 throughput and kernel integration of these candidates remain
unmeasured. Existing kernel capability/context guards and separate ARM32
integer-only glue would still be required. The production driver was not
modified, installed, reloaded or committed during this benchmark task.

## Correctness coverage

Native x86 passed 9,228 rectangle cases and A53 passed 6,152. The scalar
baseline and all candidates match independent pixel/framing oracles,
including every 16-bit RGB565 value, random XRGB/upper slot bits, odd output
alignments, vector boundaries, tails and inaccessible row-end guard pages.
The x86 run also passed undefined-behavior/bounds sanitizers. UYVY checks use
random pixel pairs and the same crops; they are not an exhaustive check of
all pairs of 24-bit colors.

QEMU Conroe, Penryn, Phenom and Sandy Bridge validate supported x86 backends
and skip unavailable ones. Emulated timings are not included. Reproduce native
validation and timing with the [commands in README.md](README.md).
