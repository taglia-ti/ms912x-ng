# UYVY-first scanning

RGB snapshots are captured and scaled before the scan worker converts a consumed
candidate to UYVY. Queued RGB snapshots can still be replaced before conversion.
The scanner compares UYVY against its retained baseline, then publishes the UYVY
cache and admitted damage together. USB requests stage and copy those UYVY
rectangles, with cursor composition confined to the final packet. Replay,
generation checks, and the two ordered full refresh batches remain in place.

XRGB8888 and XBGR8888 primary framebuffers are accepted. On little-endian hosts,
these are BGRX and RGBX bytes respectively. Capture and scaling preserve the source
byte order. The format is stored once per device, synchronized with the frame
generation under `transfer_lock`. A format change starts a new generation and
requests a full refresh, including when resolution is unchanged. The worker reads
format and generation together; obsolete queued frames are discarded, and an
in-flight old conversion cannot publish after the change.

The portable `shared/ms912x_rgbx_convert.h` API accepts either byte order, explicit
source byte bounds, row/pixel strides, crop coordinates and destination pitch.
Linux and Android share the scalar arithmetic and SSE2, SSSE3, AVX2 and NEON cores.
Channel extraction is specialized before the pixel loop, so RGBX needs no separate
red/blue swap pass. Both input orders are checked against the same UYVY oracle.

The kernel conversion contract is declared in `ms912x_convert.h`:
`ms912x_convert_rgb_to_uyvy()` validates the DRM format and rectangle, selects the
backend and writes the UYVY cache. Its architecture backends handle explicit
destination pitch and kernel FPU ownership, while the shared cores handle color
arithmetic. Capture owns the source format and generation. Conversion timing
remains in the transfer wrapper, outside the color kernels.

The old implicit-XRGB `ms912x_convert_to_uyvy()` and architecture-specific XRGB
aliases were removed; tests and benchmarks call the same current API as the driver.
Correctness checks compile only current sources. The two constant
RGBX/BGRX row specializations remain intentional: they eliminate channel-order
branches from the pixel loops and keep SIMD inside the protected worker calls.

The UYVY mask is `~(bucket * 0x01010101U)`, for buckets
0, 1, 3, 7, 15, 31, 63 and 127. The maximum retains one comparison bit per byte.
All four bytes matter: U, Y0, V and Y1. This quantization policy does not measure
absolute differences: crossing a bucket boundary still marks a change. RGB and
UYVY tolerance numbers are not equivalent measures of visual error. Explicit
`memcmp` ignores tolerance and compares every bit.

Comparison-only scalar/SIMD kernels avoid writing a replacement baseline for
unchanged blocks. The worker updates admitted runs and sampling margins in place;
unchanged and unsampled content retains its baseline. The scan slot excludes
concurrent direct capture. Metadata invalidation does not modify pixels, and a
rejected publication invalidates the speculative baseline before another scan.
A valid full update initializes the entire baseline. This removes the second
baseline buffer and incremental full-frame clones. At 1920x1200, four UYVY image
buffers replace four RGB buffers, saving 18,432,000 bytes before compactor scratch.
Raw capture slots and scaler scratch remain RGB.

SIMD comparisons retain their 128-byte blocks covering 64 UYVY pixels. The run
builder refines each coarse hit into 16-pixel spans using the same tolerance mask.
Only admitted spans and sampling margins advance the baseline. Scalar masked
scans use the same output granularity; explicit `memcmp` retains exact 64-pixel
blocks. The damage vectors grow to 8,192 rectangles, and current plus replay and
the cursor can occupy up to 16,385 rectangles in an assembled batch.

The final compactor first joins touching or overlapping rectangles with identical
horizontal spans, then identical vertical spans. These unions add no pixels and
recover groups missed by the eight-candidate local compactor. The existing bounded
expansion policy runs afterwards, once per assembled current/replay batch. Each
request owns a 384 KiB index plus its epoch counter; pending producers never share
that scratch. Up to eight rectangles bypass indexing. There is no new per-frame
allocation. Cursor ordering and the hard packet-size fallback are unchanged.

## Reproduce

```sh
make -j"$(nproc)" tests
make test-capabilities
make -j"$(nproc)"
python3 tools/test_uyvy_pipeline.py --sanitize
python3 tools/test_batching.py --sanitize
python3 tools/test_damage.py --sanitize
python3 tools/test_uyvy_pipeline.py --arm
python3 tools/simd_bench/run.py --arm
python3 tools/test_uyvy_pipeline.py --benchmark > /tmp/uyvy-benchmark.csv
```

The cross checks need the already-installed ARM64 compiler and QEMU. The pipeline
fixture also runs natively on ARM64. The route comparison benchmark requires
x86-64 with AVX2. None of these commands installs or reloads the module. ARM32 execution was not
checked because `arm-linux-gnueabihf-gcc` is not installed.

Fixtures cover pitched/cropped conversion, guard bytes, Android RGBX/BGRX parity,
all 256-by-256 byte comparisons in each UYVY lane and tolerance bucket, Y1-only
changes, comparison-only tails, retained sampled baselines, packet bytes and the
single final FFC0 marker. Stream checks use the production row dispatcher for every
scan method and reconstruct the output after all sampling phases. Separate fixtures
exercise publication/invalidation races, direct capture,
request reuse, replay, cursor composition, allocation failures, adapter isolation,
index epoch wrap, and maximum rectangle counts.

## Initial 64-pixel span measurements

The following measurements use the initial 64-pixel emitted spans and 2,048-entry
damage limit. The implemented 16-pixel spans and 8,192-entry limit are covered
separately below; these initial results remain available for comparison.

[Raw results](benchmarks/uyvy-first-2026-10-23.csv), measured on an AMD Ryzen 9
5900XT, GCC 16.2.1 `-O2`, pinned CPU 0, 1920x1080, AVX scan, AVX2 conversion,
tolerance 3. Each value is the median of seven samples of sixteen fixed
transitions, after warmup; route order alternates between samples. The unchanged
baseline is restored outside the timer. This is a warm-cache CPU microbenchmark,
not a kernel scheduling, USB throughput, latency or display-FPS measurement.

The timed path includes source capture, conversion, production scan/run building,
baseline writes, production compaction, source staging and payload copying.
The RGB reference uses the former 32-pixel masked blocks and two RGB baselines;
the UYVY path uses 32 words covering 64 pixels and one selectively updated
baseline. RGB conversion writes directly into packed rectangle payloads; the
reference counts headers/trailers without writing them. UYVY writes real packet
headers/trailers. Synchronization, queued coalescing, payload invalidation,
replay accumulation, cursor work, USB and real kernel FPU entry/exit costs are
excluded. The patterns toggle high RGB bits; they do not establish equivalent
quality between RGB and UYVY tolerance 3.

| Pattern | RGB first, stride 1, ms | UYVY first, stride 1, ms | RGB first, stride 2, ms | UYVY first, stride 2, ms |
| --- | ---: | ---: | ---: | ---: |
| Static | 0.674 | 0.664 | 0.792 | 0.632 |
| 64x64 patch | 0.653 | 0.663 | 0.796 | 0.632 |
| 10% window | 0.746 | 0.670 | 1.011 | 0.702 |
| 25% window | 0.978 | 0.737 | 1.143 | 0.691 |
| Full frame | 1.683 | 0.930 | 1.900 | 0.928 |
| Sparse grid | 1.144 | 0.683 | 1.126 | 0.677 |

Full-frame and quarter-frame CPU work falls by 45% and 25% at stride 1. Small
updates can lose: the 64x64 patch costs about 1% more in this run. Static timings
are close and should not be treated as a universal win; early conversion is
still paid for every consumed candidate. RGB-first can avoid conversions for
unchanged content and coalesce pending updates before conversion.

Both routes emit the same payload sizes for the patch, windows and full-frame
cases. Sparse grid output increases from 277,440 to 522,784 bytes at stride 1,
and from 340,800 to 649,504 at stride 2, because UYVY blocks cover more output
pixels. The stride-1 grid rectangle count falls from 1020 to 34 because adjacent
64-pixel blocks join into row spans. Fewer rectangles do not necessarily mean
fewer bytes. The CPU gain does not prove a win when USB bandwidth is limiting.

The initial 64-pixel spans kept the existing 128-byte SIMD reduction size.
Earlier local prototypes with 128/256-pixel UYVY blocks gave little CPU benefit
and substantially enlarged small patches. Block size is an output/CPU tradeoff,
not a constraint inherited from RGB scan geometry.

Isolated final-compactor measurements use seven samples of 256 calls. Input
copying and output byte counting are outside the timer:

| Pattern | Local compactor, us | Indexed compactor, us | Rectangles before/after | Encoded bytes before/after |
| --- | ---: | ---: | ---: | ---: |
| 256 interleaved fragments | 18.844 | 5.916 | 256 / 16 | 135168 / 131328 |
| 32 overlapping replay columns | 2.052 | 1.190 | 32 / 16 | 786944 / 524544 |
| 1020 separated islands | 78.603 | 82.941 | 1020 / 1020 | 48960 / 48960 |

Indexing adds about 4.3 us for this unmergeable sparse case. The fragment and
replay savings justify the bounded extra pass without widening the local search
or spending additional pixel area. Actual display behavior and performance under
USB backpressure still require adapter testing.

## Implemented 16-pixel spans and 8,192 rectangles

[Current raw results](benchmarks/uyvy-16px-8192rects-2026-10-23.csv) use the same
harness, CPU, conversion/scan backends, tolerance, resolution and sampling method
as the initial table. The emitted UYVY spans are now 16 pixels, while SIMD checks
still cover 64 pixels. Both routes have the new rectangle limit; none of these
patterns reaches it. The resized index covers the full current/replay batch.
These are separate runs: small timing differences between tables include system
variation, so the paired RGB/UYVY columns are the useful current comparison.

| Pattern | RGB first, stride 1, ms | UYVY first, stride 1, ms | RGB first, stride 2, ms | UYVY first, stride 2, ms |
| --- | ---: | ---: | ---: | ---: |
| Static | 0.685 | 0.669 | 0.862 | 0.649 |
| 64x64 patch | 0.711 | 0.649 | 0.888 | 0.647 |
| 10% window | 0.801 | 0.689 | 1.016 | 0.714 |
| 25% window | 1.039 | 0.739 | 1.220 | 0.753 |
| Full frame | 1.756 | 1.057 | 1.995 | 1.024 |
| Sparse grid | 1.186 | 1.036 | 1.303 | 0.916 |

Sparse-grid UYVY payload falls from the initial 522,784 to 146,880 bytes at
stride 1 (72% less), and from 649,504 to 178,560 at stride 2 (73% less). The RGB
reference emits 277,440 and 340,800 bytes respectively. There are now 1,020 sparse
rectangles instead of 34 wide strips. Building, compacting and packing those
separate rectangles costs more CPU than the original 64-pixel UYVY spans; the
byte savings do not imply a universally free CPU improvement. The aligned patch,
windows and full-frame payload sizes are unchanged.

Before applying the change, prototypes retaining 64-pixel SIMD checks with
16-pixel emitted spans showed roughly 2-6% full-frame CPU overhead across runs.
The fragmented grid cost about 0.28 ms more CPU while sending 72% fewer bytes.
Further shrinking to 8/4 pixels increased dense-frame cost. Reusing fine-block
SIMD flags was also prototyped, but is unnecessary for the selected run refinement.
Those experiments used the old rectangle limit; the table above measures the
implemented granularity and larger limit together.

New regression cases verify isolated fine spans and gaps inside one coarse hit,
all four UYVY lanes, mask-suppressed neighbours, coarse/fine boundaries, clipped
row tails and selective baseline updates on both AVX and SSE2. ARM64/QEMU runs the
same cases through NEON. Capacity fixtures fill all 8,192 scan rectangles before
exercising overflow and compact a maximum 16,385-rectangle batch with the index
still enabled. Driver and fixtures share the rectangle-limit definitions.
