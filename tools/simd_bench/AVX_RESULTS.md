# AVX scan compatibility and throughput

These measurements precede the independent-block pairing change in the AVX scanner.

## Result

The 256-bit scanner now requires AVX. The separate SSE4.1 scanner was removed.
The x86 scan backends are AVX and SSE2; scalar and memcmp remain available.
Chroma conversion and downscale retain their AVX2 implementations.

On this machine, the AVX build improves wide exact-scan throughput by 8-10%.
Wide masked scans range from 2.1% slower to 0.2% faster. Some short-row cases
regress by up to 6.7%. This supports the compatibility change with a measured
tradeoff for short rows. These results do not establish throughput on older CPUs.

## Method

- Date: 2026-10-21.
- CPU: AMD Ryzen 9 5900XT, pinned to logical CPU 15.
- Compiler: GCC 16.2.1, `-O2`, automatic loop and SLP vectorization disabled.
- Before: working-tree source snapshot saved before the ISA change, including
  the masked-XOR scanner and bucket policy. Its AVX2 scanner uses `-mavx2`.
- After: `ms912x_scan_avx.c`, compiled with `-mavx -mno-avx2 -mno-avx512f`.
- Both variants use the same scan policy, baseline updates and block sizes:
  256 bytes for exact comparison, 128 bytes for masked comparison.
- Four widths, three change patterns, exact and tolerance-31 masked scans:
  24 workloads. Each call processes 16 rows; the harness repeats calls on
  the same buffers. Sparse changes occur once every 67 pixels; dense changes
  invert all RGB bits. The unchanged case keeps every source pixel equal.
- Three separate benchmark runs, seven samples per variant per run, alternating
  execution order. The table uses the median of all 21 durations per variant.
- Pixel-loop measurements exclude kernel FPU entry/exit, frame snapshot capture,
  rectangle construction, USB transfer and display refresh. They are not FPS.
- Raw measurements: [results-avx.csv](results-avx.csv), containing 1,008 timings.

### Source identities

The baseline snapshot was `/tmp/ms912x-avx1-before`. Scanner SHA-256 values:

- Before: `9420d562b512f82d174e32a60fdcbee96590c80254fce23a70fb924515175113`.
- After: `b17a0a62a7462610b6cc258ce0a30245588d9e4fd7d0db00f9d464804f895bf8`.

The shared masked block template is identical in both source snapshots.

### Commands

Preserve the pre-change root C/header files as a source snapshot, then build
and compare the actual driver loops:

```sh
python3 tools/simd_bench/run.py \
  --baseline-dir /tmp/ms912x-avx1-before \
  --build-dir /tmp/ms912x-avx1-native --build-only

# Repeat with distinct CSV paths for each of the three runs.
MS912X_BENCH_BACKEND=AVX \
MS912X_BENCH_SAMPLES=/tmp/ms912x-avx1-run.csv \
taskset -c 15 /tmp/ms912x-avx1-native/verify --bench
```

## Correctness and compatibility

- Kernel module build against Linux 7.2.11-arch1-1 passed.
- `make tests` and `make test-capabilities` passed, including damage/baseline
  coverage, stride margins, threaded entrypoints and the dispatch matrix.
- Native AVX validation passed 1,792 scan cases, all byte-sized tolerance inputs,
  widths through 65,535 pixels, unaligned buffers and guard pages. Both versions
  match independent exact/masked oracles.
- QEMU Conroe, Penryn and Phenom checks passed with AVX safely skipped.
- QEMU Sandy Bridge and Ivy Bridge checks passed with AVX enabled and AVX2
  absent. Their current AVX scanner is checked against the oracle; the
  historical AVX2 scanner is skipped.
- AArch64 NEON correctness checks passed under QEMU.
- Kernel object inspection found AVX-compatible `VXORPS`, `VORPS`, `VPTEST`,
  loads/stores and memory-source `VBROADCASTSS`. The only `VPXOR` operations
  use 128-bit XMM registers. FPU entry/exit wrappers contain no vector
  instructions; `VZEROUPPER` remains inside the protected worker calls.
- No SSE4.1 or AVX2 scan symbols remain linked into the module.

Emulation checks compatibility and correctness. All performance numbers below
come from native execution on the stated Ryzen CPU.

## Measurements

Positive percentages indicate higher throughput for the AVX implementation.

| Workload | AVX2 Mpixel/s | AVX Mpixel/s | Throughput change |
| --- | ---: | ---: | ---: |
| `exact unchanged w=1920` | 14,356.8 | 15,788.1 | +10.0% |
| `bucket31 unchanged w=1920` | 9,652.5 | 9,619.0 | -0.3% |
| `exact sparse w=1920` | 14,368.3 | 15,866.0 | +10.4% |
| `bucket31 sparse w=1920` | 8,679.0 | 8,689.5 | +0.1% |
| `exact dense w=1920` | 14,386.5 | 15,827.7 | +10.0% |
| `bucket31 dense w=1920` | 9,313.5 | 9,117.5 | -2.1% |
| `exact unchanged w=640` | 13,276.3 | 14,481.6 | +9.1% |
| `bucket31 unchanged w=640` | 9,347.7 | 9,366.8 | +0.2% |
| `exact sparse w=640` | 13,441.7 | 14,525.5 | +8.1% |
| `bucket31 sparse w=640` | 8,625.9 | 8,629.9 | +0.0% |
| `exact dense w=640` | 13,371.6 | 14,489.7 | +8.4% |
| `bucket31 dense w=640` | 9,066.7 | 8,961.2 | -1.2% |
| `exact unchanged w=33` | 8,529.6 | 8,521.4 | -0.1% |
| `bucket31 unchanged w=33` | 4,583.3 | 4,740.4 | +3.4% |
| `exact sparse w=33` | 8,584.3 | 8,309.7 | -3.2% |
| `bucket31 sparse w=33` | 4,567.8 | 4,388.7 | -3.9% |
| `exact dense w=33` | 8,566.4 | 8,512.4 | -0.6% |
| `bucket31 dense w=33` | 4,364.4 | 4,138.6 | -5.2% |
| `exact unchanged w=16` | 5,224.9 | 5,049.0 | -3.4% |
| `bucket31 unchanged w=16` | 3,770.7 | 3,558.1 | -5.6% |
| `exact sparse w=16` | 5,219.8 | 5,043.9 | -3.4% |
| `bucket31 sparse w=16` | 3,663.0 | 3,589.4 | -2.0% |
| `exact dense w=16` | 5,208.1 | 5,033.2 | -3.4% |
| `bucket31 dense w=16` | 3,559.6 | 3,320.6 | -6.7% |

## Independent masked-block pairing

The AVX masked scanner now evaluates two independent 128-byte blocks per loop
iteration. Each block retains its own changed decision and next-baseline update;
reported damage granularity remains 128 bytes. Exact scanning is unchanged.

- Date: 2026-10-21; same Ryzen 9 5900XT, GCC 16.2.1 and pinned CPU 15.
- Before: `/tmp/ms912x-paired-scan-before`, the same AVX1 and masked-bucket
  implementation before pairing. After: paired AVX1 scanner.
- Three native runs, seven alternating samples per variant per workload; medians
  pool all 21 timings. The 12 masked workloads cover widths 1920, 640, 33 and
  16 with unchanged, sparse and dense input.
- At widths 640 and 1920, masked throughput changed by **+4.4% to +16.9%**.
  At widths 33 and 16 it changed by **-5.6% to -20.9%**. Exact throughput
  stayed within ±1.5%.
- The pairing is beneficial for full-width rows on this CPU. It has measurable
  overhead for short rows, where the pair loop does not run. Timing remains a
  pixel-loop microbenchmark, not an end-to-end frame-rate measurement.
- All three harness runs passed independent scan oracles, all tolerance values,
  wide-row cases and guard-page checks.
- Raw samples: [results-avx-paired.csv](results-avx-paired.csv), 1,008 timings.
