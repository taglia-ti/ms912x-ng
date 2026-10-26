# VIDEO_IN/VIDEO_OUT configuration and limits

Static follow-up, 2026-10-19. Windows was examined first, including both bundled
EXE versions, followed by macOS. No vendor executable or USB command was run.
Android/Windows/macOS implementations are treated as supported operation;
the supplied MS9132 Linux source provides untrusted hints only. Missing
validation is not evidence that arbitrary values work on the adapter.

## Configuration happens at modeset or equivalent display reconfiguration

**Yes: input geometry and output timing are configured together during display
setup/reconfiguration, then retained across normal rectangle updates.** They
describe the complete image and output raster, not the dimensions of the next
damage rectangle.

Windows 1.0.0.7 has one located direct call to each VIDEO_IN and VIDEO_OUT
serializer, both in the configuration dispatcher `0x44EED0`. The call sites
are `0x44EF76` and `0x44EF8E`. Its steady-state damage path calls the rectangle
sender without reprogramming those commands. The other EXE has the equivalent
dispatcher at `0x44E7C0`, calling its serializers at `0x44E866/0x44E87E`.

The Windows setup path stops transfer, reads SDRAM type, selects mode 3 or 5,
builds parameters, powers on/waits when appropriate, writes mode/input/output,
and starts transfer. Hotplug, topology, and virtual-monitor reconfiguration can
repeat this setup. It does not normally change input size for each image.

macOS writes input/output in its monitor-configuration callback. It disables
video before programming them, then sends STARTTRANS=0 and STARTTRANS=1 and
later enables video again. Its normal USB worker does not issue VIDEO_IN or
VIDEO_OUT. Other direct callers belong to the retained diagnostics.

The Linux CRTC path in [ms912x_kms.c](../linux/ms912x_kms.c) calls
`ms912x_display_set_mode()` in [ms912x_registers.c](../linux/ms912x_registers.c).
It computes VIDEO_IN dimensions from the selected scale, programs those input
dimensions with the output VIC, and resets retained damage when geometry changes.
The framebuffer and output mode retain their requested dimensions.

## The command encoding is wider than the rectangle encoding

```text
A6 01 WH WL HH HL FORMAT BYTE_SELECT    VIDEO_IN
A6 02 VIC OUT_FORMAT WH WL HH HL        VIDEO_OUT
```

Both dimensions are unsigned 16-bit big-endian fields on the wire. The Windows
serializer at `0x44D7C0` and macOS helper at `0x1000771D0` copy the low 16 bits
of their integer arguments without validating size, positivity, scaling ratio,
or aspect ratio. Windows fixes BYTE_SELECT to zero; macOS passes an argument
that is zero in the normal configuration path.

This establishes a 16-bit representation, **not a 65535x65535 capability**.
Mode-3/5 rectangle position and size fields are separately 12 bits wide. With
ordinary rectangle encoding, a dimension of 4096 is not representable directly;
no zero-means-4096 convention was established. Device memory, format, output
timings, and host buffers impose other constraints before that field limit.

## Windows mode 3: input comes from geometry and timing policy

The parameter builder is `0x44E9E0` in 1.0.0.7 and `0x44E2D0` in 1.0.1.6.
Ordinary VGA/HDMI/digital branches use the requested display dimensions as
the input image. Composite/S-Video branches obtain their target dimensions
through the firmware VIC and enable the full-image host-scaling path. The
YPbPr branch recognizes input heights 480, 576, 720, and 1080 in this builder;
this port-specific selection is not a general limit of VIDEO_IN.

The Windows timing lookup first matches width, height, and refresh, then
allows an exact width/height match at another listed refresh. It returns an
invalid VIC when geometry is absent from the table; it has no macOS-style
nearest-larger lookup in this function. Normal display setup checks the VIC.
This constrains what the application configures even though the low-level
VIDEO_IN helper can serialize other dimensions.

The compiled table includes:

```text
720x480, 720x576, 640x480, 800x600, 1024x768, 1152x864,
1280x600, 1280x720, 1280x768, 1280x800, 1280x960, 1280x1024,
1360x768, 1366x768, 1400x1050, 1440x900, 1600x1200,
1680x1050, 1920x1080
```

Some entries have multiple refresh rates. The largest width in this table is
1920, and its largest height is 1200 at **1600x1200**, not at 1920x1200.
The latter geometry is not established by combining two independent maxima.
EDID, SDRAM, output port, and the selected virtual display driver can further
restrict which entries are offered. These are table capabilities, not a claim
that every attached board/display accepts every entry.

The [cross-driver VIDEO_OUT inventory](video-out-modes.md) finds explicit
1920x1200 records in Android (`0x84/0x85`) and numeric 1920x1200 descriptors
in the Windows virtual-monitor DLLs. The geometry remains absent from the
Windows USB application's lookup described here. That application limit is
not a universal adapter-family output limit.

### Width is rounded, while output size retains the original width

Before sending VIDEO_IN, the dispatcher computes:

```text
video_in_width = (requested_input_width + 3) & ~3
video_in_height = requested_input_height
```

VIDEO_OUT receives the original stored input width/height, alongside its VIC.
This is visible at `0x44EF62–0x44EF8E`; the second EXE repeats it at
`0x44E852–0x44E87E`. No corresponding height rounding occurs.

For example, the table's 1366x768 geometry results in VIDEO_IN width **1368**
and VIDEO_OUT dimension fields **1366x768**. Thus the width alignment is not
merely theoretical. It also cautions against assuming that every command's
dimension fields must be identical or that padding is a separate on-wire pitch.

### SDRAM thresholds select memory format rather than rejecting dimensions

USB format and device-memory format occupy the low and high FORMAT nibbles.
For RGB888 input on the ordinary non-composite paths, the builder applies this
device-memory policy:

| SDRAM type | RGB888 memory is replaced with YUV422 memory when |
|---|---|
| 0, 2M | Both W>=800 and H>=600, or `3*W*H > 1 MiB` |
| 1, 4M | Both W>=1280 and H>=720, or `3*W*H > 2 MiB` |
| 2, 8M | Both W>=1920 and H>=1080, or `3*W*H > 4 MiB` |
| 3, 16M | This branch retains the selected RGB888 memory format |

UYVY input selects YUV422 memory directly; RGB565 selects RGB565 memory.
Composite/S-Video paths also force YUV422 memory for non-RGB565 input.
The relevant memory-format branch is `0x44EDEA–0x44EEA9`, repeated at
`0x44E6DA–0x44E799` in the second EXE.

These are format-selection thresholds, not universal maximum resolutions.
The builder does not become a complete memory-fit validator after selecting
YUV422. If the adapter stores two complete pages at two bytes per pixel, their
pixel storage would need `4*W*H` bytes. The untrusted MS9132 source uses that
formula, but it does not confirm this memory model or a universal minimum.
Keep this hypothetical accounting separate from USB payload bytes and from
the directly recovered Windows threshold policy itself.

The automatic picture-quality branch also uses a 1280x720 pixel-count threshold
when selecting the USB format. Crossing it can change format rather than
reject VIDEO_IN. No general input/output scale-ratio limit was found here.

## Windows mode 5: explicit small-input restrictions

Both EXEs check the bypass branch before the general port/format logic:

- Output desktop/timing lookup must produce VIC `0x42`, 800x600.
- Target input width must be <=800 and height <=600.
- Target input width must be divisible by four.
- No matching even-height, aspect-ratio, or maximum-upscale-ratio test appears.
- No explicit positive lower-bound check appears in this branch. Do not treat
  acceptance of zero or negative integers by the checks as useful hardware support.

For positive inputs, **400x300, 200x150, and 800x600 all satisfy these host
checks**. That is useful evidence about what the software permits, but does
not independently establish a working 4x scaler ratio or seamless transitions
between them. The Windows sender uploads each complete scaled input image.

Failure displays a warning and returns without a clear mode-validation error;
the caller can continue with existing parameter state. The width/height in
the supplied `[mem_by_pass_scale]` INI section were not proven to be read by
this path. Also retain the unresolved RGB565-configuration/UYVY-converter
discrepancy described in the [Windows report](windows-vs-android-macos-analysis.md).

## macOS: capture limits, input limits, and scaling policy differ

The normal monitor callback first derives capture dimensions after Retina
scaling. At `0x1000A2982–0x1000A29DF`, it accepts a capture canvas no wider
than **1920** and no taller than **1200**, with scale factors corresponding
to 100% or 200%. Otherwise it substitutes **1920x1080** and marks an
unsupported-capture flag. This is a host capture policy, not a VIDEO_IN
serializer check or a guarantee that all 1920x1200 inputs are supported.

For non-composite output, height above 1080 selects a host zoom factor of
approximately 4/3 before timing selection. Exact timing lookup is followed
by nearest-larger lookup when needed. Then:

- Capture width <=1280: VIDEO_IN retains actual capture dimensions, with
  zero placement offsets, while the selected VIC can describe a larger output.
- Capture width >1280: VIDEO_IN uses the selected timing's canvas dimensions,
  and the host computes offsets to center the active captured image in it.

The **1280 boundary selects a strategy; it is not the maximum VIDEO_IN width**.
The normal timing table contains 1920x1080. The explicit scale-up diagnostic
also configures 400x300 VIDEO_IN, showing that a native output timing such as
640x480 is not a minimum input-size requirement.

The normal setup does not apply Windows' four-pixel VIDEO_IN rounding or a
general minimum-size/aspect-ratio/upscale-ratio test. Paired UYVY pixels still
matter to conversion and rectangle packing. Absence of a caller check does
not establish support for arbitrary odd or tiny dimensions.

Format policy in this path is:

| Pixel path | VIDEO_IN FORMAT |
|---|---|
| RGB565 | `0x00` |
| UYVY | `0x22` |
| RGB888 with `3*res_w*res_h <= 4 MiB` | `0x11` |
| RGB888 above that threshold | `0x21`, RGB888 USB input with YUV422 memory |

The RGB888 threshold uses active `res_w/res_h`, not an independently measured
SDRAM capacity. As with Windows, this is a format policy, not proof of a
universal hardware size ceiling. Host staging buffers and the selected timing
also constrain this application. See the [scaling trace](transport-mixing-and-scaling.md)
for the normal partial-update example and precise small-input branch.

## Consequences for our modeset design

Keep output mode/VIC separate from USB input dimensions and host framebuffer
dimensions. Program VIDEO_IN/VIDEO_OUT during modeset or an explicit stream
reconfiguration, not for each damage rectangle. A future size change should
coordinate outstanding transfers and reset damage history before establishing
the complete new input image. The recovered code does not establish a seamless
per-frame input-size-change protocol.

The strongest established constraints are the field encodings, Windows width
alignment, mode-5 caller restrictions, available timing paths, and memory/format
accounting. **A universal minimum input size, maximum upscale ratio, and complete
chip/firmware-specific VIDEO_IN maximum remain unresolved.** In particular,
there is no recovered rule restricting input to 720p/1080p or to one fixed
input/output scaling ratio.

The Linux driver reads SDRAM type from register `0x0030` before bootstrap
configuration and reports the decoded capacity in the kernel log. A confirmed
type 4 (no external SDRAM) produces a warning; this driver keeps the USB/DRM
device registered but skips mode-3 bootstrap and rejects display modesets.
Unknown or unreadable SDRAM state is not treated as type 4 and prevents setup
until a later detection succeeds. The Windows mode-5 full-image bypass path is
not implemented by this Linux driver.

The fixed timing catalog is also the source for synthetic EDID. Each listed
mode has an exact modeline; `0x41` and `0x85` are included from the strongest
available additional timing evidence. The Linux-specific 1024×600 entry retains
its existing EDID timing. No 30 Hz timing is invented for 800×600, 1024×768, or
1280×720; 1920×1080@30 is the only confirmed common 30 Hz mode.

## Local evidence

`python tools/extract-partial-update-evidence.py` now includes input/output
serializers, both Windows parameter builders and dispatchers, the Windows
compiled timing records, and macOS capture geometry policy, alongside the
earlier scaler and partial-update evidence. Run `sync` after generation.
`drivers/partial-update-analysis/evidence.json` records binary/disassembly
hashes and address-indexed excerpts.
