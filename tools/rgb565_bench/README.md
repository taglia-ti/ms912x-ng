# Packed RGB565 and XRGB8888/UYVY benchmarks

This tool extracts the current production RGB565 rectangle encoder and compares
native packed RGB565 output with the existing XRGB8888-to-UYVY workers. RGB565
sources contain one **16-bit** word per pixel throughout. The output mask is
`0xF7FE`. There is no widening/narrowing or RGB565-to-XRGB8888 conversion.

## Running

```sh
make test-rgb565
make test-rgb565 RGB565_ARGS='--sanitize'
taskset -c 15 make benchmark-rgb565 RGB565_ARGS='--build-dir /tmp/ms912x-rgb565'
```

Choose an allowed CPU. The binary pins itself to the first CPU in its inherited
affinity mask. These targets are included in `make tests` and `make benchmark`.
`CC` selects the compiler; `RGB565_ARGS` passes runner options. A benchmark emits
encoding samples and `<csv-path>.pipeline.csv`, with every duration and iteration
count retained. It also preserves a SHA256 manifest of driver C sources/headers.

Cross-build for an ARM64 device with a matching libc/toolchain:

```sh
python3 tools/rgb565_bench/run.py --arch arm64 --cc /path/to/aarch64-gcc \
  --build-only --build-dir /tmp/ms912x-rgb565-arm64
scp /tmp/ms912x-rgb565-arm64/rgb565-bench root@board:/tmp/rgb565-bench
ssh root@board 'taskset -c 3 /tmp/rgb565-bench --bench --csv /tmp/rgb565.csv'
```

`--arch arm32` also supports ARMv7 NEON hardware with a matching libc/toolchain.
QEMU is useful for correctness only; timing results require native hardware.
Neither the runner nor the binary changes or loads a kernel module.

## Two measurements

### Encoding

All variants retain production rectangle validation, header, stride and trailer.
The production backend (`scalar-mask`) copies/masks four packed pixels per
64-bit integer operation, followed by bounded tails. `scalar-copy` removes the
mask as a control; `scalar-row` processes one 16-bit pixel per iteration.

Experimental SSE2, AVX2 and NEON kernels load, AND and store packed words.
There is no packing instruction or shuffle. SSSE3/SSE4.1 are unnecessary for
this operation. These candidates are **not** enabled in the driver. The UYVY
controls compile existing production scalar helpers and SIMD workers in the
same framing harness.

Nine rectangle sizes reuse one source/destination pair. The two largest cases
also rotate across eight pairs to reduce cache reuse. The row stride is 1920
native pixels, hence 3840 RGB565 bytes versus 7680 XRGB8888 bytes.

### CPU pipeline

Both paths use matched source images (XRGB color and its native RGB565
quantization), alternating two frames to maintain genuine damage:

1. Capture the entire framebuffer with the production capture helper.
2. Optionally compare, prepare and update the baseline, construct damage
   rectangles and cover skipped-row margins using production scan functions.
3. Publish by buffer ownership (no extra pixel copy) and copy emitted rectangles
   to the conversion snapshot using the production staging copy helper.
4. Encode every rectangle with the same framing harness as above.

The comparison includes full-frame scan disabled, full-frame stride 1, a 60%
video region at stride 2, a 64x64 primary-plane change and idle. The latter is a
cursor-sized primary change, not software cursor alpha composition. The video
region is 60% of full-screen height, keeping the emitted byte counts matched
between formats. CSV records the actual bytes produced by the final frame.

Scan uses production AVX1 (SSE2 fallback on older x86 CPUs) or NEON and the
existing 128-byte masked blocks: 32 XRGB pixels or 64 RGB565 pixels. XRGB tolerance
is 3 and RGB565 tolerance is the minimum 0 (wire-mask bits still ignored).
Colors change enough to be detected by both masks. This measures steady state;
the first forced full-frame update is not included.

The baseline comparison is optimized XRGB/UYVY SIMD versus **production scalar
64-bit RGB565 masking**, with a third experimental SIMD-mask path for context.
All pipeline cases use **scale=100**. Downscale correctness is checked separately
by `make test-downscale-5to4`; these timing results do not establish gains when
software scaling is enabled.

## Controls and limits

- `-O2`, no LTO or automatic loop/SLP vectorization. Driver pixel/capture/scan
  harnesses use integer-only objects; SIMD objects have separate ISA flags.
  Timing/reporting code uses the normal userspace floating-point ABI.
- Seven samples per case, each calibrated to roughly 30 ms; median reported.
  Backend order rotates between samples. Clocks are not fixed and system load
  can affect samples.
- Correctness uses an independent byte/framing oracle, all 65,536 native values,
  32 output alignments, SIMD boundaries, tails and inaccessible guard pages.
- Pipeline checks exercise both halves of each u32 scan word, partial word-block
  tails, native pixel coordinates, stride margins, baseline state and pitched
  framebuffer crops. Payload fixtures independently check packed staging and
  ignored wire bits. RGB565 reducers have a separate channel/phase oracle.
- Userspace timing excludes real kernel SIMD entry/exit overhead, locks,
  scheduling/queueing, USB transfers, compositor rendering and display refresh.
  It is not measured display FPS. Both payload formats remain two bytes/pixel.
- Driver allocations retain maximum XRGB capacity for runtime format switches;
  RGB565 uses only the packed native view. The gains concern active bytes
  touched, not a claim that reserved allocation capacity was halved.

See [PACKED_RESULTS.md](PACKED_RESULTS.md) for current measurements.
[RESULTS.md](RESULTS.md) and its older CSV/logs preserve the historical widened
layout measurements, which must not be confused with the current path.
