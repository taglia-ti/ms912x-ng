# Partial updates: mode 3/5 ordering, sizes, and stale image history

Static follow-up on the supplied Windows and macOS drivers, 2026-10-19.
Vendor implementation is treated as evidence of supported operation. No vendor
program was executed and no USB traffic was sent. The internal adapter-buffer
explanation below is an inference; the host-side replay and packet construction
are directly visible in the binaries.

## Main finding

**Both Windows versions and macOS replay the previous update's damage along
with the current damage.** They read those regions from the current image.
They also collect rectangles into a batch whose intermediate trailers are
`FF 80` and whose final trailer is `FF C0`.

This is the project's **Fibonacci-like workaround**, as named by the project
owner. The root-level [hardware quirks reference](../HARDWARE_QUIRKS.md)
documents the term, the current-only history rule, and related protocol quirks.

Consequently, their ordinary mode-3 behavior is different from sending a patch
at A, ending that update, then sending only an unrelated patch at B. Their
second update includes A again. This directly supports the suspected stale
adapter image history and provides a concrete vendor mitigation without
explicit FRAME-mode buffer-0/1 commands.

Mode 5 needs a separate interpretation: the normal Windows bypass path sends
the **complete scaled image**, despite using the rectangle packet format. It
does not establish a working sparse-patch algorithm for an adapter without SDRAM.

## Reproducible evidence

After the existing Windows/macOS extraction scripts have produced annotated
disassemblies, run:

```sh
python tools/extract-partial-update-evidence.py
sync
```

This extracts address-indexed excerpts into
`drivers/partial-update-analysis/` and writes binary/disassembly SHA-256 hashes
to `evidence.json`. It uses Python's standard library and existing local files.
Addresses in this report are virtual addresses in the identified binaries.

| Binary | SHA-256 |
|---|---|
| Windows 1.0.0.7, `WinUsbDisplay.exe$0` | `85af24188a2040f61f008c50620835b9ddcea0f4c1707447ec11002d55ed134e` |
| Windows 1.0.1.6, `WinUsbDisplay.exe` | `8b72b27c2f493dc433764e977ab939cd491dc6289bc068c52cbcfd9ae547a345` |
| macOS, `screen_capture_msdisplay` | `ba78cf46865a6f145bf1c3ecbd802c66c6ea2e1d21d40383cf85c3954449e9e6` |

## Windows: previous damage is explicitly carried forward

The important code is upstream of `SendRectanglesBywinLibusb`, in the update
builder. Looking only at the USB sender misses the replay mechanism.

In version 1.0.0.7:

| Address | Operation |
|---|---|
| `0x449833–0x449879` | Build the union of monitor bounds; intersect current normal damage with those bounds |
| `0x44987E–0x4498C1` | Clear a temporary persistent region at `this+0x2E0`; copy current damage into it |
| `0x4498C6–0x4498D5` | Clip the previous region at `this+0x2F0` to monitor bounds |
| `0x4498DA–0x4498E6` | Union that previous region into the outgoing current region |
| `0x4498EB–0x449928` | Clear the previous region; replace it with the saved current-only region |
| `0x449D1C` | Pass the resulting region to the display update builder at `0x445540` |
| `0x44659C` | Its steady-state display path calls `SendRectanglesBywinLibusb` at `0x446DF0` |

These operations were resolved from the region helpers, rather than inferred
from the two field offsets: `0x456900` computes intersection (max of left/top,
min of right/bottom), `0x456E40` computes union, and `0x456090` copies a region.
Version 1.0.1.6 has the same history sequence at `0x44921A–0x449302`, using
intersection `0x455F90` and union `0x4564D0`.

Ignoring cursor/video-region processing and extra refresh regions, the
reconstructed algorithm is:

```text
new_damage = current_damage intersect monitor_bounds
saved_new_damage = copy(new_damage)
outgoing_damage = new_damage union (previous_damage intersect monitor_bounds)
previous_damage = saved_new_damage
send current pixels covering outgoing_damage
```

**The saved history is current-only damage, not the union just transmitted.**
Saving the transmitted union instead would accumulate older damage and could
eventually cause unnecessarily large updates.

The union is enumerated as a region, then packed into a single aggregate USB
payload. There is no separate "send old pixels first" operation. The sender
copies each selected rectangle from the current framebuffer before conversion.
The enumeration follows the region library's stored rectangle order. A deeper
[ordering follow-up](mode3-multi-rectangle-batching.md#follow-up-ordinary-desktop-rectangles-have-y-first-x-second-region-order)
confirms that ordinary normalized desktop regions are ordered top to bottom,
then left to right within each horizontal band, in both Windows versions and
macOS. That order is established upstream: the USB loop adds no extra sort
and imposes no "previous before current" ordering. macOS' separately appended
current cursor can break the complete batch's spatial ordering.

### Initialization and periodic repair

Windows calls the full-region sender twice at `0x446456` and `0x446482`.
Both calls pass `isNotFirstFrame=0`. In the ordinary direct sender, that flag
selects **six repetitions of the same aggregate batch**, each with a 2000 ms
USB timeout (`0x447546–0x447638`). Thus this setup path requests twelve
full-image bulk transfers, subject to its connectivity/error branches. Normal
updates send the aggregate once with a 600 ms timeout. The scaled queue path
branches away before this six-repeat loop, so the twelve-transfer count must
not be generalized to mode 5.

There is also a periodic refresh mechanism:

- `0x4452E0` constructs four screen quadrants and selects a refresh interval.
- The branches choose 5 seconds for at least 1920x1080 or `3*W*H > 4 MiB`;
  otherwise 4 seconds for at least 1280x720 or `3*W*H > 2 MiB`; otherwise
  3 seconds for at least 800x600 or `3*W*H > 1 MiB`; otherwise it disables it.
- `0x449960–0x4499D4` checks connected displays, adds one quadrant when the
  elapsed time exceeds the selected interval, and advances its index modulo 4.
  The timestamp is shared by this update-builder object.

These refreshes are added to ordinary damage processing. The code establishes
periodic opportunities to repaint unchanged areas; it does not establish a
strict wall-clock repair guarantee while the whole capture/update system is idle.
Repainting both adapter pages during initialization, and repairing stale content
later, are plausible purposes of these policies. Their purpose is not named in
the binary.

## macOS: the same replay, with explicit cursor ordering

`thread_func_usb_transfer()` performs the following operations:

| Address | Operation |
|---|---|
| `0x10009066F–0x1000906CF` | Yield without sending when current damage is empty and the mouse has not changed |
| `0x100090702–0x100090728` | Copy `_region_curr`; union `_region_prev` into the working region |
| `0x1000908BE` | Enumerate that union with `Region::getRectList` |
| `0x100090C48–0x100090CDB` | Intersect each rectangle with image bounds; reject empty/invalid results |
| `0x100090E48` | Read the rectangle's pixels from current shared `_frame` |
| `0x100090EC2` / `0x100090FC8` | Convert pixels and pack a rectangle |
| `0x100091044` | Set its intermediate trailer to `FF 80` |
| `0x100091125–0x100091711` | When software cursor drawing is enabled, append the current cursor patch after the desktop rectangles |
| `0x1000917AF` | Change only the last trailer's second byte to `C0` |
| `0x1000917C9` | Set `_region_prev = _region_curr` |
| `0x1000917E8` / `0x100091800` | Include the old cursor rectangle in history; advance old cursor rectangle to current |
| `0x100091818` | Clear `_region_curr` |
| `0x100091939` | Send the complete aggregate with one bulk call |

The mouse callback marks `_mouse_updated` when a valid previous cursor needs
attention, including a transition to an empty current cursor. The worker's
history assignment explicitly adds the old cursor rectangle before advancing
it to the current one. In an alternating-page model this is particularly
revealing: the page being reused can contain the cursor from two displayed
updates ago. The desktop repair region precedes the current cursor overlay;
drawing that repair afterward could erase an overlapping cursor.

The worker holds the shared-state mutex while collecting pixels and updating
history, then unlocks before USB transmission. It seeds the display by packing
a full black image and sending the same packet twice, at `0x10008FFDE` and
`0x1000901B2`.

The bulk wrapper uses endpoint `0x04` and a 3000 ms timeout. It returns libusb's
status without verifying the reported actual byte count. History is advanced
before the transfer result is known. Therefore the vendor replay mechanism
should not be interpreted as a robust transactional recovery protocol.

## Exact batch boundaries and size nuances

The [multi-rectangle batch limits follow-up](mode3-multi-rectangle-batching.md)
rechecks both Windows executables first, then macOS and Android. It finds no
fixed rectangle-count cutoff, a 6,544,040-byte Windows aggregate allocation,
and 64 KiB segmentation inside the bundled Windows USB library. These are
separate from the logical FF80/FFC0 batch boundaries below.

For either vendor rectangle packer, a batch containing N rectangles is:

```text
FF 00 [x,y packed into 3 bytes] [w,h packed into 3 bytes]
     w*h*bpp tightly packed pixel bytes
FF 80 00 00 00 00 00 00
... further rectangles ...
FF 00 [last rectangle coordinates and dimensions]
     last_w*last_h*bpp pixel bytes
FF C0 00 00 00 00 00 00
```

For N=1, the only trailer is `FF C0`. Each rectangle contributes 16 bytes of
framing, so batch length is `sum(w*h*bpp + 16)`. UYVY and RGB565 use 2 bytes
per pixel; RGB888 uses 3. Coordinates and dimensions encode pixels, not byte
offsets, and there is no on-wire pitch field or per-row padding. Source pitch
must be handled while gathering pixels. Width and height are actual sizes,
not inclusive endpoints; the encoded fields are 12 bits wide.

The Windows packer (`0x44EFF0`) initially appends `FF 80` to every rectangle.
The sender overwrites the final trailer with `FF C0` at `0x4474FB`. macOS does
the equivalent at `0x100091044` and `0x1000917AF`. Neither normal rectangle
path adds a FRAME-mode buffer-ID control, polls a presentation acknowledgement,
or appends an explicit zero-length bulk transfer.

Windows additionally constructs a 16-byte `FF80...FFC0...` control-only batch
if packing produced no rectangles and uses a 16-byte transfer length in that
branch. However, an initially empty rectangle list returns before packing.
This is not evidence of a continuously running idle flip or a required commit
after every nonempty batch.

For horizontal alignment, the Windows direct sender translates into monitor
coordinates and applies:

```text
right = ceil_to_multiple_of_4(original_right)
left  = ceil_to_multiple_of_4(original_left)
if left > 0:
    left -= 4
```

It then restores the monitor offset and clamps/checks the rectangle against
source bounds before copying. An already aligned positive left edge is expanded
four pixels to the left. Packet dimensions and copied pixel count use the
expanded rectangle. There is no corresponding Y-alignment step in that loop.
This is a concrete vendor policy, not proof that two-pixel-aligned UYVY is
unsupported by the hardware. macOS aligns its software cursor's X position to
a four-pixel boundary and starts with a 64x64 cursor patch, clipped to bounds;
its ordinary worker does not apply the Windows four-pixel expansion.

There is no vendor policy here requiring a rectangle or batch to be a multiple
of the USB maximum packet size. USB packet/URB boundaries must not be confused
with rectangle trailers or the end of a logical batch.

The Windows library's 64 KiB subdivisions can fall inside a rectangle. It
passes the application's timeout to each chunk's completion wait, so a
600 ms argument is not necessarily a 600 ms total deadline for the batch.

## Mode 5: full-image bypass traffic

Windows selects mode 5 when the SDRAM-type register reports type 4, meaning
no SDRAM. Its setup expects desktop VIC `0x42` (800x600), limits the scaled
input to at most 800x600, and requires its width divisible by four.

Successful setup sets host scaling enabled. At `0x446E30`, the sender takes
that branch, replaces incoming damage with the complete source monitor region,
scales it, and packs a rectangle at `(0,0)` covering the whole scaled target
(`0x446D2E–0x446D6B`). It then queues the aggregate for a bulk sender thread.
The packet covers every input pixel each time; rectangle syntax does not mean
the vendor uses independent sparse patches in bypass mode.

The existing [Windows report](windows-vs-android-macos-analysis.md) describes
the queue's one-slot limit, 600 ms worker timeout, and a format inconsistency:
setup requests RGB565 while the shared libyuv path converts to UYVY. This
follow-up does not resolve that format interpretation. It also does not prove
that hardware rejects partial mode-5 rectangles. It establishes that the
normal Windows bypass implementation avoids relying on retained sparse image
contents. A mode-3 analog-output path can use the same full-image scaler.

## Why this explains non-overlapping flicker

A two-page adapter model explains all of the relevant host behavior. Assume
both pages initially contain the same image and each completed batch switches
the page being displayed. With disjoint new changes A, B, C:

| Update | Send only new damage: displayed changes | Vendor replay: displayed changes |
|---|---|---|
| A | A | A |
| B | B; A reverts on the other page | A+B, because this batch writes A+B |
| C | A+C; B reverts | A+B+C, because this batch writes B+C onto the page already containing A |

The adapter needs no explicit frame-ID byte for this model: an end-of-batch
marker could trigger the switch internally. **`FF C0` as the presentation/flip
boundary is a strong hypothesis, not a decoded firmware fact.** The vendor
code proves "last block differs from intermediate blocks" and previous-damage
replay. It does not expose the internal page-selection logic.

A full-image upload overwrites all pixels of the receiving page and therefore
avoids this stale-region problem. Merely sorting A before B cannot synchronize
two retained images. Sending A and B in one batch helps keep that update
together, but the next update still needs appropriate history if pages alternate.

The ordinary vendor rectangle paths do not depend on A6 frame 0/1 triggers or
the `F202/F203` register sequence used by FRAME-mode code. Their existence in
other transfer paths supports buffered hardware generally; the replay mechanism
is much more direct evidence for this particular partial-update problem.

## Implications for our current driver

The source already implements the core vendor strategy:

- `ms912x_transfer.c`, conversion worker: align `base_rect`, union it with
  `last_queued_rect`, gather current pixels, and queue one rectangle packet.
- After queuing, save **`base_rect`**, not the expanded transmitted union, as
  `last_queued_rect` for the next update.
- `ms912x_drv.c` allocates an ordered USB workqueue;
  `ms912x_usb_transfer.c` waits for each scatter/gather request and checks its
  complete byte count. Multiple SG entries are parts of one logical batch.

Thus, if the symptom still occurs with this source running, first distinguish
that path from an experiment that sends A and B independently without replay.
Useful remaining checks are:

1. Capture the actual endpoint-04 stream and reconstruct logical batches.
   Confirm the B update includes A with current pixels and exactly one final
   `FF C0`; exclude stale snapshots and accidental intermediate `FF C0` markers.
2. Check initialization/history resets. Windows repeats full images; macOS
   sends its initial full image twice. Our replay mechanism can also cause a
   previous full update to be sent again with the next new damage, but that
   is not the same initialization schedule.
3. Compare four-pixel horizontal expansion with our two-pixel UYVY alignment
   in a controlled experiment. It is a secondary suspect if replay/batching
   is correct; it does not explain distant rectangles as directly as history.
4. Audit history across failures, cancelled requests, and skipped batches.
   Our history advances when queued, while the failure path carries only
   `request->base_rect`, not all of `request->rect`. A failed batch can lose
   replay pixels needed to repair another adapter page. This is a potential
   recovery gap, not a confirmed cause of the reported successful-transfer case.
   After an incomplete transfer, a full-image resynchronization is safer than
   assuming which page or how many pixels the adapter accepted.
5. For a future multiple-rectangle optimization, replay the previous logical
   update's damage in the same batch as new damage, use `FF 80` between blocks,
   and end only the last block with `FF C0`. Re-read latest pixels for replay;
   replaying old packet bytes can restore obsolete content. Composite cursor
   overlays after desktop repairs. If several logical updates are merged,
   preserve all their new damage before advancing history.

These checks do not require introducing mode 0 into the normal mode-3 path.
No kernel behavior was changed by this investigation.
