# Mode-3 batches: multiple rectangles, counts, and byte limits

Static follow-up, 2026-10-19. Evidence priority is **Windows > macOS > Android**.
The supplied MS9132 Linux source is not used as capability evidence. No vendor
code was executed and no USB transfer was sent.

## Result

**Yes: the ordinary Windows mode-3 path sends multiple independently positioned
rectangles in one logical update and one application-level bulk-write call.**
macOS independently implements the same framing. There is no located fixed
rectangle-count limit in either sender and no on-wire rectangle-count field.
This does not establish an unlimited hardware count: no numeric adapter maximum
was recovered. The concrete Windows constraint is its allocated byte budget,
not a small fixed number such as 2, 4, 16, or 256 rectangles.

Keep three levels distinct:

| Level | What determines its end |
|---|---|
| Rectangle | Its dimensions/pixel payload and eight-byte trailer |
| Logical update/batch | Final `FF C0` trailer; previous rectangles end in `FF 80` |
| USB request/packet | Host-library/controller segmentation; can end inside a rectangle |

The bundled Windows `libusb0.dll` splits its single application bulk call into
**at most 65536-byte submissions**. Those are pieces of the same batch, not
separate updates. Multiple rectangles are processed in stream order, rather
than transmitted in parallel.

## Windows: direct evidence in both executables

| Operation | Windows 1.0.0.7 | Windows 1.0.1.6 |
|---|---|---|
| Normal rectangle sender entry | `0x446DF0` | `0x446700` |
| Append one rectangle | `0x4474D6` calls `0x44EFF0` | `0x446DE6` calls `0x44E8E0` |
| Advance rectangle iterator | `0x4474DE` | `0x446DEE` |
| Continue until end of list | `0x4474E4–0x4474E7` | `0x446DF4–0x446DF7` |
| Change last trailer to C0 | `0x4474FB` | `0x446E0B` |
| Submit complete aggregate | `0x447596` | `0x446EA6` |

The sender obtains the region's rectangle collection, resets aggregate offset,
length, and count, then visits every entry. Each accepted rectangle contributes
its own position, size, and current-image pixels. The packer appends an FF80
trailer and increments the host-side count. After the iterator reaches the
end, the sender changes only the last trailer to FFC0 and sends the aggregate.
It does not flush after every rectangle or after a fixed number of rectangles.

The host-side counter is a 32-bit field at packet-builder offset `+0x0C`:
`0x44F127` / `0x44EA19` increments it. The finalizer tests whether it is nonzero;
the counter is **not serialized into the rectangle header or trailer**. It
cannot be interpreted as an eight-bit/256-rectangle protocol field.

The usual steady-state sender submits the aggregate once. Its first-image
flag instead requests six repetitions of the same aggregate; six is a repeat
count, not a rectangle limit. Likewise, the scaled worker's one-slot queue is
a pending-packet policy, not the ordinary mode-3 sender's rectangle capacity.
See [partial-update ordering](partial-update-ordering.md).

### Concrete Windows aggregate allocation

Both executables set the allocation parameter to **`0x63DAA8 = 6,544,040 bytes`**:

- 1.0.0.7: `0x444068`, passed to the USB-worker constructor at `0x444096`.
  That constructor allocates the buffer at `0x44DCE4–0x44DCF0` and places its
  packet-builder pointer at worker offset `+0x14`.
- 1.0.1.6: equivalent setup at `0x443A48/0x443A76`, allocation at
  `0x44D5D4–0x44D5E0`.

This is the application allocation for this recovered path, not a firmware
maximum or a universal USB maximum. No capacity check, automatic growth, or
split-on-full operation appears in the rectangle appender. Consequently the
allocation is a memory-safety budget to respect, not proof that the vendor
will gracefully reject or split an oversized update.

Setup also stores `0x4E20 = 20000` nearby. No enforcement of that value was
found in the sender/packer, so **20,000 is not an established rectangle-count
limit**. It must not be reported as the adapter's maximum.

## Wire encoding and how to budget N rectangles

```text
FF 00 [x/y: 3 bytes] [width/height: 3 bytes]
      rectangle 1 pixels
FF 80 00 00 00 00 00 00
FF 00 [another position and size]
      rectangle 2 pixels
FF 80 00 00 00 00 00 00
...
FF 00 [last position and size]
      last rectangle pixels
FF C0 00 00 00 00 00 00
```

Each rectangle has an eight-byte header and eight-byte trailer. For one
rectangle, its only trailer is FFC0. There is no extra batch header, count,
per-rectangle frame ID, pitch field, row padding, or required 512-byte padding
in these packers. Normal rectangle batches have no explicit final ZLP.

For the configured pixel format:

```text
P_i = width_i * height_i * bytes_per_pixel
batch_bytes = sum(P_i + 16)

For equal-sized encoded rectangles and a chosen buffer budget B:
byte_budget_count = floor(B / (width * height * bytes_per_pixel + 16))
```

UYVY and RGB565 use two bytes per pixel; RGB888 uses three. The formula counts
the **encoded dimensions after alignment/expansion**, and all rectangles,
including replayed damage and cursor patches. All pixels use the currently
configured stream format; the header contains no per-rectangle format switch.

Examples for already aligned 32×32 UYVY rectangles:

| Rectangle count | Aggregate bytes |
|---:|---:|
| 2 | 4,128 |
| 100 | 206,400 |
| 1,000 | 2,064,000 |

These are byte-budget calculations, not observed hardware count tests. Each
position must also fit the configured image; the formula alone does not prove
that a particular number of disjoint rectangles fits a particular raster.

### Geometry and ordering constraints

- X, Y, width, and height have **12-bit fields**. Positive sizes can encode
  1–4095 directly; no zero-means-4096 convention was established. Image bounds
  and format/alignment rules can be narrower than that representation.
- Windows aligns horizontal bounds to four-pixel units and can expand the
  left edge by another four pixels. The expanded pixels count toward the
  batch budget. Its ordinary loop has no corresponding vertical alignment.
  UYVY additionally requires complete two-pixel chroma groups.
- Rectangles may have different sizes and positions. Region union/clipping
  can merge or split application damage, so application damage count need not
  equal the number of encoded rectangles.
- Alignment and cursor drawing can produce overlap. The code preserves
  packing order; macOS appends the current cursor after desktop repairs.
  Do not reorder overlapping rectangles indiscriminately.

The final trailer establishes the vendor's batch boundary; these binaries
do not independently prove atomic scanout or vblank-synchronized presentation.

### Follow-up: ordinary desktop rectangles have Y-first, X-second region order

**Yes: ordinary desktop damage is enumerated top to bottom, and left to right
within each horizontal band.** This order comes from region construction,
not an extra sort in the USB sender. It is not a rule that X and Y must both
increase throughout a batch; X can reset toward the left on the next band.

Windows provides the primary evidence:

| Stage | 1.0.0.7 | 1.0.1.6 | Recovered behavior |
|---|---|---|---|
| Region operation | `0x456280` | `0x455910` | Select lower top Y, group boxes sharing that top, process successive horizontal bands |
| Union of spans in one band | `0x456B20` | `0x4561B0` | Select lower left X from the two inputs, merge touching/overlapping spans, append in X order |
| Region-to-vector enumeration | `0x455D00` | `0x455390` | Read stored eight-byte boxes in increasing array order; append Rect objects without reversing or sorting |
| Normal USB sender | `0x446DF0` | `0x446700` | Walk the resulting vector from begin to end and pack in that order |

In 1.0.0.7, `0x456358–0x456367` selects the smaller top-Y value;
`0x456390–0x45639D` groups equal top-Y values. The band's union starts by
comparing left-X values at `0x456B41`, and repeats that comparison at
`0x456B79–0x456B85`, merging spans before appending the output. Enumeration
at `0x455DA0–0x455DE2` reads each box and advances by eight bytes. The region
collector at `0x44A2DD–0x44A316` also walks that vector forward; the inspected
collector callback at `0x460260` simply appends its rectangle to the output
vector. The later USB loop preserves that order.

macOS independently corroborates the ordering. `_QuickSortRects` at
`0x10000BDB0` compares the signed top-Y fields first (`+2` and `+0xA` for
two adjacent eight-byte boxes), then the signed left-X fields (`+0` and
`+8`) when Y is equal. It swaps out-of-order boxes. Region validation calls
it at `0x10000B585`. The normal add/union operations maintain banded regions,
and `Region::getRectList` at `0x1000061D0` enumerates the stored boxes forward
and uses `push_back`, preserving that order for the normal worker. This does
not imply a quicksort call on every USB update: already normalized regions
retain their ordering through the region operations.

For example, a normalized desktop region can be emitted as:

```text
(x=100, y=20, ...) -> (x=600, y=20, ...)
                 -> (x=40,  y=80, ...) -> (x=500, y=80, ...)
```

These are region boxes, not necessarily the original damage rectangles.
Union/intersection can split a tall damage rectangle at band boundaries and
merge neighboring spans. In the normalized region, boxes in one band share
top and bottom Y; neighboring bands can also coalesce when their spans match.

There are important limits to the ordering conclusion:

- The Windows USB sender does not inspect adjacent rectangles for sortedness
  or sort an arbitrary caller-supplied list. The result describes the ordinary
  region-generated desktop path, not an enforced property of every possible
  invocation or a demonstrated hardware requirement.
- Per-monitor clipping/translation and Windows' horizontal expansion occur
  after region construction. Expansion can introduce overlap or equal X
  values, so packets no longer need to be disjoint normalized region boxes.
  Additional separately collected groups are not proven globally sorted by
  the sender, which simply preserves the vector it receives.
- macOS explicitly appends the current software cursor **after desktop
  repairs**, even if the cursor is near the top left. The complete batch can
  therefore jump backward in Y/X at the end. Preserve that overlay ordering.
- This is spatial ordering after the new/previous damage union, not a temporal
  ordering of all previous-damage rectangles before all new-damage rectangles.
  All pixels are gathered from the current image.
- Android's active mode-0 sender supplies no additional mode-3 ordering evidence.

The Linux implementation preserves input rectangle order and does not merge
overlapping or nearby desktop damage. It replays the previous update's
current-only damage as separate rectangles in the same batch. When a cursor
rectangle is present, it is kept last so the current cursor overlays any desktop
repair it covers. History is not the outgoing union: only the new current
damage becomes history for the next update.

## Windows USB segmentation and timeouts

The app wrapper at `0x44D470` calls the bundled `usb_bulk_write` on endpoint 4.
The DLL exports it at RVA `0x4ECF`; its synchronous helper is `0x404E1F`.
The loop at `0x404E47–0x404E89`:

```text
chunk_size = min(remaining_bytes, 65536)
submit_async(buffer, chunk_size)
reap_async(timeout)
advance by actual_bytes
repeat while data remains and the chunk completed in full
```

It knows byte lengths, not rectangle boundaries. For example, a full
1920×1080 UYVY rectangle is 4,147,216 bytes with framing and takes 64 such
submissions at maximum chunk size. The allocation's full budget would take
100 submissions. A rectangle may span many submissions, while one submission
may contain many small rectangles.

The app supplies 600 ms for steady-state calls and 2000 ms for its initial
repeat path. **The DLL passes this timeout to each chunk's reap operation**;
it is not necessarily a total deadline for the complete logical batch.
A short chunk stops the loop, and the app's positive-result check does not
prove the entire aggregate was accepted. Keep complete-length checks in our
driver. Neither the 64 KiB segmentation nor these timeouts imply a hardware
rectangle-count limit.

## macOS corroboration

The normal worker enumerates a dynamic `std::list<Rect>` with
`Region::getRectList` at `0x1000908BE`, packs until the iterator reaches the
end, marks each intermediate trailer FF80, optionally appends the current
software-cursor patch, then marks the last trailer FFC0 at `0x1000917AF`.
It submits the complete aggregate at `0x100091939`. There is no located fixed
N-rectangle cutoff in this path either.

Its aggregate `_package` starts at `0x1019FF900`; the next BSS symbol `oft_y`
is at `0x10248C900`, a span of **11,063,296 bytes**. That is a linker-layout
upper bound for the storage before the next symbol, not an independently
recovered source-array declaration or adapter batch limit. No dynamic growth
or corresponding bounds check was identified in this pack loop.

macOS also copies four-byte capture pixels into the aggregate before converting
them in place (`0x100090E48`, `0x100090EC2`, converter `0x10008F760`). Its
temporary memory requirement can exceed the final two-byte UYVY payload for
the rectangle being appended. Do not use final wire length alone to reproduce
that host-side buffer policy. Its bulk wrapper passes a 3000 ms timeout.

## Android and consequences for our driver

Android defines MANUAL_BLOCK=3, but the inspected capture service selects
FRAME=0. No active multi-rectangle mode-3 sender was found there, so it adds
no count or batch-size limit to the Windows/macOS evidence.

The Linux driver encodes its current-damage and previous-damage lists as one
mode-3 request, with FF80 between rectangles and only one final FFC0. Each
source list is bounded at 64 rectangles; the combined batch can carry both
lists plus one cursor overlay rectangle. Queue overflow or an oversized
aggregate falls back to a full-frame rectangle. These are host resource
policies, not recovered hardware count limits.

Like the vendor paths, the Linux driver adds previous current-only desktop
damage to the next batch, but leaves every rectangle separate instead of
forming a bounding box. All rectangles in a batch are read from one published
source snapshot. Cursor movement is the exception to ordinary ordering: the old
cursor area is repaired, then the current cursor rectangle is sent last.
Splitting one encoded list across USB requests would need to preserve the same
FF80/FFC0 boundary; the current implementation submits the aggregate as one
checked USB request.

## Reproduction

Run `python tools/extract-partial-update-evidence.py` followed by `sync` after
the existing vendor extraction. The script now saves 60 address-indexed
excerpts and hashes, including both complete Windows send loops, allocation
sites, and DLL chunking, under ignored `drivers/partial-update-analysis/`.
The extra DLL disassembly uses the already available `llvm-objdump` and never
loads or executes vendor code.

See [evidence.json](../drivers/partial-update-analysis/evidence.json),
[Windows 1.0.0.7 sender](../drivers/partial-update-analysis/windows-1007-multi-rectangle-sender.asm),
[Windows 1.0.1.6 sender](../drivers/partial-update-analysis/windows-1016-multi-rectangle-sender.asm),
[DLL chunking](../drivers/partial-update-analysis/windows-libusb0-bulk-write-chunking.asm),
and macOS [symbols](../drivers/macos-analysis/native/symbols.txt).
