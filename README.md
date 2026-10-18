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
  `memcmp` fast path.
- Asynchronous frame conversion and USB transfer work on separate ordered
  kernel workqueues, keeping the display commit path from waiting on bulk USB
  I/O.
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
- Periodic idle tile replay is disabled. After one refresh interval without
  damage or queued transfers, the last transmitted rectangle is replayed once
  from the latest framebuffer snapshot to help the display settle.

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
| `fast_chroma` | `2` | Conversion: `0` scalar Q16, `1` SSE2/NEON, `2` AVX2/NEON. Unsupported SIMD falls back. Runtime writable. |
| `damage_scan` | `auto` | Software damage detection: `auto`, `off`, or `on`. Runtime writable. |
| `damage_scan_method` | `memcmp` | Row comparison backend: `scalar` checks exact pixels; `memcmp` treats each differing 128-byte block (32 XRGB pixels) as changed. The interface allows future SIMD backends. Runtime writable. |
| `damage_scan_stride` | `4` | Sample every Nth row, from 1 to 16. The phase follows the frame sequence; a changed sample includes the preceding N-1 rows in damage. Runtime writable. |
| `fake_edid` | `auto` | Synthetic EDID policy: `auto` falls back when needed, `on` forces it on supported ports, `off` disables it. Runtime writable. |
| `force_connected` | `auto` | Connection policy: `auto`, `on`, or `off`. Forcing connection may advertise modes the display cannot use. Runtime writable. |
| `vrr` | `1` | Advertise DRM VRR capability; load-time only and does not change timings. |
| `debugfs` | `off` | Enable transfer, conversion, damage-scan, and timing statistics; load-time only. Counters reset when the CRTC transfer session starts. |

On the first frame of each modeset epoch, the `memcmp` backend samples the
ignored X byte at 32 points on an 8×4 grid. It caches the result for that
epoch and falls back to exact scalar comparison if the sampled bytes are not
uniform.

Runtime-writable parameters are available under
`/sys/module/ms912x/parameters/`. When enabled, statistics are available as
`ms912x_stats` in the DRM device's debugfs directory.

## License

GPL-2.0-only. See [LICENSE](LICENSE). New contributions by Tiago Tagliaferri
are copyright (C) 2026; this does not replace attribution for existing work.
