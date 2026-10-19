# MSDisplay macOS 1.0.2b: static analysis and comparison with Android

Static analysis performed on 2026-10-19 of the supplied DMG under
`drivers/`, compared with the supplied Android 2.0.0.3 APK
([analysis](android-driver-analysis.md)). No installer, vendor script, or
extracted executable was run. The USB adapter was not accessed.

The [VIDEO_OUT timing inventory](video-out-modes.md) decodes this application's
25 lookup/full timing records and compares their fields with Android, Windows,
and vendor Linux. Its tables omit Windows' 1600x1200 entry.

## Confirmed differences

The macOS implementation is the closest reference among the desktop packages
for this repository's Linux transfer path. Its active path selects
**`MANUAL_BLOCK = 3`**, sends changed regions in **UYVY**, and uses the
`FF 00` rectangle header and `FF C0` terminator also found in this repository.
The Android path analyzed here selects **`FRAME = 0`** and sends complete raw
frames with buffer-switch control reports.

The most useful additional framing detail is **`FF 80` as an intermediate
rectangle terminator**. macOS batches multiple rectangles into one bulk
transfer, putting `FF 80 00 00 00 00 00 00` after intermediate rectangles and
`FF C0 00 00 00 00 00 00` after the last one. This is used by the active
worker, not just by a test function.

| Aspect | Android 2.0.0.3 | Analyzed macOS 1.0.2b |
| --- | --- | --- |
| Active transfer mode | FRAME, 0 | MANUAL_BLOCK, 3 |
| Update unit | Whole frame | Changed regions plus cursor region |
| Framing | Buffer controls, raw pixels, trailing zero-length transfer | Rectangle header and FF80/FFC0 tokens in the byte stream |
| Bulk call | JNI: entire frame; Java fallback: 16 KiB chunks | One bulk call can contain several rectangles |
| Default pixel format | RGB888; JNI branches for RGB565/UYVY | LaunchAgent selects UYVY, 2 bytes/pixel |
| YUV conversion | ARM `RGB2YUV422` routine | libyuv `ARGBToUYVY`, CPU SIMD selected at runtime |
| Damage tracking | None found in active path | `GetDifs`, accumulated regions, union with previous region |
| Cursor | Included in Android screen capture | Separate callback and software composition when needed |
| Requested check interval | Latest-image capture callback | 40 ms for screen changes; 20 ms for cursor |
| Bulk timeout | JNI: 200 ms; Java: 1000 ms | 3000 ms |
| Bulk result handling | Native wrapper logs failure but returns zero | Wrapper returns actual libusb status |
| USB selection/open | Filter contains `534d:6021` and `345f:9132` | Initialization path fixes `534d:6021` |
| Active EDID read | None found | None found; `REG_EDID = C000` constant exists |

These code differences do not establish FPS, tear-free presentation, or
compatibility with every MS912x/MS913x device.

## Artifacts and reproduction

The DMG contains `MSDisplay.pkg` and install/uninstall scripts. The package
payload contains an executable, a LaunchAgent, and a log file; it does not
contain a `.kext` in this payload.

Executable: `usr/local/bin/screen_capture_msdisplay`, an x86_64 Mach-O of
2,569,924 bytes with C++ and Objective-C symbols preserved. Its internal
version string is `MSDisplay V1.0.2b.0`, build date `Nov 25 2020`.
`PackageInfo` declares `1.0.2a.0`, so installer metadata and executable
version do not match.

```text
DMG SHA-256:
16c49b8260f07fce864abe52c2f7cad29eed0a9516985e7028ae383fb78eac61

Executable SHA-256:
ba78cf46865a6f145bf1c3ecbd802c66c6ea2e1d21d40383cf85c3954449e9e6
```

Extraction and disassembly are under `drivers/macos-analysis/`, which is
already covered by the repository's ignore rules. Reproduce with installed
tools:

```sh
bash tools/macos/analyze-dmg.sh
```

The script uses 7-Zip, Python 3, `file`, LLVM, and `strings`. It does not
install tools or access the network or USB devices. It extracts DMG → XAR/PKG
→ gzip → CPIO without mounting the disk image or executing package scripts.

Key artifacts under `drivers/macos-analysis/`:

| Path | Contents |
| --- | --- |
| `dmg/` | Original package and scripts from the volume |
| `package/` | Installer metadata, Payload, and Scripts |
| `unpacked/` | Payload contents, including executable and LaunchAgent |
| `native/screen_capture_msdisplay.asm` | LLVM disassembly of the original executable |
| `native/annotated.asm` | Same assembly with imports and strings resolved |
| `native/symbols.txt`, `constants.txt` | Preserved symbols and values read from Mach-O sections |
| `native/functions/index.json` | Index of selected function excerpts |

Annotations are generated from the binary's import table and strings. They
are not decompiled C source. Addresses below are preferred virtual addresses
in the Mach-O, before ASLR.

## Path selected by the LaunchAgent

The LaunchAgent executes:

```text
/usr/local/bin/screen_capture_msdisplay 3 x
```

In `_main`, the first character of `argv[1]` selects:

| Argument | Internal `run_mode` | Pixel format |
| --- | --- | --- |
| `0` | 0 | RGB888, 3 bytes/pixel |
| `1` | 1 | RGB565, 2 bytes/pixel |
| `2` | 2 | Automatic: width >1280 selects 2 bytes/pixel; otherwise 3 |
| `3` | 3 | UYVY, 2 bytes/pixel |

This `run_mode` selects pixel conversion; **it is not the USB `TRANSMODE`
value**. The normal path calls `set_transfer_mode_manual_block` and sends
`A6 03 03 00 00 00 00 00`, including when `run_mode = 0`.

The `argc == 3` condition sets `_cfg_no_driver = true`. Therefore, the
LaunchAgent invocation takes the path without the optional virtual framebuffer.
The binary contains `ts::DisplayXFBInterface` and may call `displayx_init` in
other invocations; those symbols do not mean a virtual framebuffer driver is
included in this package or active by default.

Evidence: `_main` at `0x100094700`, argument selection at
`0x1000950EA–0x1000951B6`, manual-block mode call at `0x1000962EF`; extracted
LaunchAgent at
`unpacked/Library/LaunchAgents/com.ms.screen_capture_msdisplay.plist`.

## Shared USB control reports and configuration

Both Android and macOS use 8-byte reports with `wValue = 0x0300` and
`wIndex = 0`:

```text
SET_REPORT: bmRequestType=0x21, bRequest=0x09
GET_REPORT: bmRequestType=0xA1, bRequest=0x01

Read register:  B5 addr_hi addr_lo 00 00 00 00 00
Write byte:    B6 addr_hi addr_lo value 00 00 00 00
```

The read value is returned in byte 3. Addresses and command dimensions are
big endian. The A6 input, output, transmission, and video commands have the
same recovered structure as in Android. Two concrete differences:

* macOS `set_power_on` always places `02` in payload byte 3, including on
  power-off: `A6 07 00 02 00 00 00 00`. Android uses zero in that byte when
  powering off.
* In the normal macOS monitor-configuration callback, `set_video_out` gets
  zero as its second argument: `A6 02 VIC 00 W_hi W_lo H_hi H_lo`. Android
  selects a value based on port/configuration. The effect of this difference
  on output has not been established on hardware.

For input, macOS uses format byte `0x22` for UYVY, `0x00` for RGB565, and
`0x11` for RGB888. In the RGB888 branch for images larger than 4 MiB, it uses
`0x21`: RGB888 input and YUV422 memory, consistent with the nibble
interpretation recovered from Android.

Normal macOS initialization writes 1 to `DEEE`, powers on with `A6 07 01 02`,
waits one second, then clears bit 0 at `F600`. It selects manual-block mode.
When configuring a monitor, it disables video, sets input/output parameters,
sends `STARTTRANS=0` and then `STARTTRANS=1`, and enables video again. The
Android `C454` polling loop was not found in this macOS path.

References: `xdata_read` at `0x1000767B0`, `xdata_write` at `0x100076B10`,
`set_power_on` at `0x1000775A0`, monitor callback at `0x1000A07E0`, and video
configuration at `0x1000A35E5–0x1000A3711`.

## Rectangle payloads and FF80 framing

`ms2160_manual_block_package` at `0x100077710` writes:

```text
FF 00
(x >> 4)  ((x & 0x0F) << 4 | (y >> 8))  (y & 0xFF)
(w >> 4)  ((w & 0x0F) << 4 | (h >> 8))  (h & 0xFF)
pixels: w * h * bytes_per_pixel
FF C0 00 00 00 00 00 00
```

Position and dimensions each occupy 12 bits; the helper applies masks while
packing them. The worker calls this helper for each rectangle and changes the
second terminator byte to `80`. Once all rectangles have been assembled, it
restores that byte to `C0` on the final rectangle:

```text
[FF 00 + position/dimensions][pixels A][FF 80 00 00 00 00 00 00]
[FF 00 + position/dimensions][pixels B][FF 80 00 00 00 00 00 00]
[FF 00 + position/dimensions][pixels C][FF C0 00 00 00 00 00 00]
```

This shows how the host marks intermediate and final blocks. It does not alone
prove that firmware presents the full batch atomically or that batching avoids
tearing.

Evidence in worker `thread_func_usb_transfer` at `0x10008F9B0`:

| Address | Operation |
| --- | --- |
| `0x100090FC8` | Build one rectangle packet |
| `0x100091034` | Add the 8-byte terminator to accumulated length |
| `0x100091044` | Change the terminator's second byte to `80` |
| `0x100091711` | Build cursor-region packet when needed |
| `0x1000917AF` | Set `C0` on the last batch terminator |
| `0x100091939` | Send the accumulated batch through libusb |

The endpoint is bulk OUT `0x04`. This active path does not call Android's
FRAME-mode buffer-switch controls and does not append a zero-length bulk
transfer. Functions `ms2160_test_frame` and `ms2160_frame_trigger` exist, but
they are separate from the normal worker path.

A deeper follow-up found that the retained `ms2160_test_su` diagnostic
explicitly alternates modes 3 and 0, uploading 400x300 images with an
800x600 output VIC. This is deliberate mixed-mode and adapter-scaling test
code, although no normal capture call path invoking it was found. See
[transfer-mode mixing and scaling](transport-mixing-and-scaling.md) for
the complete command sequence, frame-buffer writes, and call inventory.

That follow-up now also traces the normal nearest-timing fallback: it can
retain a smaller input while selecting a larger output timing and continue
partial mode-3 updates. Its worked static example is 1152x648 input with
1280x720 output; the exact 720p-to-1080p mode-3 combination remains an
extrapolation rather than an explicitly recovered configuration.

The Linux driver currently builds one rectangle per payload with `FF C0` and
16 bytes of framing overhead. FF80 is a concrete reference for investigating
multiple rectangles per payload without changing global resolution. No such
change was made here.

## Damage tracking, cursor, and idle behavior

The program registers `onFrameChanged` and `onMouseChanged`. The virtual
registration call in `createframegrabber`, vtable offset `+0x18`, resolves to
`ScreenCaptureConfiguration::onFrameChanged`; this was resolved from the call
target, not inferred from function names alone.

The capture library calls `GetDifs` at `0x1000C5070`. It uses logical
**256x256-pixel tiles**, compares row sections with `memcmp`, marks changed
tiles, generates rectangles, and merges/sanitizes them. The comparison helper
is at `0x1000C53A0`. This is host CPU change detection, not video compression
on the adapter.

The callback copies pixels into a shared buffer and appends regions to
`_region_curr`. The worker sends the union of current and previous regions,
composes the cursor if the framebuffer does not include it, then saves the
current region as previous. Thus, regions from the prior update can be sent
again on the next update. The exact reason for this replay in relation to the
firmware's internal buffers remains an inference.

The [partial-update ordering follow-up](partial-update-ordering.md) traces
these assignments and packet boundaries in detail, confirms the same replay
strategy in both Windows versions, and relates it to stale-buffer flicker.

If there is no current region and no cursor change, the worker yields and
returns to its loop without sending the normal batch. Initialization also has
a path that sends a full black background; that initial transfer is not
continuous refresh of a stationary screen.

Configured intervals are **40 ms for screen changes** and **20 ms for the
cursor** (`0x1000935BA` and `0x100093649`). These request checks at 25 and 50
times per second; they are not measured rates of complete frames displayed.

## Pixel conversion, bulk timeout, and error handling

`frame_data_convert` at `0x10008F760` calls libyuv `ARGBToUYVY` in the default
mode. The converter at `0x1000F2390` selects scalar, SSSE3, and AVX2 routines
for Y/UV, and SSE2/AVX2 routines for UYVY packing, depending on CPU and width
alignment. This differs from Android's ARM converter. The outputs were not
compared bit-for-bit, and no performance benchmark was run.

In RGB888 mode, `_changeFFtoFE` at `0x10008F800` removes each pixel's fourth
byte and replaces color bytes `FF` with `FE`. The assembly proves the
replacement; avoiding collisions with manual-block markers is a plausible
explanation, not a documented firmware requirement. This replacement was not
found in Android's RGB888 frame sender.

`ms2160_bulk_transfer` at `0x1000776C0` uses a **3000 ms timeout** and returns
the libusb status. The worker resets its error counter after a nonnegative
return. After **six consecutive negative returns**, it logs a failure, sends
power-off, and exits the transfer thread. The wrapper does not verify that
`transferred` equals the requested length. Therefore, a zero return still
does not independently prove full receipt or presentation.

## Additional recovered registers

These names are symbols and constants preserved in the Mach-O data section.
They do not mean the normal worker uses every register:

| Address | Name preserved in binary |
| --- | --- |
| `F200` | `REG_VPACK_CLEAR` |
| `F202` | `REG_VPACK_TRANSFER` |
| `F203` | `REG_VPACK_SWITCH` |
| `F20C/F20D` | `REG_VPACK_BLK_HW_L/H` |
| `F20E/F20F` | `REG_VPACK_BLK_VH_L/H` |
| `C000` | `REG_EDID` |

This adds context to Android's write to `F202`; it still does not reveal all
bits or the exact conditions for switching buffers.

Other reads found in active code: `0031` for video port; `002D`, compared
with `AD` in the branch identified by string `MS2165 - VGA+HDMI Video Port!`;
and `003C`, whose value feeds `TimingTable::getResolutionByVic`. The latter
is a useful lead for the timing selected by firmware. It is not an EDID read
or a decoder for custom timing records in flash.

## Protocol support and limits in context

Across the supplied Android, macOS, and Windows implementations, the
8-byte `SET_REPORT`/`GET_REPORT` control setup and B5/B6 register operations
are consistent. All examined rectangle-sending desktop paths use bulk OUT
`0x04` in manual-block mode; macOS and Windows show FF80 between rectangles
and FFC0 after the last one. Android uses a different whole-frame mode with
raw pixels and a trailing ZLP. The untrusted MS9132 Linux source attempts
whole-frame transfers with a ZLP and an A6 frame trigger; its normal path
packs RGB888 USB pixels with YUV422 device memory. Those source details are
hints only. The independently inspected Android/Windows/macOS paths establish
transfer families under the project's vendor-support assumption, without
certifying all chip revisions or products.

No custom audio sender or audio routing to HDMI was found in this macOS
executable. Its AVFoundation capture symbols are not evidence of USB audio
transport. The attached `534d:6021` unit separately enumerates a standard
USB Audio Class interface, but macOS audio-class recognition and an audible
HDMI route were not tested. The saved descriptor/ALSA evidence and the
distinction between host audio and vendor video transport are summarized in
the [Windows/audio analysis](windows-vs-android-macos-analysis.md).

The macOS path's relevant limits and quirks are:

* fixed USB match `534d:6021` in the analyzed initialization path;
* manual-block rectangle coordinates and dimensions are 12-bit fields;
* bulk timeout is 3000 ms, and six consecutive negative libusb returns stop
  the transfer worker after a power-off request;
* transferred length is not checked against requested length;
* batches can contain multiple rectangles, with FF80 intermediate and FFC0
  final markers; no atomic presentation guarantee is established;
* capture/check intervals are 40 ms for screen damage and 20 ms for cursor,
  not delivered-frame rates;
* idle screens do not receive the normal repeated batch, except for an
  initialization path that sends a full black frame;
* no active EDID read was found, despite a `C000` symbol;
* this normal path has no Android-style FRAME buffer-switch command or ZLP;
* no custom audio transport was found in the vendor capture executable. The
  separately enumerated USB Audio Class interface is a host audio function,
  and its physical output route remains unverified.

The strongest Linux reference is the selected manual-block mode, matching
rectangle encoding, intermediate/final tokens, and tracking of prior damage
and cursor. Together with Android, macOS confirms the `TRANSMODE` and
`STARTTRANS` names for commands currently marked UNKNOWN in Linux. FF80
batching merits a separate investigation if reducing USB calls or sending
multiple regions per transfer is useful. Latency and tearing benefits require
a real USB capture and hardware observation; static code alone does not show
them.

The Linux production module files remain unchanged. Deliverables are this
report, the extraction scripts, and local evidence artifacts.
