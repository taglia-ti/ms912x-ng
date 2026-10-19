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
  `memcmp` fast path. Each software scan submits at most one bounding rectangle
  to avoid temporal flicker between separately transferred regions.
- Asynchronous frame conversion and USB transfer work on separate ordered
  kernel workqueues. DRM page-flip completion is backpressured by the estimated
  drain time of queued USB payload bytes at measured bandwidth; the currently
  transmitting packet is excluded from the pressure estimate.
  The high watermark is two refresh intervals; backpressure clears below 1.5
  intervals, using the active mode's refresh rate (60 Hz only when unknown).
  This hysteresis leaves room
  for cross-frame damage coalescing without unbounded queue growth; the driver
  owns a private snapshot before releasing the userspace framebuffer. Updates
  below 256 KiB of estimated UYVY damage are released once the queue clears or
  after the estimated service time for 256 KiB at measured USB bandwidth.
- USB-throughput-aware damage coalescing. When USB work is already queued or
  active, nearby clips are combined toward a 256 KiB UYVY payload while limiting
  unchanged area to half of the resulting rectangle once the combined payload
  exceeds 256 KiB. There is no 512 KiB cap; when USB is idle, clips are
  dispatched promptly. If an active transfer is ending, any accumulated clips
  are sent without an extra delay. If the bounded clip queue fills and no
  eligible merge remains, the driver schedules a full refresh rather than
  dropping dirty regions.
- At 1080p, USB 2 adapters are limited to about 8 FPS for full-frame refreshes;
  small damage updates can be faster.
- XRGB8888-to-UYVY conversion using scalar Q16 arithmetic or available SSE2,
  AVX2, and NEON optimizations, with fallback when SIMD is unavailable.
- No idle replay transfers are generated; USB traffic is limited to actual
  framebuffer damage and retries of failed damage.

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

## Module parameters

| Parameter | Default | Purpose |
| --- | --- | --- |
| `fast_chroma` | `auto` | Conversion backend: `auto` selects AVX2, NEON, SSE2, then scalar as supported; `avx2`, `neon`, and `sse2` force that backend; `scalar` forces scalar Q16. Numeric `0` selects scalar and any positive value selects auto. Runtime writable. |
| `cursor_plane` | `on` | Expose and software-render the ARGB8888 cursor plane. Cursor and screen damage use the normal FIFO/coalescing path. Accepts `on` or `off`; load-time only. |
| `dbg_show_draws` | `0` | Draw an inverted 2-pixel UYVY border only around new base damage after conversion. The anti-flicker/Fibonacci on-wire union can be larger and its replayed area stays unmarked. Borders can flicker temporally; turning the flag off queues a clean full-screen refresh. Runtime writable. |
| `damage_scan` | `auto` | Software damage detection: `auto`, `off`/`n`/`0`, or `on`/`y`/`1`. Runtime writable. |
| `damage_scan_method` | `auto` | Backend: `auto` selects AVX2, NEON, SSE2, `memcmp`, then scalar as supported. `avx2`, `neon`, and `sse2` force that SIMD backend; `memcmp` marks differing 128-byte blocks (32 pixels); `scalar` checks exact RGB pixels. Tolerance 0 selects exact comparison and nonzero selects relaxed per-pixel RGB comparison; `memcmp` ignores tolerance. Exact SIMD and `memcmp` require stable X bytes; relaxed paths ignore X. Runtime writable. |
| `damage_scan_tolerance` | `auto` | Per-channel RGB threshold: auto selects `64` on USB 2 and `0` on USB 3; explicit values are 0–255. Nonzero tolerance leaves sub-threshold pixels anchored to the prior baseline. Ignored by `memcmp`. Runtime writable. |
| `damage_scan_stride` | `auto` | Row sampling: auto selects stride `3` on USB 2 and `1` on USB 3; explicit values are 1–16. A changed sample includes the preceding N-1 rows in damage. Runtime writable. |
| `fake_edid` | `auto` | Synthetic EDID policy: `auto` falls back only after a successful read yields malformed EDID; communication failures do not trigger it. `on`/`y`/`1` forces it on supported ports, `off`/`n`/`0` disables it. Runtime writable. |
| `force_connected` | `auto` | Connection policy: `auto`, `on`/`y`/`1`, or `off`/`n`/`0`. Forcing connection may advertise modes the display cannot use. Runtime writable. |
| `vrr` | `1` | Advertise DRM VRR capability; load-time only and does not change timings. |
| `debugfs` | `off` | Enable concise transfer, conversion, damage-scan, coalescing, and page-flip pacing metrics; load-time only. Counters reset when the CRTC transfer session starts. |

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
are reported in Mpixels/s. `scan_compared_pixel_diff_percent` is the fraction
of pixels actually compared that differed; it does not estimate the fraction
of the whole frame that changed and depends on scan stride and tolerance.
Baseline-seeding full scans are excluded because they do not compare pixels.
`scan_damage_bbox_of_frame_percent` reports the damage bounding-box area as a
fraction of total framebuffer area.

## License

GPL-2.0-only. See [LICENSE](LICENSE). New contributions by Tiago Tagliaferri
are copyright (C) 2026; this does not replace attribution for existing work.
