# Packed RGB565 and XRGB8888/UYVY benchmarks

This suite tests and measures the current driver code. RGB565 sources contain
one **16-bit** word per pixel throughout; the output mask is `0xF7FE`.

## Running

```sh
make test-rgb565
make test-rgb565 RGB565_ARGS='--sanitize'
taskset -c 15 make benchmark-rgb565 RGB565_ARGS='--build-dir /tmp/ms912x-rgb565'
```

Choose an allowed CPU. The binary pins itself to the first CPU in its inherited
affinity mask. These targets are included in `make tests` and `make benchmark`.
`CC` selects the compiler; `RGB565_ARGS` passes runner options. A benchmark emits
encoding samples and `<csv-path>.pipeline.csv`, retaining every duration and
iteration count. The build directory also contains driver source hashes and
`extracted-functions.json`, identifying the source and hash of each extracted
function.

Cross-build for an ARM64 device with a matching libc/toolchain:

```sh
python3 tools/rgb565_bench/run.py --arch arm64 --cc /path/to/aarch64-gcc \
  --build-only --build-dir /tmp/ms912x-rgb565-arm64
scp /tmp/ms912x-rgb565-arm64/rgb565-bench root@board:/tmp/rgb565-bench
ssh root@board 'taskset -c 3 /tmp/rgb565-bench --bench --csv /tmp/rgb565.csv'
```

`--arch arm32` supports ARMv7 NEON hardware with a matching libc/toolchain.
QEMU is useful for correctness only; timing results require native hardware.
Neither the runner nor the binary changes or loads a kernel module.

## Production code boundary

The runner extracts conversion dispatch, scalar UYVY conversion, RGB565 masking,
rectangle encoders and public batch encoders **verbatim** from `ms912x_convert.c`.
It compiles the actual SSE2/SSSE3/AVX2 or NEON source files, integer NEON glue,
scan workers, availability predicates and rectangle index. Capture, scan row
processing, baseline preparation and rectangle vector growth also use verbatim
production functions or shared project headers.

Userspace shims supply kernel types, allocators, module parameters, CPU feature
bits, `may_use_simd()` and FPU entry/exit barriers. CPU features come from the
execution host. Benchmark controls set parameters and SIMD availability before
timing; production dispatch still executes inside the measured encoder.
Memory allocation/copies/comparison use userspace libc. The surrounding pipeline
sequence simulates a frame, rather than executing kernel workers or locks.

Independent byte and pixel oracles provide expected results. The suite does not
replace framing, output loops or dispatch with generated alternative algorithms.
There are no synthetic copy/mask kernels or experimental RGB565 AVX2 backend.

## Two measurements

### Encoding

Each timed call invokes the real public batch encoder with one rectangle,
including its validation, dispatch, header, source stride and final trailer.

- `scalar-mask`: production RGB565 with SIMD unavailable; four packed pixels per
  64-bit operation and bounded tails.
- `RGB565-auto`: production RGB565 with SIMD available. SSE2 on x86-64, NEON on
  ARM64/ARM32; rows narrower than eight pixels retain the scalar fallback.
- `UYVY-scalar`: production XRGB8888 encoder with `fast_chroma=scalar`.
- `UYVY-auto`: the same encoder with `fast_chroma=auto`.
- `UYVY-SSE2`/`SSSE3`/`AVX2` or `UYVY-NEON`: supported explicit chroma settings.

The RGB565 SIMD row helpers are inline, use two independent 128-bit vectors per
iteration (16 packed pixels), then one vector and bounded scalar tails. Pixel
processing is load/AND/store, without shuffles or data-dependent branches.
A shared inline tail handles at most seven pixels with fixed 4/2/1-pixel
loads/stores, and workers advance row pointers by stride. FPU sections process
at most 16 rows. RGB565 mask selection remains
independent of `fast_chroma`.

Nine rectangle sizes reuse one source/destination pair. The two largest cases
also rotate across eight pairs to reduce cache reuse. The row stride is 1920
native pixels, hence 3840 RGB565 bytes versus 7680 XRGB8888 bytes. Encoding-only
sources are random values in each native layout, rather than matched images.

### CPU pipeline

The two formats use matched source images (XRGB color and its native RGB565
quantization), alternating two frames to maintain genuine damage:

1. Capture the entire framebuffer with the production capture helper.
2. Optionally compare, prepare/update the baseline, construct damage rectangles
   and cover skipped-row margins using production scan functions.
3. Publish by buffer ownership and copy emitted rectangles to the conversion
   snapshot using the production staging helper.
4. Encode the complete rectangle list with the real public batch encoder.

Cases include full-frame scan disabled, full-frame stride 1, a 60% video region
at stride 2, a 64x64 primary-plane change and idle. The latter is a cursor-sized
primary change, excluding software cursor composition. The video region covers
60% of screen height. CSV records bytes produced by the final frame.

Three paths are measured:

- `XRGB8888-UYVY-auto`: production SIMD scan and automatic chroma dispatch.
- `RGB565-all-scalar`: SIMD unavailable for both scan and output masking. This
  control differs from the earlier pipeline's SIMD scan plus scalar-only mask.
- `RGB565-RGB565-auto`: production SIMD scan and automatic RGB565 mask dispatch.

Scan uses AVX1 (SSE2 on older x86 CPUs) or NEON and the existing 128-byte masked
blocks: 32 XRGB pixels or 64 RGB565 pixels. XRGB tolerance is 3; RGB565 tolerance
is 0 (wire-mask bits still ignored). Changed colors are detected by both masks.
The first forced full-frame update is excluded.

All pipeline timings use **scale=100**. Reducer correctness has separate channel
and phase oracles in `make test-downscale-5to4`; these timings do not establish
gains with software scaling enabled.

## Validation and timing limits

- `-O2`, no LTO or automatic loop/SLP vectorization. Driver capture/scan/dispatch
  objects use integer-only flags; SIMD objects have separate ISA flags.
- Seven samples per case, calibrated to roughly 30 ms; medians reported. Backend
  order rotates between samples. Clocks are not fixed.
- Independent framing/color/mask oracle, all 65,536 native values, 32 output
  alignments, vector/tail boundaries and inaccessible guard pages.
- Real batch API tests cover multiple rectangles, FPU row boundaries, maximum
  batch count, invalid inputs, rejected partial batches and SIMD-unavailable
  fallback. Only the final rectangle may receive the final trailer.
- Pipeline checks cover both halves of u32 scan words, block tails, native pixel
  bounds, stride margins, baseline state and pitched framebuffer crops.
- Userspace timing excludes real kernel FPU entry/exit, locks, scheduling, USB,
  compositor rendering and display refresh. It is not display FPS. Both wire
  formats remain two bytes/pixel.
- Driver allocations retain maximum XRGB capacity for format switches; RGB565
  uses the packed native view. Active bytes touched differ from reserved capacity.

See [PRODUCTION_RESULTS.md](PRODUCTION_RESULTS.md) for measurements with current
production dispatch and batch encoders. [PACKED_RESULTS.md](PACKED_RESULTS.md)
preserves the earlier packed measurements with experimental mask candidates;
[RESULTS.md](RESULTS.md) preserves the older widened-layout measurements.
