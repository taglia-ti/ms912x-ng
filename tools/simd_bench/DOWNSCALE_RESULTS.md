# SIMD 2× downscale optimization

> Historical results for the former 2× filter. The current driver implements
> [1.5× / 3→2 downscale](DOWNSCALE_3TO2.md); these timings do not apply to it.
> The reproduction commands below describe the historical 2× revision.

## Comparison and scope

Measured on 2026-10-19 against Git baseline
`8d431efbb3be145c6774974c2de4ffff07f20e31` (`Add adapter-side 2x overscale`).
Each SIMD backend is compared with its own previous implementation; scalar is
an unchanged control. The optimized sources accompany this report. The baseline
is the original pre-amend snapshot; reproduction requires that Git object to
remain available locally.

- x86-64: AMD Ryzen 9 5900XT, Linux 7.2.11-arch1-1, GCC 16.2.1
  (20260810), CPU 15.
- ARM64: Banana Pi BPI-R3 / MT7986 Cortex-A53, OpenWrt 25.12.5,
  Linux 6.12.94, OpenWrt GCC 14.3.0, CPU 3. Native execution on the board.
- ARM32: correctness and kernel object compilation only; no native throughput
  claim is made from QEMU execution.

These are userspace microbenchmarks of the actual driver loops. SIMD context
entry/exit shims are compiler barriers, so real kernel FPU save/restore costs,
USB transfers, display presentation, conversion, and damage scanning are excluded.
No module was loaded or reloaded for these measurements.

## Implementation changes

- SSE2 computes four output pixels per main iteration and stores a full
  128-bit vector, instead of calculating duplicate results and storing 64 bits.
  SSE2 `SHUFPS` instructions rearrange integer bits without floating-point
  arithmetic. A two-output SIMD tail avoids falling back early.
- AVX2 computes eight output pixels per main iteration and stores 256 bits.
  Lane-local shuffles plus one immediate `VPERMQ` replace repeated variable
  `VPERMD` operations. Four- and two-output SIMD tails reuse inline SSE helpers.
- NEON retains four-output deinterleaved loads, replaces masked 32-bit arithmetic
  with byte rounded averages and parity correction, and adds a two-output tail.
- Helpers in the pixel loop are always inline; kernel SIMD workers remain
  noinline. Integer wrappers bracket at most 16 output rows per FPU section,
  avoiding a full frame in one non-preemptible SIMD section. Actual wall time
  still depends on row width; this is a row bound, not a time guarantee.
- Hoist the complete-pair/vector bounds out of the row loop; keep clamped
  scalar edges for odd sizes. Preserve the X/alpha channel and supported
  in-place output.
- Fix the NEON entry wrapper's missing empty-dimension guard, before buffer
  access or FPU entry. Unsupported hardware/context still uses scalar.

### Exact rounding

The result for every byte remains `(a + b + c + d + 2) >> 2`.
Let `avg` be an unsigned byte average rounded up:

```text
ab = avg(a, b)
cd = avg(c, d)
correction = ((a XOR b) OR (c XOR d)) AND (ab XOR cd) AND 1
result = avg(ab, cd) - correction
```

The correction removes the extra round-up introduced by nesting byte averages.
This avoids biased repeated averaging and preserves all four channels.

## Validation

All of the following passed (see [validation log](DOWNSCALE_VALIDATION.txt)):

- Independent byte-wise scalar oracle, previous implementation and new code:
  263 geometries; odd widths/heights, replicated boundaries, source offsets,
  padded strides, separate output, fixed-stride in-place output and packed
  in-place output. Empty dimensions return safely with NULL buffers.
- All 261,121 combinations of vertical pair sums (0 through 510), with
  distinct channel patterns, checking the exact corrected rounding.
- Guard pages directly after each source row and the output, widths 1 through
  129, including two-output SIMD tails and replicated final pixels.
- Native x86-64 and native BPI-R3 suites; emulated Conroe/SSE2, Cortex-A53/ARM64,
  Cortex-A7/ARM32; freestanding ARM32 guard with NEON disabled.
- Existing scan, chroma conversion, dispatch, overscale, and framing checks.
- Full x86-64 module build against Linux 7.2.11-arch1-1. Actual downscale
  dispatcher/scalar/NEON worker/glue objects cross-compiled with Kbuild for
  ARM64/Linux 6.12.94 and ARM32/Linux 7.2.11.
- Kernel object disassembly: no SIMD registers in integer entry wrappers;
  workers stay between FPU begin/end; SSE2 requires no AVX, AVX2 uses full
  stores and immediate lane permutation; ARM glue stays integer-only.

An initial ARM32 object build against Linux 6.12.94 could not find
`asm/neon-intrinsics.h`, an existing driver dependency. ARM32 kernel validation
therefore used Linux 7.2.11; no older-kernel compatibility workaround was added.
The R3 module/firmware work remains deferred because its configured kernel
lacks the previously identified DRM dependencies.

## Timing method and interpretation

Seven before and seven after samples per case, alternating call order after
warmup; report the independent median for each implementation. Frequency,
thermal state and background activity were not fixed. These are observed
ratios from one run, not confidence intervals. Scalar controls help show noise.

All cases use a 1,920-pixel source and destination row stride. Each timed batch
contains 24 calls for 1920×1080, 64 for the other large sizes, 2,048 for
128×128, or 32,768 for the two smallest sizes. In-place working buffers are
reset outside each timed batch, then repeatedly overwritten during the batch;
this is cache-warmed steady-state timing, not independent fresh frame timing.
The loops' work does not depend on pixel content. Buffer copies are excluded.
Packed in-place strides are covered by correctness tests, not these timings.

**Mpixel/s counts source geometry pixels**, `source_width × source_height` per
call, even when an edge pixel is replicated. For exact 2× reduction on both
axes, the output pixel rate is one quarter of the source pixel rate. `ms` is
filter time per call; its reciprocal is not an end-to-end display FPS.

Raw CSVs contain total source pixels per batch and seven elapsed nanosecond
samples per implementation, before sorting:

- [x86 samples](results-downscale-x86.csv): 36 cases, including scalar controls.
- [BPI-R3 samples](results-downscale-a53.csv): 24 cases, including scalar controls.

At Full HD, the in-place path dropped from 0.432 to 0.153 ms with SSE2,
0.293 to 0.108 ms with AVX2, and 5.863 to 5.555 ms with NEON on the R3.
The smaller NEON Full HD gain is consistent with a larger memory contribution,
but no hardware performance counters were collected to establish the cause.
Small 128×128 NEON work improved by about 1.60×.

### Ryzen 9 5900XT, CPU 15

| Backend | Source → output | Storage | Before ms | After ms | Before Mpixel/s | After Mpixel/s | Speedup |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| scalar | 1920×1080 → 960×540 | separate | 0.990752 | 0.990224 | 2093.0 | 2094.1 | 1.00× |
| SSE2 | 1920×1080 → 960×540 | separate | 0.434692 | 0.155373 | 4770.3 | 13345.9 | 2.80× |
| AVX2 | 1920×1080 → 960×540 | separate | 0.294889 | 0.113696 | 7031.8 | 18238.1 | 2.59× |
| scalar | 1920×1080 → 960×540 | in place | 0.986634 | 0.985286 | 2101.7 | 2104.6 | 1.00× |
| SSE2 | 1920×1080 → 960×540 | in place | 0.432107 | 0.153223 | 4798.8 | 13533.2 | 2.82× |
| AVX2 | 1920×1080 → 960×540 | in place | 0.293395 | 0.108321 | 7067.6 | 19143.0 | 2.71× |
| scalar | 1366×768 → 684×384 | separate | 0.505572 | 0.505392 | 2075.0 | 2075.8 | 1.00× |
| SSE2 | 1366×768 → 684×384 | separate | 0.224077 | 0.090691 | 4681.8 | 11567.7 | 2.47× |
| AVX2 | 1366×768 → 684×384 | separate | 0.154609 | 0.069554 | 6785.4 | 15083.1 | 2.22× |
| scalar | 1366×768 → 684×384 | in place | 0.502274 | 0.502487 | 2088.7 | 2087.8 | 1.00× |
| SSE2 | 1366×768 → 684×384 | in place | 0.225648 | 0.097710 | 4649.2 | 10736.8 | 2.31× |
| AVX2 | 1366×768 → 684×384 | in place | 0.154775 | 0.064602 | 6778.2 | 16239.2 | 2.40× |
| scalar | 1280×720 → 640×360 | separate | 0.452039 | 0.453284 | 2038.8 | 2033.2 | 1.00× |
| SSE2 | 1280×720 → 640×360 | separate | 0.199755 | 0.079736 | 4613.7 | 11558.1 | 2.51× |
| AVX2 | 1280×720 → 640×360 | separate | 0.135906 | 0.062823 | 6781.2 | 14669.8 | 2.16× |
| scalar | 1280×720 → 640×360 | in place | 0.452587 | 0.451346 | 2036.3 | 2041.9 | 1.00× |
| SSE2 | 1280×720 → 640×360 | in place | 0.199411 | 0.076494 | 4621.6 | 12048.0 | 2.61× |
| AVX2 | 1280×720 → 640×360 | in place | 0.134839 | 0.057241 | 6834.8 | 16100.3 | 2.36× |
| scalar | 128×128 → 64×64 | separate | 0.008090 | 0.008103 | 2025.3 | 2022.1 | 1.00× |
| SSE2 | 128×128 → 64×64 | separate | 0.003640 | 0.001459 | 4500.6 | 11229.8 | 2.50× |
| AVX2 | 128×128 → 64×64 | separate | 0.002579 | 0.001070 | 6353.8 | 15314.0 | 2.41× |
| scalar | 128×128 → 64×64 | in place | 0.008233 | 0.008248 | 1990.1 | 1986.5 | 1.00× |
| SSE2 | 128×128 → 64×64 | in place | 0.003614 | 0.001444 | 4533.1 | 11343.6 | 2.50× |
| AVX2 | 128×128 → 64×64 | in place | 0.002520 | 0.001063 | 6502.0 | 15408.3 | 2.37× |
| scalar | 33×35 → 17×18 | separate | 0.000624 | 0.000626 | 1852.1 | 1844.4 | 1.00× |
| SSE2 | 33×35 → 17×18 | separate | 0.000298 | 0.000149 | 3874.2 | 7762.1 | 2.00× |
| AVX2 | 33×35 → 17×18 | separate | 0.000198 | 0.000114 | 5843.4 | 10151.1 | 1.74× |
| scalar | 33×35 → 17×18 | in place | 0.000614 | 0.000615 | 1880.4 | 1878.5 | 1.00× |
| SSE2 | 33×35 → 17×18 | in place | 0.000293 | 0.000153 | 3947.4 | 7534.0 | 1.91× |
| AVX2 | 33×35 → 17×18 | in place | 0.000196 | 0.000111 | 5893.8 | 10448.8 | 1.77× |
| scalar | 7×5 → 4×3 | separate | 0.000027 | 0.000027 | 1282.2 | 1280.6 | 1.00× |
| SSE2 | 7×5 → 4×3 | separate | 0.000023 | 0.000022 | 1530.3 | 1606.4 | 1.05× |
| AVX2 | 7×5 → 4×3 | separate | 0.000030 | 0.000023 | 1167.9 | 1490.9 | 1.28× |
| scalar | 7×5 → 4×3 | in place | 0.000027 | 0.000028 | 1292.9 | 1272.5 | 0.98× |
| SSE2 | 7×5 → 4×3 | in place | 0.000023 | 0.000022 | 1531.4 | 1606.9 | 1.05× |
| AVX2 | 7×5 → 4×3 | in place | 0.000031 | 0.000023 | 1114.4 | 1491.1 | 1.34× |

### BPI-R3 Cortex-A53, CPU 3

| Backend | Source → output | Storage | Before ms | After ms | Before Mpixel/s | After Mpixel/s | Speedup |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| scalar | 1920×1080 → 960×540 | separate | 9.109231 | 9.114202 | 227.6 | 227.5 | 1.00× |
| NEON | 1920×1080 → 960×540 | separate | 6.338474 | 5.682657 | 327.1 | 364.9 | 1.12× |
| scalar | 1920×1080 → 960×540 | in place | 8.255990 | 8.264667 | 251.2 | 250.9 | 1.00× |
| NEON | 1920×1080 → 960×540 | in place | 5.863189 | 5.554830 | 353.7 | 373.3 | 1.06× |
| scalar | 1366×768 → 684×384 | separate | 4.674566 | 4.660451 | 224.4 | 225.1 | 1.00× |
| NEON | 1366×768 → 684×384 | separate | 3.431386 | 3.032786 | 305.7 | 345.9 | 1.13× |
| scalar | 1366×768 → 684×384 | in place | 4.595919 | 4.578871 | 228.3 | 229.1 | 1.00× |
| NEON | 1366×768 → 684×384 | in place | 3.278778 | 2.868839 | 320.0 | 365.7 | 1.14× |
| scalar | 1280×720 → 640×360 | separate | 4.102561 | 4.095942 | 224.6 | 225.0 | 1.00× |
| NEON | 1280×720 → 640×360 | separate | 2.939263 | 2.589261 | 313.5 | 355.9 | 1.14× |
| scalar | 1280×720 → 640×360 | in place | 3.677435 | 3.694695 | 250.6 | 249.4 | 1.00× |
| NEON | 1280×720 → 640×360 | in place | 2.688702 | 2.444099 | 342.8 | 377.1 | 1.10× |
| scalar | 128×128 → 64×64 | separate | 0.046678 | 0.046684 | 351.0 | 351.0 | 1.00× |
| NEON | 128×128 → 64×64 | separate | 0.026781 | 0.016774 | 611.8 | 976.8 | 1.60× |
| scalar | 128×128 → 64×64 | in place | 0.046626 | 0.046617 | 351.4 | 351.5 | 1.00× |
| NEON | 128×128 → 64×64 | in place | 0.026629 | 0.016679 | 615.3 | 982.3 | 1.60× |
| scalar | 33×35 → 17×18 | separate | 0.003606 | 0.003604 | 320.3 | 320.5 | 1.00× |
| NEON | 33×35 → 17×18 | separate | 0.002153 | 0.001503 | 536.5 | 768.4 | 1.43× |
| scalar | 33×35 → 17×18 | in place | 0.003488 | 0.003484 | 331.1 | 331.5 | 1.00× |
| NEON | 33×35 → 17×18 | in place | 0.002022 | 0.001361 | 571.2 | 848.7 | 1.49× |
| scalar | 7×5 → 4×3 | separate | 0.000158 | 0.000158 | 222.0 | 221.9 | 1.00× |
| NEON | 7×5 → 4×3 | separate | 0.000184 | 0.000158 | 190.1 | 221.4 | 1.16× |
| scalar | 7×5 → 4×3 | in place | 0.000158 | 0.000158 | 222.1 | 222.1 | 1.00× |
| NEON | 7×5 → 4×3 | in place | 0.000183 | 0.000158 | 190.9 | 222.1 | 1.16× |

## Reproduction

```sh
make tests SIMD_ARGS='--baseline 8d431ef'
MS912X_BENCH_DOWNSCALE_ONLY=1 taskset -c 15 make benchmark \
  SIMD_ARGS='--baseline 8d431ef --build-dir /tmp/ms912x-downscale'
```

Choose an available CPU. Cross-build with the existing `--arm` or `--arm32`
options, then execute the binary natively on the target for timing. Use QEMU
only for correctness. See [harness instructions](README.md).
