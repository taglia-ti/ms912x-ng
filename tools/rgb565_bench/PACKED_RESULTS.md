# Packed RGB565 versus XRGB8888/UYVY

These saved measurements predate production SSE2/NEON mask integration. The
driver now selects those backends when SIMD is usable, retaining scalar64 for
fallback and short rows. The timing tables below describe the earlier scalar
production path and experimental SIMD candidates. Updated measurements using
the integrated dispatch and batch encoders are in
[PRODUCTION_RESULTS.md](PRODUCTION_RESULTS.md).

Measured on 2026-10-21 from the uncommitted RGB565 patch set based on `72ee469`.

## Result and scope

Keeping native RGB565 packed removes the widen/narrow passes and halves the active
snapshot/capture/scan byte stride. The production output path copies and masks
four native pixels per 64-bit integer operation. It does not convert RGB565 to XRGB.

The CPU pipeline comparison below includes capture, optional scan and selective
baseline updates, skipped-row margins, staging copies and framed output encoding.
**All timing cases use `scale=100`.** They exclude USB, display refresh, real kernel
FPU entry/exit, locks/scheduling, compositor rendering and software cursor blending.
These are CPU processing times, not display FPS or a guarantee about scaled modes.

XRGB uses production AVX1 scan + AVX2 UYVY on x86 and NEON scan + UYVY on ARM.
RGB565 reuses the same scan ISA and uses its production **scalar 64-bit mask/copy**.
The third column path in raw CSV is an experimental SIMD mask, not driver dispatch.

Seven samples per case; numbers below are medians of elapsed time / iterations.
Every sample balances both alternating source frames. Video changes 60% of screen
height. Native RGB565 sources are quantized from the same XRGB source colors.
XRGB tolerance is 3; RGB565 is 0 (the two output-mask bits are still ignored).

Reduction = `1 - RGB565_time / XRGB_time`; speedup = `XRGB_time / RGB565_time`.
Thus a 2x speedup means 50% less CPU time, or 100% greater CPU throughput.

## AMD Ryzen 9 5900XT

x86-64, CPU 15 (SMT sibling 31), performance governor, GCC 16.2.1; Linux 7.2.11-arch1-1.

| Workload | XRGB → UYVY (ms) | RGB565 → RGB565 (ms) | Less CPU time | Speedup |
|---|---:|---:|---:|---:|
| 1080p-full-scan-off | 0.940 | 0.344 | 63.4% | 2.73x |
| 1080p-full-scan-1 | 1.711 | 0.615 | 64.1% | 2.78x |
| 1080p-video60-scan-2 | 1.616 | 0.482 | 70.2% | 3.35x |
| 1080p-cursor-scan-2 | 0.822 | 0.253 | 69.2% | 3.24x |
| 1080p-idle-scan-2 | 0.812 | 0.247 | 69.6% | 3.28x |
| 720p-full-scan-off | 0.374 | 0.170 | 54.5% | 2.20x |
| 720p-video60-scan-2 | 0.537 | 0.224 | 58.2% | 2.39x |

The production packed path saves **54.5–70.2%** of CPU time across these cases.

Raw data: [encoding](packed-x86.csv), [pipeline](packed-x86-pipeline.csv),
[native log](packed-x86.txt), [driver source hashes](packed-x86-source-sha256.json).

## Banana Pi R3 / MT7986 Cortex-A53

Native aarch64 CPU 3, OpenWrt 25.12.5, Linux 6.12.94; OpenWrt musl GCC 14.3.0.

| Workload | XRGB → UYVY (ms) | RGB565 → RGB565 (ms) | Less CPU time | Speedup |
|---|---:|---:|---:|---:|
| 1080p-full-scan-off | 20.173 | 9.903 | 50.9% | 2.04x |
| 1080p-full-scan-1 | 35.597 | 17.978 | 49.5% | 1.98x |
| 1080p-video60-scan-2 | 32.433 | 15.757 | 51.4% | 2.06x |
| 1080p-cursor-scan-2 | 20.595 | 10.473 | 49.1% | 1.97x |
| 1080p-idle-scan-2 | 19.990 | 10.611 | 46.9% | 1.88x |
| 720p-full-scan-off | 10.500 | 5.570 | 47.0% | 1.89x |
| 720p-video60-scan-2 | 16.129 | 8.576 | 46.8% | 1.88x |

The production packed path saves **46.8–51.4%** of CPU time across these cases.

Raw data: [encoding](packed-a53.csv), [pipeline](packed-a53-pipeline.csv),
[native log](packed-a53.txt), [driver source hashes](packed-a53-source-sha256.json).

## Encoding alone and SIMD candidates

| Target / case | Packed production scalar64 | Packed SIMD candidate | UYVY production SIMD |
|---|---:|---:|---:|
| x86 / 1920x1080 / reused-1 | 164.1 µs | 72.1 µs (AVX2) | 379.2 µs |
| x86 / 1280x720 / reused-1 | 79.0 µs | 35.8 µs (AVX2) | 176.8 µs |
| x86 / 1920x1080 / rotating-8 | 251.1 µs | 234.3 µs (AVX2) | 451.7 µs |
| x86 / 1280x720 / rotating-8 | 161.3 µs | 87.4 µs (AVX2) | 284.9 µs |
| a53 / 1920x1080 / reused-1 | 3169.7 µs | 3347.2 µs (NEON) | 6948.9 µs |
| a53 / 1280x720 / reused-1 | 1763.2 µs | 1838.9 µs (NEON) | 3542.8 µs |
| a53 / 1920x1080 / rotating-8 | 3223.9 µs | 3539.6 µs (NEON) | 7038.5 µs |
| a53 / 1280x720 / rotating-8 | 1768.8 µs | 1915.5 µs (NEON) | 3519.1 µs |

The A53 packed 64-bit path is already near a copy cost. NEON did not improve
the full CPU pipeline consistently; there is no reason to add a NEON mask dispatch
from these results. Native 16-bit SIMD no longer requires narrowing, interleaving
or pixel shuffles. SSE2/AVX2 candidates can reduce encoding time on x86; SSSE3
or SSE4.1 provide no useful extra operation for this load/AND/store loop.
They remain benchmark candidates rather than additional driver paths in this patch.

The older widened layout took about 7.27 ms for scalar RGB565 encoding of a reused
1080p frame on A53; the corrected packed path is around 3 ms. That historical
comparison uses a previous run and should not be treated as a paired experiment.
See [historical measurements](RESULTS.md).

## Correctness and practical limits

- `make modules`, `make test-capabilities` and the complete `make tests` suite passed with the final scan
  word/pixel addressing changes. Damage, entrypoint and reducer checks also passed
  under undefined-behavior/bounds sanitizers.
- Native x86 checks: 6152 encoding rectangles, all 65,536 RGB565 values per backend,
  output alignments/tails/guard pages, and 322 packed scan/capture cases.
- Native A53 checks: 4614 encoding rectangles and the same 322 packed scan/capture
  cases. Each benchmark runs correctness before timing.
- Payload/staging fixtures: 150 scenarios, including both packed half-word positions,
  ignored output bits and native-stride staging.
- RGB565 downscale oracle: 2010 geometries/crops, all ratios/channels/phases, edge
  padding, guards and unchanged source buffers on x86 and AArch64. Existing XRGB
  downscale coverage still passes. Downscale timings are not part of these tables.
- Undefined-behavior/bounds sanitizer checks passed for packed encoding/pipeline
  and the reducer oracle. Compatibility checks also passed under QEMU
  [Conroe](packed-validation-conroe.txt), [Phenom](packed-validation-phenom.txt)
  and [Sandy Bridge](packed-validation-sandybridge.txt); these are not timing data.
  Logs: [full suite](packed-tests.txt), [capabilities](packed-capabilities.txt),
  [final checks/module](packed-final-checks.txt),
  [ARM64 reducer](packed-scale-arm64.txt), [packed sanitizer](packed-sanitizer.txt),
  [reducer sanitizer](packed-scale-sanitizer.txt),
  [damage sanitizer](packed-damage-sanitizer.txt),
  [entrypoint sanitizer](packed-entry-sanitizer.txt).
- Allocations retain maximum XRGB capacity to allow runtime format switches.
  A RGB565 snapshot is packed at two bytes/pixel; u32 scan pointers describe two
  native pixels per comparison word, not a widened pixel slot.
- Timing is from userspace on live machines, with CPU affinity but no fixed clocks
  or exclusive system control. Small differences can be noise. Per-sample emitted
  byte counts match between paths; stride-2 margins alternate by one output row.
- Both wire formats use two bytes/pixel. USB-limited display FPS need not improve
  by the CPU speedup. Native RG16 display operation still requires visual validation
  after loading the module; these measurements do not establish hardware scanout.

Reproduction and methodology: [README.md](README.md).
