# RGB565 primary plane and USB stream implementation plan

> Historical design plan. The current implementation follows the later decision:
> native RGB565 stays packed at 16 bits through capture, scan, snapshots, scaling
> and staging, and is copied/masked to RGB565 output. XRGB8888 keeps its UYVY
> output path. RGB565 always ignores the wire-mask bits during scan, including
> tolerance 0. The original alternatives below are retained as planning context.
> See [packed benchmark results](../tools/rgb565_bench/PACKED_RESULTS.md).


Status: planned; no driver implementation in this document.
Reviewed against `72ee469` on 2026-10-21.

## Target behavior

- Advertise `DRM_FORMAT_RGB565` (`RG16`, little endian) first on the primary
  plane, with linear `XRGB8888` retained for clients that select it.
- Send RGB565 over the existing mode-3 USB rectangle protocol for both primary
  formats. Native RGB565 avoids RGB-to-YUV conversion; XRGB8888 is packed to
  RGB565 only for the rectangles being encoded.
- Keep native RGB565 packed through capture, scan, snapshots and staging.
  Reuse the scan algorithms through format specializations selected outside
  their loops.
- Support the current scale settings, cursor plane, damage replay, queue
  behavior and profiles before advertising the new primary format.

RGB565 and UYVY both occupy two USB bytes per pixel. The expected benefit is
less CPU and memory work for a native RGB565 framebuffer. USB bandwidth for
the same rectangles does not decrease. RGB565 provides independent color for
each pixel, with 5/6/5-bit precision; improvements to colored text and the
effect on gradients need visual comparison.

## What the supplied desktop drivers establish

The existing annotated disassemblies were checked, and their binary and
assembly hashes match `drivers/partial-update-analysis/evidence.json`.
No vendor executable or USB operation was run during this review.

| Evidence | Located behavior |
| --- | --- |
| Windows 1.0.0.7, `0x4472A6–0x4472DB` | Packs BGRA source bytes to little-endian RGB565; replaces low byte `FF` with `FE`, high byte `FF` with `F7`. |
| Windows 1.0.1.6, `0x446BB6–0x446BEB` | Repeats the same packing and byte substitutions. |
| macOS `_changeRGB888to565`, `0x10008F8C0` | Same little-endian RGB565 packing and substitutions, compacting four source bytes into two output bytes. |
| macOS `frame_data_convert`, `0x10008F760` | Chooses this RGB565 converter for `run_mode=1`; other modes select RGB888 or UYVY. The bundled LaunchAgent selects UYVY. |
| macOS configuration, `0x1000A3681–0x1000A3693` | Calls `set_video_in(width, height, 0x00, 0x00)` for RGB565. |
| Windows parameter builder and serializers | RGB565 uses USB format 0 and device-memory format 0; `BYTE_SELECT` is 0. Rectangles retain FF00/FF80/FFC0 framing. |

The resulting packed word, before the substitutions, is:

```c
rgb565 = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
```

These paths establish RGB565 USB support. Their capture inputs are still
32-bit pixels; native DRM RGB565 input is the additional work in this driver.

Binary SHA-256 values:

- Windows 1.0.0.7: `85af24188a2040f61f008c50620835b9ddcea0f4c1707447ec11002d55ed134e`.
- Windows 1.0.1.6: `8b72b27c2f493dc433764e977ab939cd491dc6289bc068c52cbcfd9ae547a345`.
- macOS: `ba78cf46865a6f145bf1c3ecbd802c66c6ea2e1d21d40383cf85c3954449e9e6`.

Reproduction and surrounding traces are indexed in the
[Windows analysis](windows-vs-android-macos-analysis.md),
[macOS analysis](macos-vs-android-analysis.md), and
[input/output configuration analysis](video-in-limits.md).

### VIDEO_IN and VIDEO_OUT remain distinct

The RGB565 USB image and RGB565 device-memory format are selected by:

```text
A6 01 WH WL HH HL 00 00    VIDEO_IN: RGB565 input and memory, byte select 0
```

`VIDEO_OUT` still carries the output VIC and its separate output-format field:

```text
A6 02 VIC OUT_FORMAT WH WL HH HL
```

The current Linux driver programs `VIDEO_IN=0x22` and `OUT_FORMAT=0x01`.
macOS uses `OUT_FORMAT=0` for its normal configuration across input formats;
Windows uses port-dependent policy, including reading register `0x0033` for
HDMI. These traces do not establish that changing the USB image to RGB565
requires changing the Linux `OUT_FORMAT` byte to zero.

Implement RGB565 through `VIDEO_IN` and the payload encoder. Retain the current
output VIC, dimension policy and `OUT_FORMAT` initially. If hardware validation
finds an output-format dependency, investigate it separately against the
Windows register policy and macOS zero value. Do not derive HDMI link color
depth or connector `max_bpc` from the framebuffer's 16-bit packing.

### Native RGB565 still needs the vendor byte substitutions

Apply these rules to pixel payload bytes, after cursor/debug composition:

- Low byte `FF` becomes `FE`, clearing blue bit 0.
- High byte `FF` becomes `F7`, clearing red bit 0 in the packed word.
- For example, white `FFFF` becomes word `F7FE`, transmitted as `FE F7`.

This is a lossy substitution found in all three desktop converters, rather
than a reversible escape sequence. Avoiding confusion with command tokens is
an inference; the substitutions themselves are explicit in the code.

Fuse the operation with the native RGB565 payload copy and the XRGB8888 packer.
An SSE2 implementation can compare bytes with `FF`, AND the result with the
repeated byte mask `01 08`, and clear those bits in the copied value. AVX2 and
NEON can perform the equivalent operation. A cursor/debug overlay that writes
after encoding must sanitize its affected payload region again. Do not
sanitize headers, trailers, source framebuffers or persistent scan baselines.

Extend the hardware quirks documentation with this RGB565-specific behavior;
the existing RGB888 FF-to-FE note does not describe it fully.

## DRM preference and fallback

Once both paths work, advertise primary formats in this order:

```c
DRM_FORMAT_RGB565,
DRM_FORMAT_XRGB8888,
```

Set `mode_config.preferred_depth = 16` for the dumb-buffer preference and call
`drm_client_setup(dev, drm_format_info(DRM_FORMAT_RGB565))` for the in-kernel
client. These mechanisms are present in the current kernel headers. Keep the
ARGB8888 cursor plane separate.

The format list advertises support. Its ordering is a best-effort hint, not a
KMS contract that forces a compositor to choose RGB565. The depth capability
applies specifically to dumb buffers, while `drm_client_setup()` controls the
kernel client. Verify the actual primary plane's `format=RG16` in debugfs and
test a direct KMS RGB565 framebuffer if the compositor continues selecting
XR24. Do not describe XR24 scanout as native RGB565.

Primary kernel references:
[dumb-buffer depth preference](https://www.kernel.org/doc/html/next/gpu/drm-uapi.html#drm-cap-dumb-preferred-depth),
[kernel-client format request](https://github.com/torvalds/linux/blob/v7.2/drivers/gpu/drm/clients/drm_client_setup.c),
[KMS format handling](https://www.kernel.org/doc/html/v7.2/gpu/drm-kms.html#drm-format-handling).

## Buffer and queue design

Keep the internal image format equal to the primary framebuffer format:

| Primary format | Snapshot/scan storage | Rectangle encoding |
| --- | --- | --- |
| RGB565 | Packed little-endian RGB565, 2 bytes/pixel | Copy with vendor byte substitutions |
| XRGB8888 | Existing 4-byte pixels | Pack selected rectangles to RGB565 with substitutions |

This avoids expanding native RGB565 to XRGB8888. It also avoids packing every
XRGB8888 frame before scanning, which would add work even when most pixels
are unchanged.

Introduce a small immutable layout description containing fourcc, bytes per
pixel and internal row stride in bytes. Capture it with every scan slot and
conversion request. Replace the `latest_xrgb`/`conversion_xrgb` naming with
neutral pixel-buffer names and make row addressing explicit. Honor framebuffer
pitch and the mapped plane origin when capturing damage.

Initially retain allocations large enough for the 4-byte fallback, using a
packed 2-byte row stride for RGB565. This reduces bytes touched without adding
buffer reallocation to format changes. Buffer capacity is distinct from the
active layout, and every pointer swap must retain that distinction.

A primary-format change can occur without a CRTC modeset. Treat it as a new
frame epoch: coordinate existing producers, reject old-layout work, invalidate
scan and payload histories, and force a complete refresh. Include layout in
request validity checks. Keep the recent lock boundaries: staging and scanning
remain outside publication locks, using metadata captured before unlocking.

Update [payload comparisons](../ms912x_payload.c): all RGB565 bits are
significant; the XRGB8888 ignored-X-byte fallback must not be used on RG16.

## Reuse the scan algorithms

Extend the current scan template with compile-time pixel type and bytes-per-
pixel constants, and retain the existing SIMD XOR/reduction/copy operations.
Select the format-specific row operations once per scan batch, outside the
pixel loops. Keep the shared rectangle builder, stride coverage, selective
baseline publication and previous-damage replay.

- AVX remains AVX1; RGB565 scanning does not require AVX2.
- Retain the two independent masked blocks per AVX iteration.
- Keep block sizes in bytes: a masked/memcmp 128-byte block covers 32 XRGB8888
  pixels or 64 RGB565 pixels. AVX exact blocks remain 256 bytes.
- Larger RGB565 damage blocks are an accepted performance tradeoff, but measure
  the resulting rectangle area and USB traffic alongside scan time.
- Exact RGB565 uses every bit and needs no X-byte stability probe. Keep that
  probe and fallback rules specific to XRGB8888.
- Keep the memcmp implementation delegated to the kernel's memcmp, including
  its 128-byte blocks.
- Preserve pixel coordinates and pixel counters at the row-result boundary.
  Handle an odd final RGB565 pixel with a bounded two-byte access.

### Tolerance remains on the existing 8-bit channel scale

Keep the public buckets `0, 1, 3, 7, 15, 31` and quality tolerance 0. Translate
the ignored low bits into 5/6/5 component positions once per batch:

```c
ignored565 = ((bucket >> 3) << 11) | ((bucket >> 2) << 5) | (bucket >> 3);
mask565 = (u16)~ignored565;
```

| Public tolerance bucket | RGB565 comparison mask |
| ---: | ---: |
| 0 | `FFFF` |
| 1 | `FFFF` |
| 3 | `FFFF` |
| 7 | `FFDF` |
| 15 | `F79E` |
| 31 | `E71C` |

Repeat this mask across vector lanes. The first buckets collapse because
RGB565 has already discarded those low channel bits. Reusing the integer 31
directly as a native 5-bit channel tolerance would discard too much color.

Make the watermark policy aware of distinct effective masks within the user's
configured bucket range. Preserve its pressure/watermark behavior, avoid
intermediate unsupported tolerance values, and report both the requested
bucket and the effective RGB565 mask. Quality stays exact for the stored
RGB565 values.

## Downscale, cursor and output encoding

Native RGB565 cannot be passed to the existing XRGB8888 downscalers: their
byte-channel arithmetic would mix the packed components. Add native RGB565
load/unpack/filter/repack operations around the shared phase, crop and staging
logic. Keep the 2:1 box filter and the selected 3:2/5:4 cubic or area policy.
Define native component rounding/clipping in an independent RGB565 oracle.
Implement scalar, SSE2, SSSE3, AVX2 and NEON as applicable, with dispatch outside
the loops. Reuse coefficient arithmetic where possible; avoid a full-frame
RGB565-to-XRGB8888 temporary pass in the final native path.

The XRGB8888 fallback keeps its current downscalers, followed by RGB565 packing
only for outgoing rectangles. Preserve crop halos, odd edges, padded columns
and in-place staging behavior in both paths.

Keep ARGB8888 cursor storage. Add RGB565 cursor composition using the existing
alpha/premultiplication convention, quantizing the result to 5/6/5. Blend only
the affected cursor region and sanitize its final bytes. Add RGB565 debug
borders through the same output-format operations. Keep cursor-last batching
and the existing replay policy.

Split common rectangle framing from pixel encoding in
[ms912x_convert.c](../ms912x_convert.c). Retain FF80 between rectangles and
FFC0 on the last rectangle, with all rectangles in the same logical update.
Encoded payload size remains `width * height * 2 + 16` per rectangle.
Keep primary/wire formats explicit in debugfs, and use encoding/copy timing
names when no chroma conversion takes place. Reuse the SIMD backend-selection
infrastructure; defer removal or public renaming of the `fast_chroma` parameter
until compatibility behavior is agreed.

The scale=auto bandwidth model remains two bytes per pixel. Preserve its USB3
selection of scale=100 and the existing automatic 1280-pixel input-width limit. Test the
limit with RGB565 before proposing any format-specific relaxation.

## Ordered implementation and validation

All items below remain pending. Follow the existing implement, validate,
hardware-test, then user-confirmed-commit workflow.

1. [ ] **Introduce image layout metadata.** Neutral buffer naming, byte strides,
   slot/request layout capture, format-change epochs and payload comparisons.
   Keep XRGB8888 behavior working during this structural step.
2. [ ] **Add RGB565 scan specializations.** Reuse the scalar/memcmp/SSE2/AVX/NEON
   algorithms, add masks and watermark handling, and validate selective
   baselines and emitted stride margins.
3. [ ] **Add RGB565 USB encoding.** Implement native copy/substitution and
   XRGB8888 packing, RGB565 cursor/debug drawing, and `VIDEO_IN=0x00`.
   Validate hardware with the existing XRGB8888 primary first, isolating the
   wire-format change from native framebuffer capture.
4. [ ] **Complete native capture and scaling.** Add RGB565 full-frame and
   damage capture plus the 50/67/80 downscalers. Validate each backend against
   an independent oracle and compare native versus fallback end to end.
5. [ ] **Advertise and prefer RGB565.** Add RG16 first, depth/kernel-client
   preferences, direct KMS tests and compositor inspection. Publish the
   hardware findings and benchmark results before retiring unused UYVY code.

Required fixtures and measurements:

- Exhaust all 65,536 RGB565 words against the vendor byte-substitution rule;
  check header/trailer bytes are untouched, including payloads containing
  `FFFF`, `00FF`, `FF00` and command-like byte sequences.
- Check both primary formats, padded pitches, mapped origins, unaligned rows,
  odd widths, tails, crop halos, empty sizes and guard pages.
- Check format changes without modesets, queued scans, request reuse, full
  refresh after invalidation, disconnect/recovery and delayed workers.
- Check tolerance 0 and all buckets, adaptive watermark transitions, isolated
  changed pixels/rows, selective baselines, stride margins and cursor repairs.
- Benchmark all scan methods with unchanged/sparse/dense changes at full-frame
  and narrow widths. Record compared bytes, changed area and prepared USB
  bytes as well as latency and source Mpixel/s.
- Benchmark RGB565 copy/substitution and XRGB8888-to-RGB565 against current
  UYVY encoding, with and without cursor composition. Benchmark each scale
  with separate buffers and in-place output on x86 and real ARM hardware.
- On hardware, verify primary format RG16 explicitly; inspect USB control
  setup, color ramps/text, red/green/blue/white, scale=100 and every scaled
  mode, scrolling edges, cursor edges and first-frame latency.
- Treat the vendor substitutions' color change as expected; investigate
  additional color/geometry corruption separately. Do not infer end-to-end
  FPS gains from isolated scan or encoding throughput.
