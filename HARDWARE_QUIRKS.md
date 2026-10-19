# Adapter quirks

Practical notes for the MS912x/MS913x adapter family. Features vary by chip,
firmware, output port, and SDRAM configuration. See
[hardware capabilities](HARDWARE_CAPABILITIES.md) for the feature summary.

Evidence priority: Windows > macOS > Android. Implemented paths in those
packages are treated as supported. The MS9132 Linux source provides hints
only. Internal buffering explanations remain hypotheses where noted.

## Vendor partial updates can briefly restore stale pixels

Sending a rectangle at A, then a separate rectangle at B, can make A briefly
revert. Full-image updates avoid this symptom.

The **Fibonacci-like workaround** is to send new damage together with the
previous update's damage, using the **latest pixels** for both:

```text
outgoing = new_damage union previous_damage
send current pixels covering outgoing
previous_damage = new_damage
```

For changes A, B, C, D, send A, A+B, B+C, C+D. Save only the new damage as
history; saving the outgoing union would accumulate older regions forever.
The union can be several rectangles in one batch, without sending the gaps.

Alternating retained image pages would explain this behavior, but the exact
internal page-switch mechanism is unresolved. Mode-3 headers contain no
explicit frame-0/1 field.

Details: [partial-update history](docs/partial-update-ordering.md).

The Linux driver follows the same history rule: each real update includes the
previous update's current-only damage, read from the latest pixels. It preserves
those rectangles separately in one mode-3 batch rather than merging them into
a bounding box. The history stores only current damage, not the outgoing replay
list, so it does not grow without bound.

## A batch can contain several rectangles

Use `FF80` after intermediate rectangles and `FFC0` only after the last:

```text
[header A][pixels A][FF80 trailer]
[header B][pixels B][FFC0 trailer]
```

Each header and trailer is eight bytes. Total batch size is
`sum(width * height * bytes_per_pixel + 16)`. No fixed hardware rectangle-count
maximum has been established.

USB request boundaries do not end a logical batch. Giving every rectangle
its own `FFC0` changes update boundaries. The Linux driver keeps current and
replayed framebuffer rectangles separate but sends them together with one final
`FFC0`. Cursor movement repairs the old cursor area and places the current
cursor rectangle last.

Desktop repairs normally follow top-to-bottom bands, left to right within
each band. This is a vendor convention, not an established hardware rule.
Draw a current cursor overlay after desktop repairs that could cover it.

Details: [batching, limits, and ordering](docs/mode3-multi-rectangle-batching.md).

## Rectangle geometry and pixel bytes need care

- X, Y, width, and height are 12-bit pixel values. Positive sizes encode
  1–4095 directly and must fit the configured input image.
- Width/height are sizes, not right/bottom coordinates. Pixels are tightly
  packed, with no transmitted row pitch or row padding.
- UYVY shares chroma between horizontal pixel pairs; preserve complete pairs.
  Windows also uses four-pixel horizontal alignment and can expand the left
  edge. This does not establish a universal four-pixel hardware requirement.
- In manual-block RGB888 paths, vendor implementations replace pixel byte
  `FF` with `FE`. This changes the color value; it is not reversible escaping.
  Do not apply this rule indiscriminately to other formats or raw FRAME data.

Details: [pixel representation](docs/partial-update-ordering.md#exact-batch-boundaries-and-size-nuances).

## Scaling belongs to the configured input image

VIDEO_IN describes the complete USB image; VIDEO_OUT selects the output timing.
They persist across rectangle updates. A rectangle uses input-image coordinates:
its own size does not redefine the scaler's input or output.

Scaled partial updates are supported in mode 3. Vendor implementations and the
Linux driver retain the previous-damage workaround when scaling. Arbitrary
scale ratios, filter behavior, and aspect ratio handling are not established.

VICs include vendor extensions: do not decode every value as a generic CTA VIC.
VIDEO_OUT dimension fields can retain input-sized values while its VIC selects
a larger output raster.

Details: [scaling](docs/transport-mixing-and-scaling.md),
[input limits](docs/video-in-limits.md), [output timings](docs/video-out-modes.md).

## SDRAM bypass and FRAME use different update rules

With SDRAM, mode 3 supports retained-image rectangle updates. The recovered
mode-5 bypass path instead sends a complete input image at `(0,0)`; sparse
mode-5 updates are not established. Its 800×600 setup restriction is a
vendor-path limit, not a proven universal silicon limit.

Mode 0 sends raw full images with separate page-related controls. Do not add
those controls to mode-3 batches or assume switching modes alone commits
previous rectangle data. A trailing zero-length USB transfer is path-specific.

Details: [bypass](docs/windows-vs-android-macos-analysis.md),
[mode switching](docs/transport-mixing-and-scaling.md).

## Reconfigure and recover with a complete image

Establish a complete image after setup, resets, and input-size changes; discard
old damage history. Vendor initialization repeats full images, consistent
with populating retained pages, but the exact required repeat count is unknown.

An incomplete transfer leaves adapter state uncertain. Do not advance damage
history as though the whole batch arrived; a complete-image resynchronization
is a conservative recovery choice.

USB completion is not a presentation acknowledgement. Output refresh rate does
not equal fresh images per second over USB. Software timeouts, buffer sizes,
and USB chunk sizes are not adapter capability limits.

Register bits can differ by chip and output port. Do not generalize a mute or
enable register from one variant to the entire family.
