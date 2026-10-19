# Transfer-mode mixing and adapter scaling

Follow-up static analysis on 2026-10-19 of the supplied Windows, macOS, and
Android drivers. This report treats implemented vendor paths as intended
capabilities, while distinguishing normal capture from retained diagnostics
and separating host scaling from scaling inside the adapter. No vendor
program or diagnostic was executed and no USB commands were sent.

The new finding is a macOS diagnostic named **Scale Up Test** that explicitly
alternates MANUAL_BLOCK 3 and FRAME 0 while keeping a 400x300 input image and
an 800x600 output VIC. The normal desktop paths do not make this alternation.
Android independently requests 1280x720 USB images with a 1920x1080 output
VIC, including an explicit output-selection branch.

## Where mode mixing exists

| Program/path | Traced behavior |
| --- | --- |
| Windows, both EXE variants, normal setup | Selects mode 3 if SDRAM type is not 4; mode 5 if it is 4. No normal mode-0 selection or 3-to-0 switching was found |
| Windows, mode-0 command branch | Can serialize A6 TRANSMODE 0, but the normal setup assigns 3 or 5 before reaching the dispatcher |
| macOS, normal LaunchAgent/capture | `_main` selects mode 3 at `0x1000962EF`; the capture worker sends rectangle packets without changing the transfer mode |
| macOS, `_ms2160_test_frame` | Selects mode 0 and sends raw test images with frame-trigger controls |
| macOS, `_ms2160_test_su` | Explicitly alternates mode 3 rectangle data and mode 0 raw image data within the same test loop |

The macOS `run_mode` argument is a pixel-conversion choice: run mode 0 means
RGB888 and does not select USB transport mode 0. Likewise, the normal libusb
setup's variable `_test_mode = 5` does not establish that transport mode 5
is used. The actual control-report calls determine the USB mode.

The test dispatcher `_ms2160_test`, at `0x100078280`, selects the scale-up
test for testcase 2. No direct caller of that dispatcher was found in the
disassembly. `_main` calls `_xusb_main` to open/inspect the adapter and
initialize its handle; the located `_test_device` path does not call the
diagnostic dispatcher. The retained test is protocol evidence, but it is
not established as part of normal screen capture or as an exposed command
line option.

## Exact macOS mixed-mode scale-up sequence

Function `_ms2160_test_su` starts at `0x100077970`. It configures input
400x300, RGB565, and output VIC `0x42` (800x600). The width/height arguments
to VIDEO_OUT are also 400x300, despite the larger timing selected by the VIC:

```text
A6 03 03 00 00 00 00 00    TRANSMODE = MANUAL_BLOCK
A6 01 01 90 01 2C 00 00    VIDEO_IN = 400x300, RGB565
A6 02 42 00 01 90 01 2C    VIDEO_OUT VIC = 800x600; dimension fields = 400x300
A6 04 00 00 00 00 00 00    stop transfer
A6 04 01 00 00 00 00 00    start transfer
A6 05 01 00 00 00 00 00    enable video
```

It reads the frame index from `F203`, inverts it, then repeats 100 cycles:

```text
Select mode 3: A6 03 03 ...
STARTTRANS=1
Bulk OUT 04: one 400x300 RGB565 rectangle, 240,016 bytes including framing
Wait 2 seconds

Select mode 0: A6 03 00 ...
STARTTRANS=1
Bulk OUT 04: raw 400x300 RGB565 image, 240,000 bytes
B6 F2 02 00 00 00 00 00
B6 F2 03 ID 00 00 00 00
B6 F2 02 01 00 00 00 00
Toggle ID between 0 and 1
Wait 2 seconds
```

Relevant call sites:

| Address | Operation |
| --- | --- |
| `0x100077A3E` | Initial manual-block selection |
| `0x100077A57` | VIDEO_IN 400x300 |
| `0x100077A73` | VIDEO_OUT VIC 42 with 400x300 fields |
| `0x100077B29` | Select mode 3 for the block phase |
| `0x100077B83` | Build 400x300 rectangle |
| `0x100077B94` | Send 240,016-byte rectangle packet |
| `0x100077BC2` | Select mode 0 for the raw-frame phase |
| `0x100077BE9` | Send 240,000 raw pixel bytes |
| `0x100077BFB`, `0x100077C2C`, `0x100077C41` | Transfer/switch/transfer writes at F202/F203/F202 |

This demonstrates sequential mode changes with the appropriate payload
framing for each phase. The loop does not send STARTTRANS=0 between its
mode changes, but it waits two seconds between phases; it is not evidence
of changing modes while bulk transfers are still outstanding.

The explicit frame-ID write belongs to the mode-0 phase. There is still no
frame-ID field in the mode-3 rectangle header. This test also does not prove
that switching to mode 0 alone commits a preceding rectangle update: the
test sends a complete raw image after selecting mode 0, before performing
the frame-buffer writes.

No explicit zero-length bulk call occurs in this diagnostic's raw-frame
phase. Android does append ZLPs in its normal FRAME path. The untrusted MS9132
source contains a ZLP as well, but provides hints only. A ZLP cannot be described as an invariant of
every recovered mode-0 sender.

## Adapter scaling: the strongest examples

### macOS: explicitly named 400x300-to-800x600 test

The test above sends exactly `400 * 300 * 2` raw pixel bytes, or that number
plus rectangle framing. It does not enlarge the image to 800x600 on the host.
The explicit output VIC is 800x600 and the dispatcher labels this function
`TC2 - Scale Up Test`. Taken as intended vendor behavior, this is a direct
reference for adapter scaling by two in both axes. The test alternates modes
3 and 0 with this configuration, showing that the scaling configuration is
used with both payload styles in the diagnostic.

This provides a stronger basis than a dimension mismatch alone. It does not
identify the interpolation filter or establish that every chip, port, and
firmware supports every input/output size combination.

### Android: 1280x720 USB input with a 1920x1080 output VIC

In `CaptureService.updateVideoParametersByConfiguration`, the default
HDMI/VGA/digital selection starts with VIC `0x81` and 1920x1080 dimensions.
When the app's `isUsb30` flag is false, it reduces the dimensions to 1280x720
without changing the output VIC. As covered in the Android report, this
flag is based on a `bcdDevice` string, not a measured negotiated bus speed.

There is also a separate, explicit YPbPr branch: selection `i2 == 0` assigns
VIC `0x81` (1920x1080) while setting both capture and input dimensions to
1280x720. This is not merely a default value that survived a fallback branch.

`getSize()` uses those input dimensions to size the ImageReader and Android
VirtualDisplay. The app therefore captures and sends a 1280x720 image, rather
than sending a 1920x1080 image labelled as 720p. `start_transaction()` passes
the smaller dimensions to VIDEO_IN and VIDEO_OUT while retaining the larger
VIC. On the phone path, the resulting command shapes are:

```text
A6 01 05 00 02 D0 FORMAT 00
A6 02 81 OUT_FMT 05 00 02 D0
```

`FORMAT` and `OUT_FMT` depend on the app's color/memory/port policy. On the
non-phone path, it can further reduce/crop the dimensions while keeping the
selected VIC, which is another use of independently configured image size
and output timing.

Together with the macOS scale-up test, this supports the interpretation that
the adapter can scale a smaller uploaded image into a larger output timing.
The VIDEO_OUT width/height fields must not automatically be interpreted as
the physical output raster: vendor code passes input-sized values there
while selecting a larger timing through VIC.

### Windows and normal macOS capture

The Windows mode-5 path similarly retains VIC `0x42` while programming smaller
scaled input dimensions. Its host scaler produces the reduced image first.
The mode-5 configuration-versus-pixel-converter discrepancy is documented in
the [Windows transport follow-up](windows-vs-android-macos-analysis.md).
No normal Windows 720p-input/1080p-output branch or active 0/3 mode mixing
was identified in the traced setup paths.

Normal macOS capture also has host-side scaling: `g_zoom` and Retina `scale`
affect source geometry, and the image callback calls libyuv `ARGBScale` at
`0x1000AAAC2`. The monitor-configuration callback derives VIDEO_IN dimensions
from `_res` and selects output timing through `res_idx`. A deeper trace below
resolves a normal nearest-timing fallback that retains a smaller input while
selecting a larger output timing, with the partial-rectangle sender still active.
The host `ARGBScale` call alone is not the evidence for adapter-side upscaling.

## Does upscaling require mode 0 or complete-image refreshes?

**Mode 3 supports adapter upscaling, including a normal capture path using
partial rectangles.** The exact 1280x720-to-1920x1080 combination is directly
present in Android's mode-0 path; the recovered mode-3 examples establish the
feature with other dimensions. Windows mode 5 also configures smaller input
with larger output timing, but sends a complete input image per update.

Transport mode and coverage of an update are separate decisions:

| Mode/path | Input to output | Coverage of each update | Conclusion |
|---|---|---|---|
| 0, Android | 1280x720 to 1920x1080 | Whole raw image | Explicit reference for this exact ratio |
| 3, macOS Scale Up Test | 400x300 to 800x600 | One rectangle covering the whole input | Adapter scaling works with mode-3 framing, before the mode-0 phase |
| 3, normal macOS nearest-timing fallback | Actual input dimensions to a selected larger timing | Changed rectangles plus previous damage and cursor | Vendor implementation combines adapter scaling with partial mode-3 updates |
| 5, Windows bypass | Configurable input no larger than 800x600 to an 800x600 timing | Whole scaled input, packed as a rectangle | Adapter scaling is configured in mode 5; sparse updates are not demonstrated |

### Why the mode-3 diagnostic does not depend on mode 0

At `0x100077A3E`, `ms2160_test_su` selects mode 3, configures 400x300 input
and output VIC `0x42`, and enables output. Its first loop iteration selects
mode 3 again, sends a 240,016-byte rectangle, and waits two seconds. Only
after that wait does `0x100077BC2` select mode 0 and send another image.

Thus the mode-3 scaling phase stands on its own: no following mode-0 frame
trigger is needed to explain the intended display of the first scaled image.
However, its rectangle covers the entire 400x300 input; this diagnostic alone
does not answer whether sparse patches can use the scaler.

### Normal mode-3 capture supplies the partial-update evidence

The normal program selects USB mode 3 at `0x1000962EF`. The monitor callback
`createframegrabber()::$_0::operator()()` handles the non-CVBS timing path as
follows:

1. Initialize `g_zoom` to 1.0. The non-CVBS branch changes it when capture
   height exceeds 1080 and derives `res_w/res_h` from the capture geometry
   (`0x1000A2F7F–0x1000A2FD1`).
2. Request an exact 60 Hz timing for `res_w/res_h` with `getVIC`
   (`0x1000A31D1`). If unavailable, use `getNearestVIC`
   (`0x1000A342A`), then save the selected VIC in `res_idx`.
3. Load that timing's dimensions into `_res`
   (`0x1000A34BC–0x1000A351B`).
4. **If capture width is at most 1280, replace `_res.width/height` with the
   actual `res_w/res_h` and set both offsets to zero**
   (`0x1000A3521–0x1000A355D`). The selected output VIC stays unchanged.
   The larger-width branch instead computes centering offsets; do not
   confuse that branch with the small-input scaler case.
5. Program VIDEO_IN from `_res` and VIDEO_OUT from the unchanged `res_idx`
   plus `_res` dimensions (`0x1000A361F–0x1000A36EA`). The usual UYVY
   conversion setting is `0x22`. No mode-0 transition is made here.

The timing helpers were inspected directly. `getVIC` requires exact width
and height. `getNearestVIC` at `0x1000A4730` considers timings at least as
wide and tall as requested, checks refresh compatibility, and minimizes
`(timing_width - input_width) + (timing_height - input_height)`, starting with
a score limit of 1000. It initially defaults to VIC `0x81`; that fallback is
not an explicit 720p-to-1080p policy.

A concrete worked example reconstructed from the compiled timing table is a
**1152x648, 60 Hz capture at scale 1.0**:

- It has no exact timing entry.
- The nearest lookup selects 1280x720, VIC `0x4F`, with score 200. The
  1152x864 entry scores 216; narrower or shorter entries cannot fit it.
- Because width is at most 1280, `_res` is replaced with 1152x648 while
  `res_idx` remains `0x4F`.
- In the UYVY path, the resulting setup has this shape:

```text
A6 03 03 00 00 00 00 00    USB mode 3, selected by normal startup
A6 01 04 80 02 88 22 00    VIDEO_IN 1152x648, UYVY
A6 02 4F 00 04 80 02 88    VIDEO_OUT 1280x720 VIC, 1152x648 dimension fields
```

This is a static reconstruction of a supported code path, not a capture of
an actual 1152x648 monitor session. The timing table's relevant constructor
calls include `0x10009AC94` (1152x864) and `0x10009ACFD` (1280x720).

The capture callback continues accumulating changed rectangles into
`_region_curr`. With `scale=1` and `g_zoom=1`, it takes the direct pixel-copy
branch, so no host upscale to 1280x720 supplies the missing output pixels.
The normal worker unions current and previous damage, clips against
`res_w/res_h`, and packs those individual rectangles; it neither expands
every update to the complete input nor switches to mode 0.

Taken under the vendor-code-as-supported-operation assumption, this establishes
**partial updates to a smaller mode-3 image while the adapter produces a larger
output timing**. It supports a retained input image followed by output scaling,
rather than independently stretching each damage rectangle to the full screen.
The latter placement inside the chip is an architectural inference.

### What remains specific to 720p-to-1080p and mode 5

No explicit mode-3 branch was found that deliberately selects VIC `0x81` while
programming input 1280x720. In normal macOS capture, an exact 1280x720 request
finds its own VIC `0x4F`; it does not automatically retain a 1080p output.
Using the Android dimension/VIC combination with mode 3 is therefore a well
supported implementation direction, but this exact mode/ratio/format combination
is not directly demonstrated by a recovered vendor path.

In Windows mode 5, the setup writes the smaller target dimensions into VIDEO_IN,
keeps output VIC `0x42`, and selects mode 5 before starting the full-image
rectangle sender. The host scaler reduces the desktop to the input dimensions;
it does not enlarge that USB payload back to the 800x600 output raster.
This is a vendor reference for mode-5 adapter scaling, subject to the format
interpretation issue already documented. The software restricts that path to
800x600 output, so it supplies no 720p-to-1080p mode-5 reference and no sparse
patches while scaling. Neither absence establishes a universal hardware ban.

For mode-3 implementation, configure the **whole input image size** and larger
output VIC once, then address patches within that input image. A patch's width
and height do not replace the configured input size. Continue the Fibonacci-like
damage replay and normal FF80/FFC0 batching in input-image coordinates. Transform
damage and cursor positions when the host reduces a larger logical desktop;
scaling does not remove the adapter-history requirement.

The focused evidence extractor now also saves the mode-3 scaling test, normal
scale policy, timing selection, small-input override, video setup, timing-table
constructor, and timing lookup functions under `drivers/partial-update-analysis/`:

```sh
python tools/extract-partial-update-evidence.py
sync
```

## Implications for our driver

These findings suggest two separate implementation opportunities:

1. Support a smaller USB source image while retaining the chosen display
   output VIC. For example, convert/downsample a logical 1920x1080 desktop
   into a 1280x720 upload and let the adapter scale it to the output timing.
   The vendor examples give a basis for exploring this while retaining
   manual-block mode 3; bypass mode 5 is not needed just to request scaling.
2. Support an explicit mode transition for a particular operation if it has
   a demonstrated benefit. The macOS test is a reference for switching
   between block and raw-frame modes, not a reason to mix them routinely.

At the same pixel format, 1280x720 uses 44.4% of the bytes of 1920x1080,
saving 55.6% before framing. UYVY payloads would fall from 4,147,200 to
1,843,200 bytes per full image. A bandwidth-only limit could therefore
improve by up to 2.25 times; host downsampling cost and other limits remain.
This trades image detail for bandwidth and should be an explicit policy.

Implementing smaller uploads requires tracking logical framebuffer size,
USB source size, and output VIC independently; transforming damage and cursor
coordinates; and expanding damage enough for the downsampling filter. It
should preserve the current damage and queue-management architecture.
The vendor code does not establish the hardware scaler's filter, exact
aspect-ratio/border behavior, available ratios, or vblank synchronization.

## Local evidence

`drivers/macos-analysis/mixing-scaling-followup/evidence.json` records the
Mach-O hash, direct-call inventory, test parameters, and selected assembly
excerpts. The directory also contains Android Java excerpts with original
line numbers. Existing complete disassemblies and the Windows mode-selection
evidence remain available under their respective analysis directories.

Related reports: [Android](android-driver-analysis.md),
[macOS](macos-vs-android-analysis.md),
[Windows](windows-vs-android-macos-analysis.md), and
[MS9132 Linux source](ms9132-vendor-linux-analysis.md).
