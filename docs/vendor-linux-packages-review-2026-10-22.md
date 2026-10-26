# Vendor Linux packages: framebuffer and DRM review

Static source review dated 2026-10-22 of the two newly supplied Linux
archives. This extends the
[cross-platform driver review](vendor-drivers-review-2026-10-22.md) and
compares the new DRM source with the
[older Linux source analysis](ms9132-vendor-linux-analysis.md).

The [version comparison](vendor-driver-version-comparison.md) distinguishes
changes we already implement from useful transport and recovery experiments.

The packages were extracted and read without modifying their source, building
or loading modules, running their demo, or accessing a USB adapter. Source
claims describe these implementations; they do not establish correct hardware
behavior, accepted limits, or compatibility. The earlier report's treatment
of vendor-attributed source as untrusted implementation evidence still applies.

## The packages implement different transfer paths

| Aspect | Framebuffer package | DRM package |
| --- | --- | --- |
| Download | `FrameBuffer_SourceCode_V3.0.2.11.zip` | `MS91xx_Linux_Drm_SourceCode_V3.0.3.13.zip` |
| Extracted directory | `FrameBuffer_SourceCode_V3.0.2.11/` | `DRM_SourceCode_V3.0.3.12/` |
| Kernel modules | `usbdisp_fb.ko` | `usbdisp_drm.ko` and `usbdisp_usb.ko` |
| Application interface | fbdev, mapped framebuffer, pan-display ioctl | DRM/KMS primary plane, GEM/PRIME, legacy cursor callbacks |
| Ordinary host pixels | Default 24-bit RGB-family data; additional format branches | XRGB8888 primary plane only |
| Selected USB transfer mode | Whole-frame mode 0 | Manual-block mode 3 |
| USB pixels | Complete raw image; format follows selected fb format | CPU-converted UYVY rectangle |
| Stream framing | No rectangle header or `FFC0` token | Eight-byte `FF00` rectangle header and eight-byte `FFC0` terminator |
| End of submission | Zero-length bulk transfer, then A6 frame trigger | Zero-length bulk transfer; A6 frame-trigger call is commented out |
| Damage policy | Whole image supplied on pan | One bounding rectangle, accumulated in two pending slots |
| Idle repair | Nominal 2-second whole-frame resend | Full rectangles marked after 2500 ms without a completed sender attempt |

Both USB tables match `345f:9132`, `345f:9133`, and `345f:9135`, restricted
to interface class/subclass/protocol `FF/00/00`. Neither table lists
`534d:6021`. The same three IDs are also registered in the HAL, and the DRM
bridge checks them. HAL recognition of MS912x chip signatures does not by
itself make another USB product ID bind.

The supplied DRM package is therefore a materially different implementation
from `drivers/ms9132-drm-linux-master/`, reviewed earlier. That older tree
sets transfer mode 0 and sends full frames with A6 triggers; the new tree
selects mode 3 and implements rectangle packing and CPU damage comparison.
The older report remains about its original tree.

## Identification and evidence

| Archive | Bytes | SHA-256 |
| --- | ---: | --- |
| `FrameBuffer_SourceCode_V3.0.2.11.zip` | 50,724 | `46fb8dc31f1245fa8e64f8b0ea5cdfe4bcfdbd471be63ec52d6fc5dfa5109a62` |
| `MS91xx_Linux_Drm_SourceCode_V3.0.3.13.zip` | 79,033 | `d099925e59fddf09058b0d6ecda8a326b43e3c5f23b77a9c39b1a5a9a98a7f0e` |

Every extracted file was compared byte-for-byte with its ZIP member. There
are 27 files in the framebuffer archive and 44 in the DRM archive. The DRM
archive's `3.0.3.13` label differs from its directory name `3.0.3.12`.
The fbdev module declares `3.0.2.11`; the DRM USB module separately declares
`1.1.0` and its platform module `1.0.1`.

Below, `F/` means the extracted framebuffer directory and `D/` the extracted
DRM directory, both under `drivers/review-2026-10-22/linux-source/`.
Line anchors refer to the unchanged extracted files.

## Framebuffer package

### Public interface and application-owned images

`F/fbdev/usbdisp_fbdev.c:811` registers `mmap`, open/release, mode validation,
mode setting, a custom ioctl, and `fb_pan_display`. There are no driver
`fb_read` or `fb_write` callbacks; the supplied instructions describe those
interfaces as unsupported.

`msfb_var_color_format()` defaults to 24 bits per pixel. The format parser
also recognizes RGB565, RGB888/BGR888, and XRGB8888/XBGR8888; the HAL has
additional YUV conversion branches. Those branches should not be treated as
equally validated application paths.

The intended application sequence is:

1. Query the framebuffer's variable/fixed information.
2. Allocate a virtual height for multiple images and map the framebuffer.
3. Write one image into a mapped page.
4. Set `yoffset` and issue `FBIOPAN_DISPLAY`.

`msfb_pan_display()` at line 608 computes the selected page address from
`yoffset * line_length` and submits a full visible image to the HAL. Merely
writing mapped pixels does not call this driver callback. The source demo
illustrates this sequence, but its drawing loop hardcodes three bytes per
pixel despite computing a bytes-per-pixel variable. It is a 24-bit example,
not a general format conformance test.

The instructions describe double buffering. `msfb_ops_check_var()` prints an
error if `yres_virtual < 2 * yres`, but does not reject that condition at that
point. The stated double-buffer constraint is therefore not fully enforced
by this check.

`FBIOGET_HDMI_HPD_MSFB`, defined in `F/fbdev/msfb.h`, is handled at
`usbdisp_fbdev.c:782`. It reads HAL connection state and copies a connected/
disconnected value to userspace. That gives applications an explicit status
query; it is distinct from the DRM connector's automatic polling path. The
instructions contain both an older no-hotplug statement and a later note
introducing this ioctl.

### Mode 0 and the exact sender sequence

`F/usb_hal/usb_hal_interface.c:819` initializes memory format YUV422 and
`trans_mode = USH_HAL_TRANS_MODE_FRAME`, whose value is zero.
`usb_hal_enable()` chooses the input-format nibble from the framebuffer's
format. With the default 24-bit input, VIDEO_IN uses `0x21`: YUV422 memory
and RGB888-family USB pixels.

The RGB copy routine at line 348 copies packed RGB565/RGB888 directly,
swaps BGR888, or drops the fourth byte from supported 32-bit pixels while
respecting source pitch. The payload is an entire raw image.

`F/usb_hal/usb_hal_thread.c:64` holds `usb_buf.mutex` across:

```text
submit the whole image URB and wait, timeout 2000 ms
submit a zero-length bulk message, timeout 2000 ms
toggle frame_index
send A6 00 frame_index 64 00 00 00 00
```

The frame-trigger helper receives decimal delay 100, serialized as hex `64`.
The bulk endpoint helper returns endpoint 4. This sequence is close to the
older vendor DRM frame path. It differs from Android 2.0.1.3's native 913x
branch, which uses delay `01` and toggles its index after sending the trigger.
The differing index conventions do not establish which buffer firmware presents.

The event loop waits in 20 ms intervals. When enabled and after its first
image, its timeout counter requests a resend after 2000/20 intervals without
an update event. That is an implementation timer, not a measured display rate.

## DRM package

### Public XRGB8888, internal UYVY

`D/drm/msdisp_drm_modeset.c:467` advertises only `DRM_FORMAT_XRGB8888` for
the primary plane. `msdisp_drm_handle_damage()` passes the full framebuffer
pointer, pitch, and length into the HAL. It does not pass a list of DRM
damage rectangles into the USB packetizer.

The HAL copies those 32-bit pixels into `desktop_buf`. Its initialization
selects `USB_HAL_TRANS_MODE_MANUAL_BLOCK = 3` and YUV422 device memory.
`usb_hal_enable()` explicitly forces YUV422 USB input too, producing
VIDEO_IN format byte `0x22`.

`D/usb_hal/usb_hal_thread.c:156` converts a selected desktop region to
UYVY, two pixels at a time. It computes limited-range Y values from
`263*R + 516*G + 97*B`, averages both pixels' chroma, and writes `U Y0 V Y1`.
The converted pixels begin at USB buffer offset 8. No native UYVY primary
plane is exposed to applications; conversion is done by the kernel sender.

### Damage comparison and two pending rectangles

The new sender compares `desktop_buf` with `image_buf` and searches the
top, bottom, left, and right changed edges. It forms one bounding rectangle,
so distant small changes can cause unchanged pixels between them to be sent.
This is not the desktop Windows/macOS multi-rectangle region list.

`usb_hal_combine_rects()` at line 267 expands both `rect[0]` and `rect[1]`
with every newly detected change. It aligns horizontal boundaries to four
pixels and vertical boundaries to two pixels. The sender selects one slot
using `frame_index`, clears that slot after packing it, and toggles the index
after the USB attempt. The other slot retains pending coverage.

This is explicit host-side damage retention across alternating indices.
It supports the need to inspect stale-region handling, without proving an
adapter's internal page architecture. The reference image is updated from
current desktop pixels during comparison; the sender does not replay old
pixel contents from a captured prior frame.

### One mode-3 rectangle per submission

`usb_hal_package_block()` at line 213 writes:

```text
FF 00
(x >> 4) ((x & 0x0F) << 4 | (y >> 8)) (y & 0xFF)
(w >> 4) ((w & 0x0F) << 4 | (h >> 8)) (h & 0xFF)
UYVY pixels: w * h * 2 bytes
FF C0 00 00 00 00 00 00
```

Position and size fields are masked to 12 bits. The actual packet size is
`w * h * 2 + 16`. This helper emits a final `FFC0` token; it does not build
an intermediate `FF80` chain.

`usb_hal_dev_send_frame()` at line 382 performs the cursor composition,
damage comparison, conversion, and packet packing, then submits the payload
URB and a separate zero-length bulk message. Its A6 frame-trigger call at
line 441 is commented out. The active path uses the rectangle terminator
instead of an explicit per-frame A6 trigger.

The framebuffer and DRM modes therefore cannot exchange payloads unchanged.
Their shared endpoint and control helpers do not imply shared pixel framing.

### Update coalescing, cursor composition, and idle repair

The producer copies an image into shared desktop storage and queues an
UPDATE notification. The sender drains queued events, sets one `bupdate`
flag, and processes the latest shared image. FIFO entries are notifications,
not separately owned image snapshots.

The CRTC cursor callbacks accept 64x64 images. The HAL saves background under
the old cursor, restores it, and composites the current cursor into desktop
pixels before comparing damage. Cursor movement sets `mouse_moving`, which
can wake the update decision without a new primary-plane image.

When there is no queued update or cursor movement, the scheduler checks
`update_time`; after 2500 ms it marks both slots as full rectangles and
requests a send. After the first successful sender return, the code enables
video/screen output and also marks both slots full. These are explicit
initialization/repair policies, not proof of their visual necessity.

`D/drm/msdisp_drm_drv.c` generates DRM vblank/page-flip notifications with a
20 ms software timer. Those notifications are not synchronized with payload
completion or observed monitor refresh. They cannot establish 50 displayed
frames per second.

## Shared controls, modes, and identification

Both packages implement eight-byte HID feature reports with value `0x0300`,
index zero, SET_REPORT `21/09`, and GET_REPORT `A1/01`. Their XDATA reader
copies up to four bytes from response offsets 3–6, and their write helper
uses `B6`. The A6 helpers include transfer-mode selection, input/output
configuration, STARTTRANS, VIDEOON, power, and frame trigger.

Both enable sequences stop transmission, disable video/screen output,
power on, configure mode/input/output, start transmission, and defer visible
video enable until after the sender's first successful return. The selected
mode is what differs: 0 for framebuffer, 3 for DRM.

Both sources check the `FF00–FF02` signature suffix `13 0A`, then fall back
to `F000–F002` and suffix `16 0A`, distinguishing `A7/B7` revisions. The
ordinary mode checks combine a timing lookup with a device-memory estimate.
EDID helpers can return synthetic tables for some output/memory cases and
read `C000 + block * 128` in others; DRM additionally modifies detailed
timings/checksums. An EDID returned by these implementations is not always
an untouched monitor EDID.

The framebuffer instructions state 640x480 through 1920x1080. Its source
checks pixel area against those products, plus HAL timing validation. DRM
sets mode-config bounds up to 1920x1600, while its ordinary timing whitelist,
SDRAM checks, and fixed host buffers add other constraints. None of these
numbers alone establishes an adapter's maximum supported mode.

No JPEG encoder or ALSA/audio transport was found in either package. The
`compression` field in the DRM source belongs to its BMP dump-file header.
The shared 9133 USB ID is not evidence that these sources implement the
JPEG encoding path found in the newer Windows UMDF driver.

## Concrete implementation concerns

These are source-level findings and inferred consequences, not reproduced
hardware failures. The packages were left unchanged.

| Finding | Source evidence | Consequence or limit |
| --- | --- | --- |
| SDRAM-none value is treated as capacity by the ordinary size macro | Both `hal_adaptor.h` files define `(2 MiB << type)`; both mode checkers use it; `SDRAM_NONE` is 4 | Value 4 becomes 32 MiB in that calculation. The sources' mode 0/3 setup is not a verified SDRAM-less implementation |
| DRM retires damage before USB success | `D/usb_hal/usb_hal_thread.c:406` clears the selected slot; the index toggles even if the URB failed | Failed coverage is not explicitly restored to that slot; the other slot and later repairs must not be mistaken for reliable retry acknowledgment |
| Cursor composition and image publication use different locking scopes | DRM cursor drawing writes `desktop_buf` before the sender takes `usb_buf.mutex`; producers copy that buffer under `usb_buf.mutex` | The source permits overlapping desktop writes; cursor/desktop publication needs a concurrency audit |
| Thread destruction uses a fixed delay rather than joining | Both `usb_hal_destroy()` implementations set the run flag, sleep 300 ms, then release storage; individual bulk waits allow 2000 ms | The delay does not prove the sender exited before its buffers/state are freed |
| ZLP and trigger failures do not determine frame success | Both senders preserve the payload URB result; framebuffer does not check the frame-trigger return | A successful sender return does not establish complete protocol success or a visible image |
| DRM RGB event length remains zero | `cpy_len` starts at zero and is not assigned in the RGB copy branch; it is copied into metadata/event length | The active sender recomputes rectangle length, so this is inconsistent metadata, not evidence that its USB payload is empty |

The fbdev double-buffer check and demo's fixed 24-bit drawing are additional
application-interface limitations described above. No build-compatibility
claim is made for these versions; the previous report's build result applies
only to its older source tree.

## Reproduction and source anchors

Extraction requires Python's standard library and does not execute package
contents:

```sh
python3 -m zipfile -e drivers/FrameBuffer_SourceCode_V3.0.2.11.zip \
  drivers/review-2026-10-22/linux-source

python3 -m zipfile -e drivers/MS91xx_Linux_Drm_SourceCode_V3.0.3.13.zip \
  drivers/review-2026-10-22/linux-source
```

The vendor build instructions describe loading modules and, for DRM,
restarting X11/RandR integration. Those instructions were read, not run.
They also tell users to unload the alternative package before using the
other one. Both bind the same USB interface; they are alternative host
implementations, not modules that must all be loaded together.

Generated evidence is in `drivers/review-2026-10-22/linux-evidence/`:

- `source-inventory.json` records every extracted file's size and SHA-256.
- `index.json` indexes 16 line-numbered excerpts with source/excerpt hashes.
- `fb-public-api.txt`, `fb-mode0-and-input.txt`, and
  `fb-send-and-periodic-refresh.txt` trace the framebuffer path.
- `drm-mode3-and-copy.txt`, `drm-uyvy-and-packet.txt`,
  `drm-damage-history.txt`, and `drm-sender-and-refresh.txt` trace the DRM path.
- `drm-cursor-locks.txt`, `drm-capacity-check.txt`, `drm-capacity-macro.txt`,
  `drm-teardown.txt`, and `fb-teardown.txt` preserve the main audit concerns.

Sorted `relative_path + NUL + file_sha256 + newline` records produce these
source-tree SHA-256 values:

```text
8464233651a1f0bfa3be2ea85275b688771e439498e75d600895c277cdb17a40  FrameBuffer_SourceCode_V3.0.2.11
8a459d717b5928cf4295b9579ef050bb292e5ad339f74bd3e44521b9039a5b25  DRM_SourceCode_V3.0.3.12
```

The useful independent corroboration is specific: the DRM packet layout and
UYVY transport match the desktop binary evidence, and both Linux senders
include explicit ZLP submissions. Chip-specific register effects, retained
device pages, fault recovery, and physical display results still require
validation on the intended adapter.
