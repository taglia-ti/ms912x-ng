# Vendor version changes and relevance to ms912x

Comparison dated 2026-10-22, based on the locally supplied old/new packages,
their extracted code, and the ms912x working tree at that time. Its implementation
comparison is historical; in particular, the current primary plane accepts
XRGB8888/XBGR8888 and converts to UYVY, rather than exposing the earlier native
RGB565 path. Use the current source for present driver behavior. The detailed
[package review](vendor-drivers-review-2026-10-22.md) and
[Linux source review](vendor-linux-packages-review-2026-10-22.md) contain
package hashes, addresses, and reproduction steps.

This is a static comparison. No driver code,
vendor source, module state, or device settings were changed. No performance
or visible-output result is claimed.

## What changed between the supplied versions

| Package comparison | Older implementation | Newly supplied implementation | Relevance to our driver |
| --- | --- | --- | --- |
| Android 2.0.0.3 to 2.0.1.3 | Native mode-0 consumer sends register-style frame controls before pixels, then a ZLP | Detects a chip family; sends pixels/ZLP first, then either `A6 00` or the register-style reports | Useful evidence for a future mode-0 backend; does not justify adding A6 frame triggers to our mode-3 sender |
| macOS November 2020 to June 2022 build | Desktop worker calls the bulk wrapper directly; wrapper issues one payload transfer | Separate queued USB consumer; wrapper adds a ZLP; consumer has a conditional idle resend | ZLP is a concrete transport difference worth testing; queueing alone is already present here |
| Old Linux DRM tree to new DRM archive | XRGB8888 is packed into full 24-bit frames, mode 0, ZLP and A6 trigger | Copies XRGB8888 into desktop storage, compares damage, converts UYVY rectangles, mode 3, `FFC0` and ZLP; A6 trigger disabled | Independent source counterpart to our rectangle path, plus explicit damage retention and repair policies |
| Linux framebuffer 3.0.2.11 | No earlier framebuffer package supplied | Separate fbdev branch using mode 0, mapped pages and pan-display submission | A full-frame reference, not an upgrade that our DRM architecture needs to copy |
| Old Windows installer to MultiDev 1.0.1.60 | Two application variants; existing mode-3/5 and rectangle paths | Still mode-3/5 helpers and `FF80/FFC0`; adds packaged ARM64 display/bus variants | Mostly compatibility/package changes in the paths reviewed; no new compression benefit established |
| Old Windows installer to MSDisplay 2.0.1.7.3 | SDRAM-less setup selects mode 5 in the previously traced path | A setup branch selects mode 4 for SDRAM type 4; USB INFs also bind `345f:9132` | Research material for a separate SDRAM-less sender, not a switch to speed up current sparse rectangles |
| Windows 4.1.13.29 bundle | Earlier reviewed applications use uncompressed rectangle transport | 9133-targeted UMDF driver has a real TurboJPEG encode caller and encoding policy | Potential future compressed backend on compatible 9133 hardware; this is not a general MS912x protocol upgrade |

Names alone do not establish chronology or executable identity. In particular,
the newer macOS DMG still contains an executable identifying itself as
`1.0.2b.0`, MultiDev `1.0.1.60` contains `1.0.1.6` application resources,
and the Windows `4.1.13.29` bundle contains display DLLs version `4.2.4.7`.
The Windows 4.x and framebuffer packages are different branches, not steps
in a single linear progression of one driver.

## The important Linux DRM change

The old and new vendor DRM code expose XRGB8888 to applications. The major
change happens below that interface:

```text
Old: XRGB8888 -> packed 24-bit full image -> mode 0 -> ZLP -> A6 trigger
New: XRGB8888 -> desktop copy -> compare -> UYVY rectangle -> FFC0 -> ZLP
```

For a complete 1920x1080 upload, the old normal RGB888 path uses 6,220,800
pixel bytes. The new UYVY rectangle uses 4,147,216 bytes including framing,
about one third less. Partial rectangles can reduce the payload further.
These are byte calculations, not measured throughput or FPS.

Our driver already sends two bytes per pixel for both its UYVY and native
RGB565 paths. Consequently, the vendor's full-frame RGB888-to-UYVY saving
is not an additional bandwidth saving available to our existing paths.

The new vendor DRM sender uses **one bounding rectangle** and retains damage
in two alternating pending slots. Our sender retains the previous damage
vector and can send several separately positioned rectangles, ending
intermediate ones with `FF80` and the final one with `FFC0`.

For example, two 64x64 changes at opposite corners of a 1920x1080 image cost
16,416 bytes as two rectangles, versus 4,147,216 bytes for their enclosing
full-screen rectangle: approximately 253 times as many bytes for the latter.
This example excludes cursor and history traffic. It illustrates why copying
the vendor's single-bounds algorithm would lose useful behavior we already have.

The new source's two damage slots strengthen the evidence for retaining
coverage across submissions. They do not prove how firmware implements pages,
nor that exactly two host slots are the best policy for every adapter.

## What the 2026-10-22 driver baseline had

This assessment uses the working tree at HEAD
`c6631db17790ff7106575147c1adc566c4ca4044`, including its uncommitted Linux
changes. It is not a claim about an installed module or the committed tree
alone. File hashes are saved with the comparison evidence.

| Vendor mechanism | Current ms912x source | Assessment |
| --- | --- | --- |
| Mode-3 header, intermediate/final markers | `ms912x_convert.c`, `ms912x_mode3.h`, shared mode-3 core | Already implemented for multi-rectangle batches |
| UYVY rectangle conversion | `ms912x_convert_xrgb8888_to_uyvy_batch()` | Already implemented with scalar/SIMD paths |
| RGB565 pixel path | Native RGB565 primary-format choices and `ms912x_convert_rgb565_batch()` | Current tree already has a native path; do not replace it with the vendor DRM's XRGB-only interface |
| Damage history | `ms912x_conversion_prepare_batch()` and `ms912x_conversion_update_replay_locked()` | Already retained; preserve sparse coverage and refresh pixels from a coherent source |
| Queued preparation and USB submission | Separate scan, conversion and ordered USB workqueues; request generations and payload refresh | Already implemented; a larger vendor-style FIFO is not inherently an improvement |
| Failure carry-forward | `ms912x_usb_carry_failed_rect_locked()` and success-based clearing | More explicit than the vendor DRM's clear-before-send behavior; retain it |
| Thread/work draining | `ms912x_transfer_stop()` drains submissions and synchronously cancels work | Retain this rather than the vendor source's fixed 300 ms teardown sleep |
| Video re-enable after setup | `ms912x_display_set_mode()` sends STARTTRANS 1 and VIDEOON 1 | Already present in the inspected path; this comparison does not establish a missing re-enable bug |
| Chip signatures and flash custom timings | `ms912x_timing_register_base()`, `ms912x_timing_load_custom()` | Already used for timing discovery; not a complete family-specific transport dispatch |
| Adapter-side scaling | VIDEO_IN/VIDEO_OUT canvas selection in `ms912x_display_set_mode()` | Already implemented; smaller input versus larger output is also present in older vendor evidence |
| SDRAM detection and no-memory rejection | `ms912x_sdram_detect()`, modeset checks | Already explicit; keep the rejection until a separate bypass path is validated |

The workqueue and lifetime mechanisms above were inspected for this comparison;
their presence is not a complete concurrency proof. Likewise, pixel-format
advertisement does not prove which format a compositor currently selects.

## Mode-3 USB boundaries

The old macOS wrapper at `0x1000776C0` sends one payload transfer. The newer
wrapper at `0x10005D680` adds a separate zero-length transfer after it.
Importantly, the newer Linux DRM sender also submits a ZLP after its mode-3
`FFC0` packet. This makes the finding relevant to rectangle transport rather
than only Android's distinct mode-0 protocol.

Our `ms912x_usb_request_execute()` currently submits the scatter-gather
payload, waits, and checks status/byte count. That path has no separate ZLP
submission or explicit zero-packet request. Actual wire behavior should still
be checked in a capture rather than inferred solely from the call count.

For scale, one UYVY 4x62 rectangle
is 512 bytes including framing, while 4x64 is 528 bytes; 4x126 is 1024 bytes.
These are useful boundary cases for endpoints with the corresponding packet
sizes, not assumed properties of a connected device.

The vendor change does not demonstrate that a ZLP increases FPS,
is required on every revision, or fixes a particular observed display issue.

## Vendor idle repair policies

The new Linux DRM source marks both pending slots full after 2500 ms of
inactivity. The framebuffer branch resends its retained complete image on a
nominal two-second timer. The new macOS USB consumer also has a conditional
idle resend: when `_frame_type == 0`, it reuses its last packet after more
than 60 ms without a new queued packet. `_getCurrentTime` returns milliseconds;
the timeout is not 60 seconds. Other `_frame_type` paths differ.

These distinct policies show that vendor code sometimes repeats a static image.
They do not establish a universal repair interval. Linux replays prior damage
with the next update and has full-refresh recovery; Android's shared damage
owner also provides one bounded idle repair. See the
[current pipeline](uyvy-first-pipeline.md). A 1080p UYVY full image costs roughly
4.15 MB before framing.

## New timing and register evidence: mostly corroboration

The newer Linux code adds flash reads, a `modify1`/`modify2` timing decoder,
and EDID detailed-timing insertion. Its family-specific flash bases are
`0xFC50` for the 913x classification and `0x1C00` otherwise, with records at
base+`0x10` and base+`0x30`.

Our timing loader already uses those bases, markers, and record layout. It
checks transfer results and validates timing fields before advertising modes.
`ms912x_connector_add_custom_timing()` adds validated DRM modes. The new vendor
source therefore corroborates that implementation; copying its EDID rewrite
is not necessary to gain those modes.

The source also adds SFR/GPIO setup around timing discovery, with different
operations for its chip families. These are leads for specific compatibility
or resume problems. Their register names and writes do not establish a
safe universal initialization sequence. Some helper calls ignore failures,
so an unconditional port of this code would also import weak error handling.

## SDRAM bypass and JPEG evidence

Windows 2.0.1.7's mode-4 setup branch is a useful lead for SDRAM-less adapters.
The earlier mode-5 implementation is another, with different restrictions.
Neither establishes that the mode-3 packet builder can be reused unchanged
for bypass operation.

The Windows UMDF DLL's JPEG caller has INFs targeting `345f:9133`. Our USB
table also lists 9133, alongside other IDs; that does not establish JPEG
support on every device matching that entry, much less across MS912x.
The capability test, compressed wire envelope, firmware requirements,
latency, image quality, and CPU cost still need to be established.

The DLL's internal encoding selector 3 chooses its JPEG path. It must not be
confused with the older protocol's `TRANSMODE=3`, which selects manual blocks.
The newly supplied Linux packages still use uncompressed pixels even though
their tables include 9133.

The vendor source's ordinary size macro calculates 32 MiB for its no-memory
enum value; that does not establish working SDRAM-less mode-0/3 output. The new
macOS producer's FPS counter measures queue production, while DRM's software
vblank timer is independent of USB completion; neither measures visible FPS.

## Evidence and validation scope

`drivers/review-2026-10-22/comparison-evidence/` contains:

- `working-tree-snapshot.json`: HEAD and hashes of the inspected local driver
  and documentation files, including the uncommitted versions.
- `linux-transfer.diff`, `linux-hal.diff`, `linux-usb-control.diff`, and
  `linux-drm-plane.diff`: direct comparisons against the earlier Linux tree,
  accounting for the `ms9132.c` to `usb_device.c` rename.
- `android-controls.diff` and `android-device.diff`: reconstructed Java
  comparisons; important frame ordering is separately checked in native code.
- `index.json`: eleven hashed source/assembly excerpts mapping vendor behavior
  to our completion, queueing, formats, modesetting, timing, and teardown paths.
- `macos-idle-resend.asm`: the conditional resend branch and millisecond clock
  helper at `0x10005C65B–0x10005C79B`.

The package reviews retain the Android native frame sequence, old/new macOS
bulk wrappers, and Windows mode/JPEG evidence. The byte calculations here
can be reproduced from `2 * width * height + 16` per mode-3 rectangle.
No new build, module load, USB capture, or hardware experiment was performed.
