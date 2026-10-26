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
sudo -A make install-udev
sudo -A udevadm control --reload-rules
sudo -A insmod ./ms912x.ko
```

To remove it later:

```sh
sudo -A rmmod ms912x
```

Your kernel needs display support, USB support, and its matching headers. DKMS
and Arch package instructions are available below.

## Per-adapter Settings and udev Presets

The Linux module has no profile selector. Runtime display settings are writable
`ms912x_*` attributes on each bound USB interface, for example:

```sh
adapter=/sys/bus/usb/devices/1-2:1.0  # Replace with your adapter's interface.
cat "$adapter/ms912x_damage_scan"
printf '%s\n' auto | sudo -A tee "$adapter/ms912x_scale"
```

Find the interface with `ls /sys/bus/usb/drivers/ms912x/`; choose the entry ending
in `:1.0` (or the corresponding interface number), rather than a DRM `cardN`.
Each adapter has independent scan, scale, EDID, and connection settings.

[60-ms912x.rules](linux/udev/60-ms912x.rules) applies the former automatic scan presets
when each interface binds:

| Host and negotiated USB link | Former preset | Scan mode | Stride | Tolerance |
| --- | --- | --- | ---: | --- |
| x86, USB 2 or slower | latency | `on` | 2 | `3-15` |
| x86, USB 3 or faster | quality | `auto` | 1 | `0` |
| Other architectures | efficiency | `off` | 5 | `0` |

Install the rules with `sudo -A make install-udev`; the Arch package includes them.
Direct DKMS installation installs the module only, so install the rules separately.
The architecture match requires systemd-udev 244 or newer. Reload the rules after
installation with `sudo -A udevadm control --reload-rules`. They apply on the next
bind or reconnect. To apply them to an already bound interface:

```sh
sudo -A udevadm trigger --action=bind "$adapter"
sudo -A udevadm settle
```

Without the rules, the fixed driver defaults are scan `auto`, stride `1`, tolerance
`0`, scale `100`, fake EDID `auto`, and forced connection `auto`. Writing `inherit`
to an adapter attribute restores its fixed driver default; it does not reapply
udev settings. Trigger the bind event above to reapply the rules.

For persistent customization, place a later rule in
`/etc/udev/rules.d/90-ms912x-local.rules`. For example, apply latency settings and
automatic scaling to one physical USB port (replace `1-2:1.0`):

```udev
ACTION=="bind", SUBSYSTEM=="usb", DRIVER=="ms912x", KERNEL=="1-2:1.0", TEST=="ms912x_damage_scan", ATTR{ms912x_damage_scan}="on", ATTR{ms912x_damage_scan_stride}="2", ATTR{ms912x_damage_scan_tolerance}="3-15", ATTR{ms912x_scale}="auto"
```

Later local rules override the packaged presets. Rules write sysfs attributes
directly; they require no helper process. See the [udev rule reference](https://www.freedesktop.org/software/systemd/man/latest/udev.html)
for matching an adapter by USB serial or other parent attributes.

Remove old `profile`, `damage_scan`, `damage_scan_stride`,
`damage_scan_tolerance`, `scale`, `fake_edid`, and `force_connected` options from
module-loading commands and `/etc/modprobe.d/`; the module no longer accepts them.

USB 2 bandwidth is limited. `ms912x_scale=auto` chooses a smaller USB image when
the requested mode would send too much data, while preserving the output
resolution. Fine details may become softer. Lowering output resolution or refresh
rate can also reduce transfer delay.

Multi-adapter support has software isolation and lifecycle coverage, plus runtime
checks from the earlier implementation with one adapter. Simultaneous operation
with two or three physical adapters remains unverified. See the
[multi-adapter measurement guide](docs/multi-adapter-measurement.md) for
measurement and validation limits.

## Scale

`ms912x_scale` controls the size of the image sent over USB. The adapter still uses the requested output mode.

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
four screen refreshes, trying `100`, `80`, `67`, then `50`. It skips candidates
wider than 1280 pixels. At USB 3 speed or faster, it uses full size (`100`).
This is a cautious estimate, not a measurement of your adapter or computer.
Manual scaled input above 1280 pixels can corrupt output on the tested setup;
the driver warns but does not cap a manual choice. See [adapter quirks](HARDWARE_QUIRKS.md).

You can change the setting after loading:

```sh
cat "$adapter/ms912x_scale"
printf '%s\n' auto | sudo -A tee "$adapter/ms912x_scale"
```

The new scale takes effect on the next display mode change or screen update.

## Other Settings

Display settings below are per-adapter USB-interface attributes. Each also
accepts `inherit` to restore the fixed default listed above.

| Attribute | What it does |
| --- | --- |
| `ms912x_damage_scan` | `auto` scans when needed; `on` scans each update; `off` relies on changes reported by DRM. |
| `ms912x_damage_scan_stride` | Rows between scan samples, from 1 to 16. Higher values use less scan work but may delay or miss brief changes. |
| `ms912x_damage_scan_tolerance` | UYVY byte bucket tolerance: 0 to 127, or a range such as `3-127`. Values round down to `0, 1, 3, 7, 15, 31, 63, 127`; 0 compares UYVY exactly. Queue pressure selects a bucket within the range. |
| `ms912x_fake_edid` | `auto` uses a built-in display description only when a successful read is malformed; `on` always uses it where supported; `off` disables it. |
| `ms912x_force_connected` | `auto` detects the display, `on` forces it connected, and `off` forces it disconnected. Forcing connection can offer modes the display cannot show. |

The remaining global options are under `/sys/module/ms912x/parameters/`:

| Module option | What it does |
| --- | --- |
| `damage_scan_method` | Runtime: `auto`, `scalar`, `memcmp`, `sse2`, `avx` (alias `avx1`), or `neon`. Unsupported CPU methods fall back safely; `memcmp` ignores tolerance. |
| `fast_chroma` | Runtime conversion backend: `auto`, `scalar`, `avx2`, `ssse3`, `sse2`, or `neon`. |
| `dbg_show_draws` | Runtime damage borders. Turning it off requests a clean refresh on all adapters. |
| `cursor_plane` | Enables or disables the separate cursor plane. Load-time only. |
| `vrr` | Advertises variable-refresh capability to DRM; it does not change output timings. Load-time only. |
| `debugfs` | Enables transfer and scan statistics. Load-time only. |

The primary DRM plane accepts XRGB8888 and XBGR8888 (Android RGBX byte order
on little-endian hosts). Snapshots are converted to UYVY before software scanning;
USB batches copy the admitted UYVY rectangles. Linux and Android use the same
RGBX/BGRX color cores in `shared/`, without a separate channel-swap pass. The source
format is stored per device; changing it invalidates queued frames and requests
a full refresh.

Relaxed scanning compares all four UYVY bytes (U, Y0, V, Y1) with masked XOR,
using 128-byte blocks covering 64 output pixels, then refines detected changes
into 16-pixel damage spans. Up to 8,192 damage rectangles can be retained before
the coverage fallback. A byte bucket spans at most the selected tolerance;
crossing its boundary marks the block even for a small color difference. This
conservative rule can send more damage than an absolute-difference threshold.
Buckets 63 and 127 ignore six and seven low bits per byte respectively;
127 retains only the most significant bit for comparison.
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

Linux module sources, architecture flags, and udev rules live in `linux/`;
`linux/Makefile` builds and cleans only the external kernel module. The Android
Gradle project has its own `android/Makefile`. The root `Makefile` keeps the
project-wide entry points and produces `ms912x.ko` at the repository root.
Code shared with Android stays in `shared/` and the common root files.

The root targets are:

```sh
make android             # assemble the Android debug APK
make linux KVER=...      # build for an installed kernel
make tests               # run Linux and Android host tests
make dkms-package        # create a DKMS source tarball under build/dkms/
make clean               # clean both Linux and Android build outputs
```

To run Android instrumentation tests on a connected emulator or device, use
`make android-test-device DEVICE_SERIAL=<adb-serial>`. Set `MIRROR_ONLY=true`
for the API 24 mirror-only APK. To build the Linux module for another installed
kernel, set `KVER` or `KSRC`.

To install a generated DKMS source archive, extract it into `/usr/src` and add
it to DKMS, then install it for the target kernel:

```sh
make dkms-package
version=REPLACE_WITH_VERSION  # Use the version printed by make dkms-package.
sudo -A tar -xzf "build/dkms/ms912x-ng-${version}.tar.gz" -C /usr/src
sudo -A dkms install -m ms912x-ng -v "$version"
```

On Arch Linux, build and install the local package with `makepkg -si`; install
the matching kernel headers and DKMS first.

Useful checks:

```sh
make test-capabilities
make tests
make test-udev
```

`make test-capabilities` checks SDRAM decoding, EDID byte-read bounds/failures,
and generated EDID data; when
`edid-decode` is installed it also parses the EDID. `make tests` runs the Linux
production conversion, damage, batch, SIMD and capability fixtures plus Android host-side unit,
native pipeline, helper-control, mouse-routing and ADB bridge tests. It does not
run device instrumentation unless `make android-test-device` is invoked.
See the [test target map](docs/testing.md) for Linux driver call sites and
fixture boundaries.
Run `make test-batching` for batch coverage, replay, cursor ordering, allocation
fallbacks, area-dependent local merge budgets, and coalescing at half the display period
with watermark hysteresis;
`python3 tools/test_batching.py --sanitize` adds undefined-behavior checks. Run `make benchmark` for pixel-processing benchmarks.
`make test-udev` runs the real udev rules in an isolated synthetic USB tree. It
requires bubblewrap with unprivileged user namespaces, a C compiler, and udevadm
with JSON test output. Architecture input is supplied by a test shim; it does
not run on ARM hardware or exercise physical USB binding.

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
