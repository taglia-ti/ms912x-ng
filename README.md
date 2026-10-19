# ms912x-ng Linux driver

Linux DRM/KMS driver for MacroSilicon USB display adapters. The driver exposes
the kernel module as `ms912x`.

Supported USB IDs:

| USB ID | Interface |
| --- | --- |
| `534d:6021`, `534d:0821` | USB 2 |
| `345f:9132`, `345f:9133`, `345f:9135` | USB 3 |

## Features

- DRM/KMS modesetting with monitor EDID and hotplug handling. The synthetic
  EDID fallback prefers 1920×1080@60 and also advertises 1024×600@60 for
  compatible panels.
- Damage-based updates with an optional CPU framebuffer scan. In `auto` mode,
  the scan is used when DRM damage clips are unavailable or the scan state must
  be revalidated. Row comparisons use a stability check before enabling the
  `memcmp` fast path. Scans preserve separate changed-run rectangles at each
  backend's pixel or block granularity. Pending rectangles and the previous
  update's current-only rectangles are sent together as one mode-3 multiframe
  batch, without geometrically merging framebuffer damage. History advances
  from current damage only, so replay does not accumulate indefinitely.
- Asynchronous frame conversion and USB transfer work on separate ordered
  kernel workqueues. DRM page-flip completion is backpressured by the estimated
  drain time of queued USB payload bytes at measured bandwidth; the currently
  transmitting packet is excluded from the pressure estimate.
  The high watermark is two refresh intervals; backpressure clears at one
  refresh interval, using the active mode's refresh rate (60 Hz only when unknown).
  This hysteresis leaves room
  for cross-frame batching without unbounded queue growth; the driver
  owns a private snapshot before releasing the userspace framebuffer. Updates
  below 256 KiB of estimated UYVY damage are released once the queue clears or
  after the estimated service time for 256 KiB at measured USB bandwidth.
- USB-throughput-aware batching. When USB work is active, pending clips can wait
  briefly toward a 256 KiB UYVY batch target, but each rectangle stays separate
  in the encoded batch. There is no 512 KiB cap; when USB is idle, clips are
  dispatched promptly. If the bounded 64-rectangle queue fills, the driver
  schedules a full refresh rather than merging geometry or dropping damage.
- At 1080p, USB 2 adapters are limited to about 8 FPS for full-frame refreshes;
  small damage updates can be faster.
- XRGB8888-to-UYVY conversion using scalar Q16 arithmetic or available SSE2,
  AVX2, and NEON optimizations, with fallback when SIMD is unavailable.
- No idle replay transfers are generated. Each real update replays the previous
  update's current-only damage as separate rectangles in the same batch, matching
  the recovered Windows/macOS workaround without bounding-box merging. A failed
  or short batch makes adapter contents uncertain and triggers full-image
  resynchronization.

## Build and install

Build against the running kernel:

```sh
make
sudo insmod ./ms912x.ko
```

To build for another installed kernel, set `KVER` or `KSRC`. DKMS installation
is also supported with `sudo dkms install .`.

On Arch Linux, build and install the local DKMS package with `makepkg -si`
(requires `dkms` and the headers for the target kernel).

NEON scan and conversion support ARM64 and ARM32. ARM32 requires
`CONFIG_KERNEL_MODE_NEON=y`; the CPU's NEON capability is checked at runtime.
Unsupported CPUs (including the original Pi Zero/ARM11), kernels without
kernel-mode NEON, and contexts where SIMD is unavailable keep scalar conversion
and the existing scalar/kernel-memcmp scan fallbacks.
Pi 2/Cortex-A7 and a 32-bit kernel on Cortex-A53 can use the ARM32 path.
The NEON workers use baseline Armv7-A or Armv8-A instructions, with separate
integer-only FPU wrappers and no SVE or dot-product requirement.

Cross-build against a configured target kernel with its matching toolchain:

```sh
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- KSRC=/path/to/kernel/build
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KSRC=/path/to/kernel/build
```

The target kernel must provide DRM/KMS, GEM shmem helpers and USB support.
A router kernel with DRM disabled needs those dependencies before the module
can be loaded.

Run the SIMD correctness checks with `make tests` and the native pixel-loop
microbenchmarks with `make benchmark`. Pass harness options through `SIMD_ARGS`,
for example `make tests SIMD_ARGS='--cpu Penryn'` or
`make benchmark SIMD_ARGS='--build-dir /tmp/ms912x-bench'`. See the
[SIMD tool documentation](tools/simd_bench/README.md) for requirements and
additional profiles.

## Module parameters

| Parameter | Default | Purpose |
| --- | --- | --- |
| `fast_chroma` | `auto` | Conversion backend: `auto` selects AVX2, SSSE3, NEON, SSE2, then scalar as supported; `avx2`, `ssse3`, `neon`, and `sse2` request that backend; `scalar` selects scalar Q16. Unsupported CPU features fall back to scalar. Numeric `0` selects scalar and any positive value selects auto. Runtime writable. |
| `cursor_plane` | `on` | Expose and software-render the ARGB8888 cursor plane. Cursor movement repairs the old cursor area, then places the current cursor overlay after framebuffer damage in the mode-3 batch. Accepts `on` or `off`; load-time only. |
| `dbg_show_draws` | `0` | Draw an inverted 2-pixel UYVY border around each outgoing rectangle. Borders can flicker temporally; turning the flag off queues a clean full-screen refresh. Runtime writable. |
| `damage_scan` | `auto` | Software damage detection: `auto`, `off`/`n`/`0`, or `on`/`y`/`1`. Runtime writable. |
| `overscale` | `auto` | Adapter 2× upscaling: `auto`, `on`/`y`/`Y`/`1`, or `off`/`n`/`N`/`0`. Auto enables only on links below USB 3 for exact 4:3 modes above 1024×768 or exact 16:9 modes above 1366×768. Runtime writable. |
| `damage_scan_method` | `auto` | Backend: `auto` selects AVX2, SSE4.1 for relaxed scans, NEON, SSE2, `memcmp`, then scalar as supported. `avx2`, `sse41` (alias `sse4.1`), `neon`, and `sse2` request that SIMD backend; unsupported CPU features fall back to scalar. `memcmp` marks differing 128-byte blocks (32 pixels); `scalar` checks exact RGB pixels. Tolerance 0 selects exact comparison and nonzero selects relaxed per-pixel RGB comparison; `memcmp` ignores tolerance. Exact SIMD and `memcmp` require stable X bytes; relaxed paths ignore X. Runtime writable. |
| `damage_scan_tolerance` | `auto` | Per-channel RGB threshold: `auto` pressure-scales from 1 below low watermark to 64 at high watermark; an integer fixes the threshold, or `min-max` sets a custom pressure-scaled range (bounds 0–255). Ignored by `memcmp`. Runtime writable. |
| `damage_scan_stride` | `auto` | Row sampling: auto always uses stride `1`; explicit values are 1–16. A changed sample includes the preceding N-1 rows in damage. Runtime writable. |
| `fake_edid` | `auto` | Synthetic EDID policy: `auto` falls back only after a successful read yields malformed EDID; communication failures do not trigger it. `on`/`y`/`1` forces it on supported ports, `off`/`n`/`0` disables it. Runtime writable. |
| `force_connected` | `auto` | Connection policy: `auto`, `on`/`y`/`1`, or `off`/`n`/`0`. Forcing connection may advertise modes the display cannot use. Runtime writable. |
| `vrr` | `1` | Advertise DRM VRR capability; load-time only and does not change timings. |
| `debugfs` | `off` | Enable concise transfer, conversion, damage-batching, scan, and page-flip pacing metrics; load-time only. Counters reset when the CRTC transfer session starts. |

### Adapter overscale

Overscale changes the driver's internal image and USB input raster. The
advertised KMS mode and selected adapter output VIC/timing remain exactly at
the modeset resolution. During adapter scaling, the dimension fields of both
`VIDEO_IN` and `VIDEO_OUT` describe the smaller input canvas, matching the
recovered mode-3 scaling traces. In `auto`, links below USB 3 halve eligible
exact 4:3/16:9 modes; USB 3 and other aspect ratios remain 1:1. `on` forces 2×
adapter upscaling regardless of link speed or aspect ratio, while `off`
disables it. Runtime changes notify DRM and are applied on the next primary
plane update without changing the output VIC/timing. The active state is shown
in `ms912x_stats`.

The internal width is ceil-half rounded upward to a multiple of four for the
adapter's input-width requirement; height is ceil-half without vertical
alignment. For example, modeset `1366×768` keeps KMS and output timing at
`1366×768`, while the input canvas and `VIDEO_OUT` dimension fields are
`684×384`. Scalar, SSE2, AVX2 and NEON use the same rounded
2×2 box filter; `make tests` checks scalar-equivalent output and the QEMU ARM64
profile checks NEON. Use `make benchmark` for native downscale timings.

### Damage-scan trade-offs

The scanner compares framebuffer pixels with a private baseline and converts
detected changes into USB update rectangles. Choose settings according to the
bottleneck: `damage_scan_stride` primarily trades comparison CPU for detection
latency, while `damage_scan_tolerance` can trade subtle color changes for fewer
updates and less USB traffic. The visible failure mode is stale pixels or
flicker, not memory corruption.

| Option | When to choose it | Gain | Cost or risk |
| --- | --- | --- | --- |
| `damage_scan=off` | DRM damage clips are reliable and scan CPU is undesirable. | Avoids software comparison scans; uses the damage supplied by DRM. | An omitted or incorrect clip is not independently detected and can leave pixels stale. Framebuffer swaps without usable clips still require conservative full damage. |
| `damage_scan=auto` | Normal use; DRM usually supplies clips, but some updates need scan-based detection. | Uses clips in the common case and scans when clips are absent or the baseline needs revalidation. | A non-empty but incomplete clip list is trusted, so omitted damage can remain stale. |
| `damage_scan=on` | Diagnose missing DRM damage or prefer scan-derived damage for every relevant update. | Can detect changes that DRM clips omitted, if they are present on sampled rows and exceed tolerance. | Highest scan work; it still cannot see transient changes that appear and revert between samples. |
| `damage_scan_method=auto` | Recommended unless measuring a specific backend. | Selects an available SIMD/block backend where its correctness conditions hold, with safe fallback. | Block-based detection can enlarge rectangles and send unchanged neighboring pixels. |
| `damage_scan_method=avx2`, `sse41`, `sse2`, or `neon` | The CPU scan is a bottleneck and the named backend is supported. | SIMD usually lowers comparison CPU versus scalar. | Detection can be block-granular, increasing transmitted area. Exact SIMD paths require stable X bytes; unsupported features fall back to scalar. |
| `damage_scan_method=memcmp` | X bytes are stable and comparison CPU matters more than spatial precision. | Efficient block comparison. | A difference marks a whole 32-pixel block, which can increase USB bytes for small damage. Unstable X bytes cause scalar fallback; tolerance is ignored. |
| `damage_scan_method=scalar` | Debugging backend behavior or using a CPU without a usable SIMD backend. | Straightforward RGB comparison; tolerance 0 gives pixel-exact RGB detection. | Highest CPU cost, especially for relaxed nonzero-tolerance comparison. |
| `damage_scan_tolerance=N` (`0`–`255`) | A fixed color sensitivity is desired. | Predictable per-channel threshold independent of queue pressure. | Suppressed changes stay stale until a channel exceeds N; `memcmp` ignores tolerance. |
| `damage_scan_tolerance=min-max` | A bounded pressure-responsive threshold is desired. | Interpolates linearly from min below low watermark to max at/above high watermark. | A wider range permits more color error under queue pressure; `memcmp` ignores tolerance. |
| `damage_scan_stride=1` | Prompt, complete row coverage matters; this is the default for all link speeds. | Checks every row on each scan and minimizes sampling delay. | Most row-comparison CPU. |
| `damage_scan_stride=N` (`2`–`16`) | Scan comparison CPU is the bottleneck and some latency is acceptable. | Checks about one row in N, reducing row-comparison work roughly with N. | A changed sample marks its N-row band as damage, so USB traffic may stay similar or even rise. Persistent changes on skipped rows can remain stale for up to N-1 scan updates; a change that reverses before sampling can be missed. Full-frame snapshot/copy work remains, so total CPU does not fall by 1/N. |

**USB 2 guidance:** start with the defaults (`stride=1`, `tolerance=auto`,
which ranges from 1 to 64 as queue pressure rises). If
`present_backpressured` is `yes` and `usb_session_payload_mbps` is near the
link's practical capacity, set a fixed higher tolerance or raise the maximum of
the dynamic range to reduce transmitted damage. Watch for stale or stepped
gradients; lower it again if visible. Do not raise stride as a
bandwidth fix: it saves comparison CPU, not necessarily USB bytes, and can make
sampled damage bands taller. Raise stride only when scan CPU is the bottleneck
and the additional line latency is acceptable. Prefer `damage_scan=auto` over
`on` when DRM clips are trustworthy; use `on` when clip omissions are suspected
and accept the extra scan cost.

To tune, compare `present_backpressured`, `usb_session_payload_mbps`,
`usb_transfer_period_ms`, `scan_avg_duration_us`,
`scan_rect_candidates_avg_per_frame`, `scan_rectangles_output_avg_per_frame`,
`scan_damage_bbox_of_frame_percent`, and `conversion_avg_us` in `ms912x_stats`.
A bounding-box merge preserves changed pixels but may include unchanged pixels,
trading more USB bytes for fewer rectangles.

On the first frame of each modeset epoch, the `memcmp` backend samples the
ignored X byte at 32 points on an 8×4 grid. It caches the result for that
epoch and falls back to exact scalar comparison if the sampled bytes are not
uniform.

Runtime-writable parameters are available under
`/sys/module/ms912x/parameters/`. When enabled, statistics are available as
`ms912x_stats` in the DRM device's debugfs directory.

Statistics cover the current CRTC transfer session. Input update and USB
transfer rates are session averages; USB transfers are requests and do not
necessarily equal complete displayed frames. Scan and conversion throughput
are reported in Mpixels/s. Baseline-seeding full scans are excluded from scan
comparison totals because they do not compare pixels.
`scan_damage_bbox_of_frame_percent` reports the damage bounding-box area as a
fraction of total framebuffer area.

## Vendor protocol analysis

See the [vendor protocol documentation](docs/README.md) for adapter capabilities,
quirks, driver analyses, and extraction commands.

## License

GPL-2.0-only. See [LICENSE](LICENSE). New contributions by Tiago Tagliaferri
are copyright (C) 2026; this does not replace attribution for existing work.
