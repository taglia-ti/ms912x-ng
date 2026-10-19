# Adapter capabilities

Brief capability map for the MS912x/MS913x family, based on implemented Windows,
macOS, and Android paths, in that evidence order. Features depend on the chip,
firmware, output port, and SDRAM configuration; one adapter need not expose
everything listed. The MS9132 Linux source is hints only and adds no confirmed
capabilities here. Practical constraints are in [adapter quirks](HARDWARE_QUIRKS.md).

## Video input and pixel formats

The adapter receives uncompressed pixels over USB. Implemented encodings are:

| USB pixel format | Bytes per pixel | Representation |
|---|---:|---|
| RGB565 | 2 | Packed 16-bit RGB |
| RGB888 | 3 | Packed three-byte RGB; byte ordering follows the configured path |
| YUV422 / UYVY | 2 | `U0 Y0 V0 Y1`, sharing chroma between horizontal pixel pairs |

USB input format and SDRAM storage format are configured separately; vendor
paths include RGB888 input with YUV422 storage. YUV444 has a format constant,
but no recovered active transfer path establishes it as supported.

`VIDEO_IN` configures the complete input image's dimensions, format, and byte
selection. Dimensions have 16-bit command fields; this is a representation,
not proof of an arbitrary image-size capability. Rectangles have separate
12-bit geometry. Universal minimum input dimensions are unresolved.

Details: [input configuration and limits](docs/video-in-limits.md).

## Video output and scaling

`VIDEO_OUT` selects a fixed output timing through a vendor VIC, separately
from the USB image dimensions. Recovered timing records include:

- Nominal 60 Hz modes from 640×480 through 1920×1080, plus 1600×1200 and
  1920×1200. The latter has approximately 60 Hz CVT and reduced-blanking records.
- 50 Hz modes for 720×576, 1280×720, and 1920×1080; 30 Hz for 1920×1080.
- 75 Hz modes for 640×480, 800×600, 1024×768, 1280×768, and 1280×1024.

There are 30 distinct VICs in the recovered adapter-specific tables, including
alternative IDs for the same geometry. These are discrete timings, not every
size within a range. 1920×1200 is the largest recovered raster, not an
established silicon ceiling. Interlaced, 4K, and other enum-only modes are not
established by the inspected output paths.

**The adapter can upscale a smaller USB image to a larger output raster.**
References include 1280×720 → 1920×1080 in mode 0, 400×300 → 800×600 in a
mode-3 diagnostic, and 1152×648 → 1280×720 with normal mode-3 partial updates.
The mode-5 path also configures smaller input with an 800×600 output timing.

Scaling therefore works with partial mode-3 updates as well as full images.
Configure input/output geometry for the complete image, then address rectangles
in input coordinates. A rectangle cannot independently choose a new full-screen
scale ratio. Arbitrary ratios, downscaling, filtering, and aspect-ratio policies
remain unresolved.

The family port map distinguishes HDMI, VGA, digital, YPbPr, CVBS, and S-Video.
Available connectors and valid timings vary by variant and port.

Details: [output timing catalog](docs/video-out-modes.md),
[adapter scaling](docs/transport-mixing-and-scaling.md).

## Transfer modes

The USB endpoint supports different pixel framing according to the selected mode:

| ID | Mode | Evidence/status |
|---:|---|---|
| 0 | FRAME | Implemented raw full-image uploads with separate page-related controls |
| 1 | FIX_BLOCK_MN | Named fixed-block mode; no recovered active sender |
| 2 | FIX_BLOCK_WH | Named fixed-block mode; no recovered active sender |
| 3 | MANUAL_BLOCK | Implemented positioned rectangles, partial updates, and multi-rectangle batches |
| 4 | MEM_BYPAS_FRAME | Named SDRAM-bypass frame mode; no recovered active sender |
| 5 | MEM_BYPAS_BLOCK | Implemented SDRAM-bypass block path; recovered updates cover the complete input image |

Mode 3 can update **several differently sized and positioned rectangles in one
logical batch**, avoiding the unchanged gaps between them. Intermediate
rectangles end with `FF80`; the final rectangle ends with `FFC0`. There is no
on-wire rectangle count, and no fixed hardware count maximum was recovered.
Each rectangle costs its pixel bytes plus 16 framing bytes.

Mode-3 rectangle headers contain no explicit frame-0/1 selector. FRAME paths
use separate page-related controls. Switching between modes 0 and 3 appears
in a vendor diagnostic, with the correct full-image or rectangle payload for
each phase; switching modes alone is not an established presentation command.

Details: [multi-rectangle batches](docs/mode3-multi-rectangle-batching.md),
[mode switching](docs/transport-mixing-and-scaling.md).

## SDRAM and operation without it

Register `0x0030` reports SDRAM configuration using vendor names:

| Value | Reported capacity |
|---:|---|
| 0 | 2M |
| 1 | 4M |
| 2 | 8M |
| 3 | 16M |
| 4 | None |

SDRAM retains image contents for partial updates. Pixel storage can differ
from the USB format, so transfer byte count does not directly give SDRAM use.
Exact internal page organization and universal per-mode memory requirements
are unresolved.

**Operation without external SDRAM is supported through the implemented mode-5
bypass path.** The recovered configuration uses an 800×600 output timing,
input no larger than 800×600, and input width divisible by four. It uploads
the complete input image at `(0,0)`. These are limits of the recovered path;
sparse mode-5 updates and broader bypass dimensions remain unestablished.

Details: [SDRAM and bypass selection](docs/windows-vs-android-macos-analysis.md).

## Display controls and audio

- USB control reports configure transfer mode, input/output, start/stop, video
  enable, and power. Bulk OUT endpoint `0x04` carries image data.
- Register reads/writes support configuration and status. Implemented paths
  expose display hotplug status and monitor EDID reads.
- Additional Android helpers implement brightness, contrast, saturation, hue,
  image position, and horizontal/vertical size controls. Their applicability
  depends on the output path; they are not all exposed in normal capture menus.
- Audio-capable variants can expose a separate USB Audio Class playback
  function, independent of the video bulk stream. Available audio formats
  and HDMI routing are variant-specific; the rectangle protocol contains no
  recovered PCM audio transport.

Details: [commands and controls](docs/android-driver-analysis.md),
[EDID, hotplug, and audio](docs/windows-vs-android-macos-analysis.md).
