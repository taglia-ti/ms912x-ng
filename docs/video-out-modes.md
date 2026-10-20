# Vendor VIDEO_OUT timing inventory

Static analysis of the supplied Android APK, both Windows applications, all
six Windows indirect-display DLLs, the macOS application, and vendor Linux
source. No vendor program or USB command was executed. Android, Windows, and
macOS implementations are treated as supported paths. The supplied MS9132 Linux
source is untrusted and provides hints only; it cannot establish hardware
support or limits. A desktop descriptor and an unused enumeration are
distinguished from an adapter VIDEO_OUT selection.

## Findings

The adapter-specific lookup/full-timing tables collectively document **30
distinct VIC values in Android/Windows/macOS alone**. The MS9132 source adds no
independent confirmed VICs. The Linux fixed list now contains 28 of those
vendor VICs, plus its separate 1024×600 entry. It adds the two best-supported
missing records below; `0x04` duplicates the existing 720p60 timing and `0x84`
remains intentionally omitted because its selection evidence is incomplete.
The four additional vendor records are:

| VIC | Output | Strongest evidence |
|---|---|---|
| `0x04` | 1280×720@60 | Android full timing, identical to its `0x4F` timing |
| `0x41` | 640×480@75 | Android full timing and selection entry; matching Windows desktop descriptor |
| `0x84` | 1920×1200@59.99, CVT | Android full timing |
| `0x85` | 1920×1200@59.95, reduced blanking | Android full timing and selection entry; matching Windows desktop descriptor |

Thus **1920×1200 is explicitly described**, rather than inferred by combining
a maximum width of 1920 with a maximum height of 1200. Windows' normal USB
lookup and macOS' timing tables do not contain that geometry. Their application
policies should not be described as universal limits of the adapter family.

Windows' virtual display layer additionally contains 45 numeric descriptors,
19 more than its USB application's 26 geometry/refresh combinations. Those
extra records are useful leads, but most have no located Windows USB VIC
mapping. No adapter-specific 4K output path was established.

## What the USB command selects

```text
A6 02 VIC OUT_FORMAT WIDTH_HI WIDTH_LO HEIGHT_HI HEIGHT_LO
```

The VIC selects the output timing. There is no pixel-clock, porch, sync-width,
or refresh-rate field in this command. Its width/height fields can retain the
smaller input geometry when the adapter scales to a larger VIC-selected raster.
Changing those fields is not a substitute for selecting a different timing.
See [VIDEO_IN limits](video-in-limits.md) and
[adapter scaling](transport-mixing-and-scaling.md).

**These are vendor VICs, not a universally interchangeable CTA VIC namespace.**
Low IDs include CEA definitions, while IDs from `0x40` include vendor VESA
extensions. For example, `0x40` means 640×480@60 here, and `0x81` means
1920×1080@60. Do not decode them using a generic modern CTA table. Decimal
enum suffixes also differ from hexadecimal: `VFMT_VESA_132_*` is `0x84`.

## Complete adapter-specific inventory

`L` = dimension/refresh-to-VIC lookup; `T` = full numeric timing record.
Windows covers both 1.0.0.7 and 1.0.1.6, whose records match exactly.
An em dash means absent from these tables, rather than a hardware rejection.
Rates below use nominal lookup labels, except the two Android-only WUXGA
records. Full records retain fractional values in the extraction artifacts.

| VIC | Output size | Hz | Android | Windows | macOS | MS9132 hint only |
|---|---|---:|---|---|---|---|
| `0x02` | 720×480 | 60 | L/T | L/T | L/T | — |
| `0x04` | 1280×720 | 60 | T | — | — | — |
| `0x11` | 720×576 | 50 | L/T | L/T | L/T | — |
| `0x13` | 1280×720 | 50 | L/T | L/T | L/T | — |
| `0x1F` | 1920×1080 | 50 | L/T | L/T | L/T | — |
| `0x22` | 1920×1080 | 30 | L/T | L/T | L/T | — |
| `0x40` | 640×480 | 60 | L/T | L/T | L/T | L |
| `0x41` | 640×480 | 75 | T | — | — | — |
| `0x42` | 800×600 | 60 | L/T | L/T | L/T | L |
| `0x44` | 800×600 | 75 | L/T | L/T | L/T | — |
| `0x47` | 1024×768 | 60 | L/T | L/T | L/T | L |
| `0x49` | 1024×768 | 75 | L/T | L/T | L/T | — |
| `0x4C` | 1152×864 | 60 | L | L/T | L/T | L |
| `0x4E` | 1280×600 | 60 | L | L/T | L/T | L |
| `0x4F` | 1280×720 | 60 | L/T | L/T | L/T | L |
| `0x54` | 1280×768 | 60 | L/T | L/T | L/T | L |
| `0x56` | 1280×768 | 75 | L/T | L/T | L/T | — |
| `0x57` | 1280×800 | 60 | L | L/T | L/T | L |
| `0x5B` | 1280×960 | 60 | L | L/T | L/T | L |
| `0x60` | 1280×1024 | 60 | L/T | L/T | L/T | L |
| `0x61` | 1280×1024 | 75 | L/T | L/T | L/T | — |
| `0x64` | 1360×768 | 60 | L/T | L/T | L/T | L |
| `0x66` | 1366×768 | 60 | L/T | L/T | L/T | L |
| `0x67` | 1400×1050 | 60 | L/T | L/T | L/T | L |
| `0x6B` | 1440×900 | 60 | L/T | L/T | L/T | L |
| `0x73` | 1600×1200 | 60 | L/T | L/T | — | — |
| `0x78` | 1680×1050 | 60 | L/T | L/T | L/T | L |
| `0x81` | 1920×1080 | 60 | L/T | L/T | L/T | L |
| `0x84` | 1920×1200 | 59.99 | T | — | — | — |
| `0x85` | 1920×1200 | 59.95 | T | — | — | — |

Android has 26 lookup records and 26 full records, but their sets differ:
its full table adds `04/41/84/85` and omits `4C/4E/57/5B` relative to the lookup.
Windows has 26 records in each table with the same VIC set. macOS has 25 in
each, omitting Windows' `0x73`. The untrusted MS9132 source has 16 ordinary
lookup records, all nominally 60 Hz. These describe its code's whitelist,
not an independently trusted hardware capability list.

The Linux fixed 1024×600@60 mapping, `0x96`, was not found in these vendor
timing tables. It remains a Linux-specific mapping and does not add a confirmed
vendor capability.

## Full timing data for the four additional VICs

Raw values from Android's `g_arrTimingTable`; clocks are converted from 10 kHz
units and refresh from 0.01 Hz units. H/V offset and sync-width values are
preserved with the vendor field names, without silently treating offsets as
front porches.

| VIC | Flags | Htotal | Vtotal | Pixel clock, MHz | Refresh, Hz | Hoffset | Voffset | Hsync width | Vsync width |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `0x04` | 7 | 1650 | 750 | 74.25 | 60.00 | 260 | 25 | 40 | 5 |
| `0x41` | 1 | 840 | 500 | 31.50 | 75.00 | 184 | 19 | 64 | 3 |
| `0x84` | 5 | 2592 | 1242 | 193.12 | 59.99 | 544 | 41 | 208 | 3 |
| `0x85` | 3 | 2080 | 1235 | 154.00 | 59.95 | 112 | 32 | 32 | 6 |

Flag bit 0 marks progressive scan, bit 1 positive H sync, and bit 2 positive
V sync in the vendor polarity enum. All full records recovered here are
progressive. The 193.12 MHz entry is the largest clock in these adapter-specific
tables; it is a recorded timing, **not an established hardware clock ceiling**.
Reduced blanking lowers the output clock; it does not reduce the active USB
image's pixel count at the same geometry and format.

Android's `timing` selection array explicitly includes `0x41` and `0x85`;
`0x84` is present in the full table but not that selection array. The ordinary
HDMI/VGA/digital capture selector remains narrower. These arrays are not all
proven active UI menus. The service's firmware-VIC geometry lookup does read
the full table (`CaptureService.java`, lines 891–893).

## Windows' larger virtual-monitor table

Each of the three IDD packages, in both x86 and x64 builds, has the same **45
numeric desktop descriptors**. They contain totals, active dimensions, nominal
refresh, clock, and a label pointer, but no adapter VIC. In
`indirectdisplaydriver2/x64/IndirectDisplayDriver2.dll`, the table is at
`0x18000E590`, with 40-byte records. Function `0x180007D70` iterates 45 entries
and matches width, height, and rate before copying a descriptor. These are
numeric records used by code, rather than names inferred from strings alone.

The following combinations are additional to the Windows USB lookup:

| Desktop descriptor | Clock, MHz | Possible vendor VIC by enum name |
|---|---:|---|
| 640×480@75 | 31.50 | `0x41`, also an Android full record |
| 800×600@85 | 56.25 | `0x45` |
| 1024×768@70 | 75.00 | `0x48` |
| 1024×768@85 | 94.50 | `0x4A` |
| 1152×864@75 | 108.00 | `0x4D` |
| 1280×800@75 | 106.50 | `0x58` |
| 1280×800@85 | 122.50 | `0x59` |
| 1280×960@75 | 129.87 | `0x5D` |
| 1280×960@85 | 148.50 | `0x5E` |
| 1280×1024@85 | 157.50 | `0x62` |
| 1400×1050@75 | 156.00 | `0x68` |
| 1400×1050@85 | 179.50 | `0x69` |
| 1440×900@75 | 136.75 | `0x6C` |
| 1440×900@85 | 157.00 | `0x6D` |
| 1600×900@60 | 97.75 | `0x6F/0x70` name candidates; no USB mapping established |
| 1600×1200@75 | 202.50 | `0x75` |
| 1600×1200@85 | 229.50 | `0x76` |
| 1680×1050@75 | 187.00 | `0x7A` |
| 1920×1200@60, reduced blanking | 154.00 | `0x85`, also an Android full record |

Except the independently recovered Android records, these VIC associations
come from enum names, not a located IDD-to-USB mapping. The implemented
Windows desktop descriptors are real capabilities of the virtual display
layer; they do not by themselves establish those physical HDMI/VGA timings.
The USB application's lookup can fall back to another listed refresh for the
same geometry, so a 75/85 Hz desktop can resolve to a 60 Hz output VIC. A
geometry absent from its lookup, such as 1600×900 or 1920×1200, remains absent
from that ordinary configuration path.

Two labels are misleading in every extracted IDD table:

- Record 39 says `1920*1080*60`, but its numeric active size is **1920×1200**,
  totals **2080×1235**, clock **154000000 Hz**. These match Android's `0x85`
  size/totals/clock. The IDD rate is nominally 60; Android records 59.95.
- Record 41 says `640*576*50`, but its numeric active size is **720×576**.

The IDD's 1366×768 descriptor also uses **85.75 MHz**, versus **85.50 MHz**
in the application timing tables. Its higher desktop clocks, including
229.50 MHz, must not be promoted to adapter clock limits.

## Declared IDs and generic DRM modes

Android's enum contains 122 `VFMT_*` constants, including NULL and INVALID;
92 have neither a lookup entry nor a full timing in that Java class. Among
the named-only formats are 1600×900, 1920×1440, 2048×1536, 2560×1600,
1080i, and several 24/25/30/85/120 Hz modes. The Linux vendor header carries
the same style of enum. These names provide protocol candidates, not the
same evidence as a complete record or implemented VIDEO_OUT choice.

The untrusted MS9132 source's `drm/msdisp_drm_mode.c` contains a generic DRM/CEA table
with 3840×2160 and 4096×2160 entries. Its ordinary adapter validator instead
calls the 16-entry `g_support_mode` lookup, checks a two-page memory budget
of `4*W*H`, and DRM sets maximum dimensions to 1920×1080. The presence of the
generic table therefore does not establish 4K VIDEO_OUT support. Neither its
whitelist, memory formula, nor DRM dimension bounds establish hardware limits;
they are implementation observations retained as hints.

Its composite/S-Video advertisement helper supplies CEA VIC `0x02`; this is
outside the ordinary 16-entry lookup, and advertisement alone does not bypass
the enable-time validator. The custom-mode parameter can register a numeric
`VIC_WIDTHxHEIGHT@RATE` mapping, but it does not upload a new pixel clock or
porch definition to firmware. It also has the width-assignment bug described
in the [vendor Linux analysis](ms9132-vendor-linux-analysis.md).

## Consequences for this driver

The strongest additional fixed-mode candidates are `0x41`, `0x84`, and `0x85`.
`0x04` is an alternative 720p60 ID, with the same Android timing data as the
already supported `0x4F`; no additional geometry results from adding it.
Keep EDID filtering, chip/port policy, memory requirements, and output timing
separate from host capture or USB image limits. A 1920×1200 output record
does not establish every smaller-input scaling ratio to that output.

Do not copy an entire vendor timing record without checking differences:
Android and macOS use Hoffset/Voffset/Hsync **190/45/88** for `0x81`, while
both Windows versions use **192/41/44**. They agree on VIC, size, totals,
148.50 MHz clock, and 60 Hz. These host-side record differences are not
different values in the eight-byte VIDEO_OUT command, which sends the VIC
rather than those timing parameters. Likewise, preserve the vendor's
**74.17 MHz** value for `0x22`; do not silently replace it with 74.25 MHz.

## Current Linux implementation

The fixed Linux catalog contains 29 entries: the 26 entries already mapped
from the Windows adapter timing table, Android-backed `0x41` (640×480@75) and
`0x85` (1920×1200 reduced blanking), and Linux-specific `0x96` (1024×600@60).
`0x04` is not duplicated because it has the same timing as `0x4F`; `0x84` is
not added because the recovered Android table does not establish its selection
path and it collides with `0x85` under integer-refresh lookup.

The synthetic EDID is generated from this same fixed catalog plus valid custom
flash timings. It encodes their actual modelines as detailed timings across a
base block and CTA extension blocks, with a valid checksum on each block. The
preferred timing is 1920×1080@60 when available. The only advertised 30 Hz
fixed mode is the source-confirmed 1920×1080@30 (`0x22`); 800×600@30,
1024×768@30, and 1280×720@30 are not advertised because adapter-specific
timing evidence was not found.

## Reproduction and evidence

Run after the existing Android/Windows/macOS extraction steps:

```sh
python tools/extract-video-out-timings.py
sync
```

The [extractor](../tools/extract-video-out-timings.py) uses only Python's standard
library. It reads Java/C definitions, propagates constants through Windows'
straight-line initializers, reads macOS timing-constructor arguments, and
decodes IDD descriptors directly from PE bytes using the existing section
metadata. It never loads or executes vendor code. It checks the two Windows
applications agree, all six IDD descriptor tables agree, expected counts,
and the 30-VIC union using Android/Windows/macOS alone. It also checks the
MS9132 source adds no VIC outside that union. JSON and CSV mark its records
`hint_only`, separately from `vendor_implementation` records. Compiler padding
is left uninterpreted.

Generated local artifacts under ignored `drivers/video-out-analysis/`:

- [timings.json](../drivers/video-out-analysis/timings.json): all lookup and
  full timing values, Android selection arrays, enum-only IDs, source hashes,
  and APK/application hashes.
- [timings.csv](../drivers/video-out-analysis/timings.csv): flat timing inventory.
- [windows-desktop-timings.json](../drivers/video-out-analysis/windows-desktop-timings.json):
  all 45 descriptors for each of six DLLs, file offsets, original labels, and
  binary hashes.

Primary local sources:

- Android [Util.java](../drivers/android-analysis/jadx/sources/com/ms/ms2160/Util/Util.java),
  lines 8–17; its `com/example/msdisplay/Util.java` counterpart repeats the tables.
- Windows [1.0.0.7 assembly](../drivers/windows-analysis/pe/app/WinUsbDisplay.exe$0/annotated.asm),
  TimingTable constructor `0x44BEE0`, detailed table at object offset `0x18`,
  lookup table at `0x288`, lookup function `0x44D1E0`; the extractor locates
  the matching initializer in 1.0.1.6 independently.
- macOS [assembly](../drivers/macos-analysis/native/annotated.asm), TimingTable
  constructor `0x1000997C0`, lookup `0x1000A45E0`, nearest lookup `0x1000A4730`.
- Untrusted MS9132 hints: [ms9132.c](../drivers/ms9132-drm-linux-master/usb_hal/ms9132.c),
  `g_support_mode` and `ms9132_get_mode_vic`; adapter validation in
  [usb_hal_interface.c](../drivers/ms9132-drm-linux-master/usb_hal/usb_hal_interface.c).
