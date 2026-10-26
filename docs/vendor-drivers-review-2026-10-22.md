# Vendor driver review: new downloads, 2026-10-22

Static reverse-engineering review of the newly supplied Windows, macOS,
Android, and Linux packages in `drivers/`. This supplements the earlier
[Android](android-driver-analysis.md),
[macOS](macos-vs-android-analysis.md), and
[Windows](windows-vs-android-macos-analysis.md) reports.

The [version comparison](vendor-driver-version-comparison.md) maps the old/new
changes to our current driver and ranks useful experiments and compatibility work.

No vendor application, installer, package script, or driver was executed. No
USB device was opened and no hardware behavior was tested. Repository code was
left unchanged. Extraction, decompilation, and disassembly artifacts are kept
separately under the ignored `drivers/review-2026-10-22/` directory.

## Findings that change the previous picture

| Finding | Direct evidence | Meaning for further investigation |
| --- | --- | --- |
| Android 2.0.1.3 adds chip-dependent frame completion | `isMS913X()`, a new JNI boolean, and the native consumer's two control-report branches | Whole-frame completion is not uniform across this application’s supported chip families |
| The newer macOS wrapper appends a zero-length bulk transfer | Two calls to `libusb_bulk_transfer` in `_ms2160_bulk_transfer`; the second has length zero | A ZLP also appears after desktop rectangle batches, not only in Android's frame path |
| macOS separates batch production from USB submission | `_pool_add_worker` and `_thread_usb_transfer` | The producer's counter labeled FPS is not a USB-completion or display-presentation counter |
| The two new Linux packages use different active transports | Framebuffer initializes mode 0; DRM initializes mode 3 and packs UYVY rectangles | The new DRM package adds a source reference for vendor rectangle handling |
| Windows 2.0.1.7 adds a setup branch selecting transfer mode 4 for SDRAM type 4 | SDRAM read, state assignment, and `A6 03 04` serialization | The earlier Windows mode-5 bypass behavior is not the only supplied bypass implementation |
| The 4.1.13.29 Windows installer contains a separate 9133-targeted driver with a real JPEG encoding path | USB INF, UMDF DLL, encoding selector, and call into `tj3Compress8` | This is evidence for that driver generation, not proof of JPEG support on MS912x |
| Download names do not consistently identify the actual executable version | PE version resources, macOS version string, and PKG metadata | Identify an artifact by its hash and internal metadata before comparing addresses or features |

The familiar uncompressed mode-3 rectangle format remains present in the
new macOS binary and both older-generation Windows downloads. No finding here
establishes a new maximum resolution, rectangle count, display rate, or
tear-free presentation guarantee.

## Package identity and comparison scope

All sizes below are the original downloaded file sizes.

| Download | Bytes | Recovered identity |
| --- | ---: | --- |
| `MSDisplay_Android_V2.0.1.3.apk` | 2,883,954 | `com.ms.ms2160`, versionName `2.0.1.3`, versionCode 1 |
| `MSDisplay_MacOS_V2.0.0.1_20220606.dmg` | 1,364,998 | Executable still says `MSDisplay V1.0.2b.0`; build date `Jun 6 2022`; PKG version `1.0.0.1` |
| `MSDisplay_MultiDev_v1.0.1.60.exe` | 3,007,835 | Two `WinUsbDisplay.exe` variants, both resource version `1.0.1.6` |
| `MSDisplay_Windows_V2.0.1.7.3.exe` | 3,411,586 | Two `WinUsbDisplay.exe` variants, both resource version `2.0.1.7` |
| `WinUSBDisplay_Windows_V4.1.13.29.exe` | 4,396,352 | Manager `4.1.13.29`, UMDF display DLLs `4.2.4.7`, and a Windows-7 application `3.1.15.98` |
| `FrameBuffer_SourceCode_V3.0.2.11.zip` | 50,724 | fbdev source and demo; builds `usbdisp_fb.ko` |
| `MS91xx_Linux_Drm_SourceCode_V3.0.3.13.zip` | 79,033 | Extracted directory is `DRM_SourceCode_V3.0.3.12`; builds separate DRM and USB modules |

Original-package SHA-256 values:

```text
6a257f3e865eae783ffbb93167601f830ca8087e6ea31d0fa016716b72f02c57  MSDisplay_Android_V2.0.1.3.apk
32f2a58dff98c95dbf57baca6bd0d475da44d70e93aae5caceade47b44ada3e6  MSDisplay_MacOS_V2.0.0.1_20220606.dmg
f22255c6d52f89e94cad7ae5e303e52a38209f2d536902da6f9f532e7cbace24  MSDisplay_MultiDev_v1.0.1.60.exe
4c0008a6da0b5beaa05f3ab33e822fd49fbf581bb3235fd4279743048ddeb927  MSDisplay_Windows_V2.0.1.7.3.exe
9c81b78c180a3fd89be820aa6c62e22101ccb1a076390a49b7d182a4c4cdee62  WinUSBDisplay_Windows_V4.1.13.29.exe
```

The older APK, DMG, and Windows installer were retained as comparison inputs;
their hashes match the earlier reports. The new MultiDev executables are not
byte-identical to the earlier installer's executables, even where their
resource versions coincide. A shared version string is insufficient evidence
that two builds have identical behavior.

All four `WinUsbDisplay.exe` variants in the MultiDev/2.0 packages are PE32
x86 (`Machine=0x14c`), including variants whose PDB paths contain `arm64`.
The new MultiDev package separately contains actual ARM64 bus/display drivers
(`Machine=0xaa64`). The 4.x package contains x86, x64, and ARM64 UMDF display
DLLs. The new macOS display executable is x86_64 Mach-O, not a universal or
native ARM64 executable.

## Android 2.0.1.3

### The capture and configuration model remains recognizable

The APK still has minSdk 21, targetSdk 26, and compileSdk 28. Its USB filter
still lists `534d:6021` and `345f:9132`. Its native libraries remain limited
to `armeabi` and `armeabi-v7a`; there is no ARM64 library in this APK.

`CaptureService.updateGlobalParameters()` still selects `FRAME = 0`.
`displayInit()` still creates a MediaProjection capture display with flag
16, and `BulkSendSplit()` still has a Java fallback and a native path.
This remains screen mirroring. The new APK does not supply an independent
Android desktop implementation through this path.

Comparing reconstructed `com/example/msdisplay/MsDisplay.java` with the
older APK shows the same Java control implementations, with three new
native converter declarations and an extra boolean parameter on
`nativeConvertColorBulkTransfer`. In particular, the report layouts remain:

```text
SET_REPORT: type 21, request 09, value 0300, index 0000, length 8
GET_REPORT: type A1, request 01, value 0300, index 0000, length 8

B5 AH AL 00 00 00 00 00       read XDATA; value in response byte 3
B6 AH AL VV 00 00 00 00       write XDATA
A6 01 WH WL HH HL FMT ORDER   VIDEO_IN
A6 02 VIC OUT WH WL HH HL     VIDEO_OUT
A6 03 MODE 00 00 00 00 00     TRANSMODE
```

The existing RGB565/RGB888/YUV422 format choices, memory-format nibble,
four-pixel input-width rounding, and 720p-input/1080p-output configuration
remain visible in `updateVideoParametersByConfiguration()` and
`start_transaction()`. Their presence is not a new hardware-limit discovery.

### New chip-family detection reaches the native consumer

`USBDevice.isMS913X()` reads three bytes at `0xFF00`, `0xFF01`, and `0xFF02`.
It returns true for these signatures:

```text
A7 13 0A
B7 13 0A
```

The first address is rendered by JADX as
`MotionEventCompat.ACTION_POINTER_INDEX_MASK`; that constant is `65280`, or
`0xFF00`. It is a numeric coincidence in reconstructed Java, not an Android
input operation.

`CaptureService.onConnect()` stores the result in `isMS913x`, and the native
send path passes it through `USBDevice.converColortransferBulk()` to JNI.
The ARMv7 library exports a new `isms913x` object at `0x8008`. The JNI entry
stores the boolean through GOT slot `0x7ED0`; the consumer reads that same
slot. ELF relocations identify the slot as `isms913x`, so the Java-to-native
connection is supported by the binary as well as the declarations.

This is the application's family classification. It does not identify every
product revision, nor does it establish that a failed register read is safely
distinguished from an unsupported chip.

### Frame completion now has two native branches

The ARMv7 queued worker at `consumerThread`, address `0x53B8`, performs:

```text
raw full-frame bulk OUT
zero-length bulk OUT
if isms913x:
    A6 00 frame_id 01 00 00 00 00
else:
    12 F2 02 00 frame_id 00 00 00
    B6 F2 02 01 01 00 00 00
toggle frame_id
```

The data call is at `0x5514`, the zero-length call at `0x5530`, the family
test at `0x5544`, the A6 payload construction at `0x5550–0x5570`, and the
alternative reports at `0x55C0–0x56B4`. These are payload bytes reconstructed
from stores in the disassembly; `12` in the first alternative report is
literal and must not be replaced with `B6`.

This changes two details of the earlier APK's queued path: controls now
follow the pixel transfer/ZLP, and the detected 913x branch uses the A6 frame
trigger. In 2.0.0.3, the queued path sent the two register-style reports before
the pixels. The new Java fallback still invokes `frame_switch()` before its
16 KiB pixel chunks and final zero-length transfer. Consequently, the new
native ordering must not be generalized to every path in the APK.

The A6 command's active use is stronger evidence than the older APK's unused
Java `frame_trigger()` helper. Firmware presentation timing and the meaning
of the second parameter beyond its literal value `01` remain unverified.

### Native changes and decompilation limits

All four bundled `.so` files differ from their older-APK counterparts.
The ARMv7 `libmsusb.so` is 30,256 bytes, SHA-256:

```text
efa2f27dec1008a9aa96dd9d480bcec072440ad75004185bda5496200bc883e5
```

It adds JNI entries `RGB32ToRGB24`, `RGB32ToRGB565`, and `RGB32ToUYVY`.
Only their declarations were found in the reconstructed app Java; they should
not be described as the active capture path merely because they are exported.

JADX exited with status 3 and two `JADX ERROR` markers, in support-library
`DiffUtil` and `GridLayoutManager`. No `JADX ERROR` marker was found under
the app's `com/` sources. Warnings remain, including duplicated-code warnings
in `CaptureService`. The critical new control sequence above was checked in
native assembly rather than inferred solely from decompiled Java.

## macOS download labeled 2.0.0.1

### Packaging and the active desktop path

The extracted executable is 4,590,532 bytes, SHA-256:

```text
624629fd198dd3a46e7e6007f4b439dd9a35621273da898ce96d1e183646bf69
```

The payload contains the display executable, uninstall script, LaunchAgent,
and log file; it does not contain a `.kext`. The DMG additionally carries an
uninstaller PKG. The internal version/build strings and PKG version disagree
with the DMG's `V2.0.0.1` label, as recorded in the identity table.

The LaunchAgent still executes `screen_capture_msdisplay 3 x`, selecting
UYVY pixel conversion and the path without the optional virtual-framebuffer
driver. The normal startup still calls `_set_transfer_mode_manual_block`,
now at `0x10005D0E0`, from `0x10007A63A`.
It serializes `A6 03 03 00 00 00 00 00`. Argument `3` and USB mode `3`
are separate choices even though their numeric values happen to match.

`_ms2160_manual_block_package` at `0x10005D730` retains the eight-byte
`FF 00` header, packed 12-bit position/dimensions, raw pixels, and eight-byte
terminator. The active worker at `0x100076150` still combines damage history,
packs desktop rectangles, and appends a cursor rectangle when needed:

| Address | Operation |
| --- | --- |
| `0x1000767FC` | Add another region to the working damage region |
| `0x100076D4A` | Call the rectangle packer |
| `0x100076DBD` | Set an intermediate terminator byte to `80` |
| `0x100077430` | Pack the cursor rectangle |
| `0x1000774C5` | Set the final terminator byte to `C0` |
| `0x1000774DF–0x10007752E` | Update/clear region history |

These findings reinforce the earlier framing and history analysis. They do
not show a new rectangle-count field or an atomic-presentation guarantee.

### Batch production and USB submission are now separate

The older desktop worker called the USB wrapper directly. The newer worker
enqueues its assembled packet through `_pool_add_worker` at `0x100077649`
or `0x1000776C9`. The pool helper at `0x10005BF50` copies the packet into its
queue storage; the copy call is at `0x10005C078`.

Startup creates `_thread_usb_transfer`, address `0x10005C4C0`, as a separate
thread. That consumer obtains a queued packet through `_thread_routine` and
calls `_ms2160_bulk_transfer` at `0x10005C588`. A second call site at
`0x10005C69E` can resend the retained packet when the consumer has no new
packet and its `_frame_type` condition permits it. The producer still calls
the wrapper directly for its initialization uploads.

This matters when interpreting the new binary's logs: the producer increments
`_fps_bulk_transfer` at `0x1000778D1`, after the enqueue path. It is not
incremented by the final USB completion callback or by a display-vblank event.
The value cannot establish completed transfers or visible frames per second.
The queue's complete synchronization, overload, and teardown behavior was not
audited here, and its presence alone does not establish lower latency.

### New zero-length transfer after each bulk-wrapper payload

The old `_ms2160_bulk_transfer` at `0x1000776C0` issued one libusb call.
The new helper at `0x10005D680` issues two:

| Call | Endpoint | Length | Timeout |
| --- | --- | --- | --- |
| `0x10005D6BD` | `04` | Requested payload size | 3000 ms |
| `0x10005D70F` | `04` | Zero | 3000 ms |

At the second call, `ECX`, the libusb length argument, is explicitly zero.
This is a separate zero-length submission after the whole wrapper payload,
not another `FF C0` marker or a ZLP inserted between individual rectangles.

The helper attempts the second call even if the first call failed. It returns
the first call's status; the second call's status is stored but not returned.
It also logs the first call when its transferred count is nonpositive or its
status is negative. This is useful recovery/error-handling evidence, but
does not prove the extra transfer is required by every adapter revision.

The normal monitor-configuration callback still disables video, programs
VIDEO_IN/VIDEO_OUT, sends STARTTRANS 0 and 1, then re-enables video. Relevant
new addresses are `0x1000A0034`, `0x1000A007F–0x1000A0137`,
`0x1000A0149`, `0x1000A015E`, and `0x1000A028D`.
The later build therefore preserves an explicit reconfiguration sequence;
it does not establish which steps are mandatory for a Linux scaling change.

## Windows MultiDev 1.0.1.60 and MSDisplay 2.0.1.7.3

### Duplicate executable names and device bindings

Each installer contains two entries named `app/WinUsbDisplay.exe`.
Extraction with `--collisions rename` preserves them as `WinUsbDisplay.exe$0`
and `WinUsbDisplay.exe`. The addresses below refer specifically to `$0`.

| Package directory | `$0` bytes / resource version | Other EXE bytes / resource version |
| --- | --- | --- |
| `windows-10160` | 1,523,712 / `1.0.1.6` | 1,614,336 / `1.0.1.6` |
| `windows-20173` | 1,493,504 / `2.0.1.7` | 1,565,696 / `2.0.1.7` |

The MultiDev 1.0.1.6 USB INFs bind `534d:6021&MI_03`. The 2.0.1.7 INFs
add `345f:9132&MI_03` alongside it. Both packages still ship three numbered
indirect-display driver slots. This is installation structure, not proof of
three independent connectors inside one adapter.

The 2.0 package also bundles `libVMonitor.dll`, a DisplayProxy driver, and
dfmirage components. MultiDev 1.0.1.60 supplies ARM64 versions of its
indirect-display bus and three display DLLs. Those files should not be
confused with the architecture of the main applications.

### Familiar reports and rectangle framing remain

MultiDev 1.0.1.6's control wrapper at `0x44CCE0` uses eight-byte reports,
value `0x0300`, index zero, and a 600 ms timeout. Its mode wrappers at
`0x44CFE0` and `0x44D040` construct modes 3 and 5. The 2.0.1.7 control
wrapper at `0x44E920` also supplies eight-byte length, value `0x0300`, and
600 ms timeout; its register read/write helpers are at `0x44EB20` and
`0x44EAB0`.

Both retain `FF80/FFC0` rectangle batch tokens. Examples are the MultiDev
stores at `0x4466A9/0x4466B7`, and 2.0.1.7 stores at
`0x446263/0x446271` and `0x446805/0x446813`.
The 2.0.1.7 code also patches a final rectangle's terminator at `0x44625A`.
These observations confirm continued uncompressed rectangle support; they
do not imply that every branch sends rectangles.

### The 2.0.1.7 bypass setup differs from the earlier analysis

The SDRAM helper at `0x44E5B0` constructs a `B5 00 30` read.
Initialization first assigns the object's transfer-mode field to 3 at
`0x44F227`. In the branch that reads SDRAM type, value 4 is copied into
that field at `0x44F247`. Reconfiguration repeats the SDRAM check and
assignment at `0x44FACE–0x44FADB`.

The configuration serializer at `0x45070A–0x4507AA` handles modes 0, 3, 5,
and 4 explicitly. For the mode-4 case it writes:

```text
A6 03 04 00 00 00 00 00
```

Thus this is an actual mode-4 setup branch, not just an unused enum constant.
Android's name for value 4 is `MEM_BYPAS_FRAME`. The previous Windows report
found mode 5, `MEM_BYPAS_BLOCK`, for the old installer's SDRAM-less branch.
The new finding qualifies that conclusion by build and branch. The full
steady-state mode-4 sender, its accepted dimensions, and its recovery
requirements were not completely decoded in this review.

### INI values need to be checked against their callers

Both downloads include the same `config.ini`: bypass scale 400x200,
`pqmode=1`, `frame_switch_enable=3`, thresholds 42 and 18, and
`frame_time=3`. The comments describe dynamic RGB888/RGB565 switching,
but comments and values alone do not establish the selected runtime format.

There is a concrete lookup mismatch in 2.0.1.7's reader:

| Setting | Section supplied to `GetPrivateProfileIntW` | Default | Call address |
| --- | --- | ---: | --- |
| `frame_switch_enable` | `frame_swtich` | 0 | `0x443B2C` |
| `frame_avg_fre_0` | `frameframe_swtich_swtich` | 42 | `0x443B4F` |
| `frame_avg_fre_1` | Empty string | 18 | `0x443B72` |
| `frame_time` | `frame_swtich` | 3 | `0x443B95` |

The shipped INI places both thresholds under `frame_swtich`, so the reader
does not request them from that section. The first incorrect section name
also appears in MultiDev 1.0.1.6. This makes the two threshold numbers poor
evidence of successful user configurability; the embedded defaults already
match the shipped numbers. No Windows runtime test was performed.

## Windows 4.1.13.29: 9133 binding and JPEG encoding

### A different display-driver package

The libusb and WinUSB INFs bind `345f:9133&MI_03`. The UMDF INF binds that
same interface and installs `msUsbDisplayDriver.dll` with IddCx-related
configuration. Its `DriverVer` is `06/13/2025,4.2.4.7`.
These reviewed USB INFs do not list `534d:6021` or `345f:9132`.

The x64 display DLL is 663,632 bytes, SHA-256:

```text
d31ca3662811a6933eda4340309be602f1ced7bbf33192b999a890e0e3cedc7b
```

It exports `FxDriverEntryUm`; its preserved PDB path contains `V42_jpeg`.
The manager is version `4.1.13.29`, while the separately bundled
`WinUsbDisplay.exe` describes itself as the Windows-7 application and has
version `3.1.15.98`. A single installer version must not be assigned to all
of these components.

### A caller actually encodes pixels as JPEG

The x64 DLL contains libjpeg-turbo's `3.0.5 (build 20241113)` string and
TurboJPEG routines. Those strings alone would be insufficient: older Windows
applications also contain JPEG-related screen-capture code. In this DLL,
there is additionally a concrete encoding caller:

1. At `0x1800198EA`, a frame-processing routine checks its internal encoding
   field at object offset `0x614` against 3.
2. That branch calls `0x180019AB4` from `0x180019926`.
3. The callee initializes a TurboJPEG instance, sets parameters, and calls
   `0x180024E40` from `0x180019B6A`. The target identifies itself with the
   `tj3Compress8` diagnostic string at `0x180024E8C`.
4. It copies the encoded result and computes a size rounded up to four
   bytes. The caller additionally searches for the JPEG `FF D9` end marker.

The alternate branch calls `0x180019BF4`; its complete encoding format is
not classified here. The subsequent buffer is handed back to the surrounding
frame-processing path. This traces an actual JPEG encode operation in the
display DLL, rather than relying on a PDB path or bundled-library strings.

`EncodeMode` is also read by configuration code at `0x1800218D2` and stored
at offset `0x39C`. A getter at `0x180021518` feeds the display object's
configuration field at offset `0x620`. Separate selection logic chooses
internal encoding values 2/3 and has an automatic-policy branch when the
configuration is zero. The manager contains matching get/set encode-mode
diagnostics.

**Internal encoding value 3 here is not evidence for USB `TRANSMODE=3`.**
The latter selects manual-block transport in the older protocol; the former
selects a JPEG encoding branch in this DLL. The JPEG USB envelope, device
capability negotiation, and firmware requirements were not fully recovered.
There is no basis here to send JPEG bytes to an MS912x mode-3 rectangle path.

## Linux framebuffer and DRM packages

The [Linux package review](vendor-linux-packages-review-2026-10-22.md)
analyzes both packages separately, including their application interfaces,
active modes, framing, damage history, cursor handling, and source defects.
Neither package was built, loaded, or executed.

| Archive | SHA-256 |
| --- | --- |
| `MS91xx_Linux_Drm_SourceCode_V3.0.3.13.zip` | `d099925e59fddf09058b0d6ecda8a326b43e3c5f23b77a9c39b1a5a9a98a7f0e` |
| `FrameBuffer_SourceCode_V3.0.2.11.zip` | `46fb8dc31f1245fa8e64f8b0ea5cdfe4bcfdbd471be63ec52d6fc5dfa5109a62` |

The framebuffer package selects **FRAME = 0** and sends whole raw images,
a ZLP, then an A6 frame trigger. The new DRM package selects
**MANUAL_BLOCK = 3**, compares desktop pixels, retains two pending damage
rectangles, converts the selected rectangle to UYVY, and sends an `FF00`
header and `FFC0` terminator followed by a ZLP. Its per-frame A6 trigger
call is commented out. This is a significant change from the older vendor
Linux tree analyzed previously, which selected whole-frame mode 0.

The DRM ZIP extracts into `DRM_SourceCode_V3.0.3.12/`, another
filename/content version mismatch. Both packages' USB tables list
`345f:9132`, `345f:9133`, and `345f:9135`; their HALs register those IDs too.
Neither lists `534d:6021` in its USB table.

Both implementations' `ms91xx_get_chip_id()` check `FF00–FF02` for a `13 0A` suffix, then
falls back to `F000–F002` for `16 0A` and distinguishes `A7/B7` revisions.
This supports the interpretation of Android's register reads as family
detection, while demonstrating that the source's predicate is not identical
to Android's stricter first-byte check.

Both senders explicitly submit a zero-length bulk message after the payload
URB. No JPEG encoder was found in these source archives; the only
`compression` matches in the DRM source describe its BMP
file-header field. Product ID 9133 in a shared source table therefore does
not establish that the source implements the Windows DLL's JPEG path.
Source presence and attribution remain separate from hardware acceptance.

## Implications for this repository

The existing Linux rectangle protocol continues to have direct vendor
counterparts. The most useful new research targets are:

- **Completion and USB boundaries:** compare the newer macOS ZLP submission
  with its earlier wrapper and the Linux sender, preserving the distinction
  between USB transfer termination and `FF80/FFC0` stream tokens.
- **Chip-dependent frame control:** retain the Android family predicate and
  report ordering when designing a whole-frame experiment; the Java fallback
  and native consumer are different references.
- **Bypass:** study the Windows 2.0 mode-4 sender before generalizing the older
  mode-5 restrictions to every vendor build or implementing SDRAM-less output.
- **Linux damage history:** compare the new DRM package's two pending
  rectangles and idle repair with the existing sender, including how it
  retires damage before USB success.
- **Compression:** investigate the 9133-targeted UMDF path separately, including
  its capability checks and wire envelope, before proposing MS912x support.

This review provides static implementation evidence. It neither implements
these changes nor establishes performance, adapter compatibility, memory
capacity, or visible correctness. Those require separately scoped runtime
validation on the intended device and protocol mode.

## Evidence locations and reproduction

Paths in this section are relative to the repository root. Original downloads
and generated artifacts remain local under the existing `drivers/` ignore rule.
The packet bytes, version distinctions, and key addresses are included above
so the report remains useful without committing vendor binaries.

| Directory or file under `drivers/review-2026-10-22/` | Contents |
| --- | --- |
| `inventory.json` | Download sizes and SHA-256 values, including comparison inputs |
| `android/jadx/sources/`, `android/jadx/resources/` | Reconstructed Java, decoded manifest, and USB filter |
| `android/native/armeabi-v7a/` | Native disassembly, symbols, strings, and `libmsusb-relocations.txt` |
| `android/capture-service.diff`, `android/jadx.log` | Capture-service comparison and decompiler failure log |
| `macos/package/`, `macos/unpacked/`, `macos/native/` | PKG metadata, payload, symbols, and annotated Mach-O assembly |
| `windows-10160/`, `windows-20173/`, `windows-411329/` | Separate installer payloads and PE inventories, metadata, strings, and assembly |
| `linux-source/` | Extracted source archives |
| `linux-evidence/` | File inventory and 16 hashed source excerpts for the separate Linux package review |
| `evidence/index.json` | Seventeen assembly excerpts with address ranges and source/excerpt hashes |

The most relevant excerpts are `android-frame-completion.asm`,
`macos-queued-sender.asm`, `macos-new-zlp.asm`, `macos-previous-bulk-wrapper.asm`,
`windows2017-sdram-and-mode4.asm`, `windows2017-ini-reader.asm`,
`windows42-jpeg-selection.asm`, and `windows42-jpeg-encoder.asm`.

Use installed tools only. The existing APK and DMG scripts accept alternate
inputs and output directories:

```sh
bash tools/android/analyze-apk.sh \
  drivers/MSDisplay_Android_V2.0.1.3.apk drivers/review-2026-10-22/android

bash tools/macos/analyze-dmg.sh \
  drivers/MSDisplay_MacOS_V2.0.0.1_20220606.dmg drivers/review-2026-10-22/macos
```

The APK command's status 3 is expected for this observed JADX run; inspect
the log and error markers rather than interpreting it as complete success.

For each Windows package, preserve duplicate names and generate PE metadata.
For example, for the 2.0.1.7.3 installer:

```sh
innoextract --extract --collisions rename \
  --output-dir drivers/review-2026-10-22/windows-20173/unpacked \
  drivers/MSDisplay_Windows_V2.0.1.7.3.exe

python3 tools/windows/pe-evidence.py \
  drivers/review-2026-10-22/windows-20173/unpacked \
  drivers/review-2026-10-22/windows-20173/pe

llvm-objdump -d --no-show-raw-insn --x86-asm-syntax=intel \
  'drivers/review-2026-10-22/windows-20173/unpacked/app/WinUsbDisplay.exe$0' \
  > 'drivers/review-2026-10-22/windows-20173/pe/app/WinUsbDisplay.exe$0/disassembly.asm'

python3 tools/windows/annotate-asm.py \
  'drivers/review-2026-10-22/windows-20173/pe/app/WinUsbDisplay.exe$0'
```

Repeat with separate output directories for MultiDev 1.0.1.60 and Windows
4.1.13.29. For the JPEG trace, disassemble
`app/msUsbDisplayDriver/x64/msUsbDisplayDriver.dll` and annotate its matching
PE metadata directory. The existing Windows wrapper assumes some older
payload filenames; its PE helper and annotation tool can be used directly
when those filenames are absent. No script changes are needed for this review.

Addresses are preferred virtual addresses for Mach-O/PE and symbol virtual
addresses in the ARMv7 ELF, before runtime relocation. Windows descriptive
function labels in this report are analyst interpretations; the executables
do not contain their original source or matching PDB files.
