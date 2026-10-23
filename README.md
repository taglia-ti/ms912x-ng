# ms912x-ng Linux Driver

This driver lets Linux use supported MacroSilicon USB display adapters as monitors.
The supported USB IDs are:

| USB ID | USB link family |
| --- | --- |
| `534d:6021`, `534d:0821` | USB 2 |
| `345f:9132`, `345f:9133`, `345f:9135` | USB 3 |

## Install and Start

Build for the running kernel and load the module:

```sh
make
sudo insmod ./ms912x.ko
```

To remove it later:

```sh
sudo rmmod ms912x
```

Your kernel needs display support, USB support, and its matching headers. DKMS
and Arch package instructions are available below.

## USB 2: Start Here

USB 2 bandwidth is limited. Sending a larger picture or updating the screen more often uses more bandwidth and can add delay. For a USB 2 adapter, start with:

```sh
sudo insmod ./ms912x.ko profile=latency scale=auto
```

`profile=latency` favors quick updates. `scale=auto` chooses a smaller USB image when the requested mode would send too much data, while keeping the selected output resolution. Scaling may make fine details softer.

If delay remains noticeable, lowering the output resolution reduces detail, and lowering the refresh rate makes motion less smooth; both reduce the amount of data sent. Higher resolution and refresh preserve more detail and smoother motion, but may increase delay on USB 2. Choose the balance that suits your display.

## Profiles

Profiles set a starting point for screen-change detection:

| Profile | Behavior | Scan stride | Color threshold |
| --- | --- | ---: | ---: |
| `efficiency` | Scan-based detection off | 5 rows | 0, exact color changes |
| `latency` | Scan every update | 2 rows | 3 to 15, adjusted through bucket levels |
| `quality` | Scan when needed | 1 row | 0, exact color changes |

With `profile=auto`, x86 systems choose `latency` below USB 3 speed and `quality` at USB 3 speed or faster. Other processor architectures choose `efficiency`.

The driver supports multiple independent USB adapters. Module parameters provide
defaults; each adapter can override its profile and runtime display settings
through writable `ms912x_*` attributes on its USB interface. `profile=auto` is
resolved from that adapter's own USB link speed. `damage_scan_method` and
`fast_chroma` remain module-wide because they select CPU implementations, while
`cursor_plane` remains a load-time module-wide control.

Multi-adapter support is considered functional based on per-device state,
software isolation and lifecycle fixtures, and runtime checks with the available
single adapter. Simultaneous operation with two or three physical adapters has
not been hardware-verified because that setup is not currently available. See
the [multi-adapter support plan](docs/multi-adapter-support-plan.md) for the
remaining hardware validation matrix.

When loading the module, options are applied from left to right: a later option for `profile`, `damage_scan`, `damage_scan_stride`, or `damage_scan_tolerance` takes precedence. For example:

```sh
sudo insmod ./ms912x.ko profile=latency damage_scan_stride=1
```

The explicit stride overrides the profile's stride. At runtime, write `profile` to one of the three individual scan settings to return that setting to the selected profile.

## Scale

`scale` controls the size of the image sent over USB. The adapter still uses the requested output mode.

| Value | Input image |
| --- | --- |
| `100` | Full size, no downscaling; default |
| `80` | Four-fifths-size path (5:4 downscale) |
| `67` | Two-thirds-size path (3:2 downscale) |
| `50` | Half-size path |
| `auto` | Choose per output mode; aliases: `on`, `1`, `y`, `Y` |
| `off`, `0`, `n`, `N` | Use `100` |

Below USB 3 speed, `auto` estimates each frame as two bytes per input pixel
(including width padding) and compares it with a 26 MB/s bandwidth estimate. It
picks the largest input size expected to send at least one update for every
four screen refreshes, trying `100`, `67`, then `50`. At USB 3 speed or
faster, it uses full size (`100`).
This is a cautious estimate, not a measurement of your adapter or computer.

You can change the setting after loading:

```sh
cat /sys/module/ms912x/parameters/scale
printf '%s\n' auto | sudo tee /sys/module/ms912x/parameters/scale
```

The new scale takes effect on the next display mode change or screen update.

## Other Settings

Module settings are under `/sys/module/ms912x/parameters/`. Rows marked
load-time only can be changed only when loading the module; the other settings
can be changed while it is running.

| Setting | What it does |
| --- | --- |
| `damage_scan` | `profile` follows the profile; `auto` scans when needed; `on` scans each update; `off` relies on changes reported by DRM. |
| `damage_scan_stride` | Number of rows between scan samples, from 1 to 16. Higher values use less scan work but may delay or miss brief changes. `profile` restores the profile value. |
| `damage_scan_tolerance` | UYVY byte bucket tolerance: 0 to 31, or a range such as `3-31`. Values round down to `0, 1, 3, 7, 15, 31`; 0 compares UYVY exactly. A range advances through bucket levels as queue pressure rises between watermarks. `profile` restores the profile range. |
| `damage_scan_method` | `auto` selects an available CPU method. `scalar`, `memcmp`, `sse2`, `avx` (alias `avx1`), or `neon` can be selected for testing. The 256-bit scanner requires AVX. Unsupported CPU methods fall back safely. |
| `fast_chroma` | Selects the color-conversion method: `auto`, `scalar`, or a supported CPU method such as `sse2`, `ssse3`, `avx2`, or `neon`. |
| `fake_edid` | `auto` uses a generated display description only when a successful read is malformed; `on` always uses it where supported; `off` disables it. |
| `force_connected` | `auto` detects the display, `on` forces it connected, and `off` forces it disconnected. Forcing connection can offer modes the display cannot show. |
| `cursor_plane` | Enables or disables the separate cursor plane. Load-time only. |
| `dbg_show_draws` | Draws a visible border around outgoing screen updates. Turning it off requests a clean refresh. |
| `vrr` | Advertises variable-refresh capability to DRM; it does not change output timings. Load-time only. |
| `debugfs` | Enables transfer and scan statistics. Load-time only. |

The primary DRM plane accepts XRGB8888 and XBGR8888 (Android RGBX byte order
on little-endian hosts). Snapshots are converted to UYVY before software scanning;
USB batches copy the admitted UYVY rectangles. The portable RGBX/BGRX converter
in `shared/` uses the same color arithmetic.

Relaxed scanning compares all four UYVY bytes (U, Y0, V, Y1) with masked XOR,
using 128-byte blocks covering 64 output pixels, then refines detected changes
into 16-pixel damage spans. Up to 8,192 damage rectangles can be retained before
the coverage fallback. A byte bucket spans at most the selected tolerance;
crossing its boundary marks the block even for a small color difference. This
conservative rule can send more damage than an absolute-difference threshold.
Changed blocks and their emitted stride margins advance the baseline; unchanged
blocks retain it. `memcmp` remains an exact comparison and ignores tolerance.
Conversion statistics measure RGB-to-UYVY work, including unchanged candidates;
copying cached UYVY into USB packets does not count as another conversion.

[UYVY pipeline benchmarks](docs/uyvy-first-pipeline.md) describe the CPU and
payload tradeoffs and the fixtures used to check the new path.

The kernel log shows the adapter model (when available), USB ID, connection speed,
and SDRAM size. Adapters without SDRAM can still appear in Linux, but this driver
cannot use them to display an image. If the SDRAM size cannot be read, display
output stays disabled until detection succeeds.

## Build and Developer Tests

To build for another installed kernel, set `KVER` or `KSRC`. DKMS is also supported:

```sh
sudo dkms install .
```

On Arch Linux, build and install the local package with `makepkg -si`; install the matching kernel headers and DKMS first.

Useful checks:

```sh
make test-capabilities
make tests
```

`make test-capabilities` checks SDRAM decoding and generated EDID data; when `edid-decode` is installed it also parses the EDID. `make tests` runs the SIMD, conversion, damage, and batch-planning tests.
Run `make test-batching` for batch coverage, replay, cursor ordering, allocation
fallbacks, area-dependent local merge budgets, and coalescing at half the display period
with watermark hysteresis;
`python3 tools/test_batching.py --sanitize` adds undefined-behavior checks. Run `make benchmark` for pixel-processing benchmarks.
For per-device USB, scan, conversion, latency, queue-pressure, and topology
captures, see the [multi-adapter measurement guide](docs/multi-adapter-measurement.md).

Regenerate the checked-in synthetic EDID header with `make generate-fake-edid`.

For ARM NEON builds, ARM32 requires a kernel configured with `CONFIG_KERNEL_MODE_NEON=y`. Cross-builds need the matching kernel tree and toolchain:

```sh
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- KSRC=/path/to/kernel/build
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KSRC=/path/to/kernel/build
```

See [the SIMD tool documentation](tools/simd_bench/README.md) for benchmark requirements. Vendor protocol research is in [docs](docs/README.md); adapter hardware capabilities are summarized in [HARDWARE_CAPABILITIES.md](HARDWARE_CAPABILITIES.md).

## License

GPL-2.0-only. See [LICENSE](LICENSE). New contributions by Tiago Tagliaferri are copyright (C) 2026; this does not replace attribution for existing work.
