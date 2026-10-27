# ms912x-ng

Use MacroSilicon USB display adapters with Linux and Android.

## Features

- **Partial updates:** send changed screen regions to reduce USB traffic and improve update times.
- **Linux:** DRM display, per-adapter settings, EDID detection, and USB scaling.
- **Android:** screen mirroring and a pseudo-extended screen on supported devices, without root. The separate screen uses Wireless debugging. See the [Android guide](android/README.md).

## Supported adapters

| USB IDs | USB |
| --- | --- |
| `534d:6021`, `534d:0821` | USB 2 |
| `345f:9132`, `345f:9133`, `345f:9135` | USB 3 |

Display output requires adapter SDRAM. Multiple physical adapters have not been tested together.

## Linux installation

Install matching kernel headers, then build and load:

```sh
make
sudo make install-udev
sudo udevadm control --reload-rules
sudo insmod ./ms912x.ko
```

Remove the module with `sudo rmmod ms912x`. For DKMS and Arch packages, see [Build and tests](#build-and-tests).

## Adapter settings

Settings are per USB interface. Find yours under `/sys/bus/usb/drivers/ms912x/`:

```sh
adapter=/sys/bus/usb/devices/1-2:1.0  # Replace with your interface.
cat "$adapter/ms912x_damage_scan"
printf '%s\n' auto | sudo tee "$adapter/ms912x_scale"
```

The installed [udev rules](linux/udev/60-ms912x.rules) apply these scan settings on bind:

| Host and USB link | Scan | Stride | Tolerance |
| --- | --- | ---: | --- |
| x86, USB 2 or slower | `on` | 2 | `3-15` |
| x86, USB 3 or faster | `auto` | 1 | `0` |
| Other architectures | `off` | 5 | `0` |

Rules require systemd-udev 244 or newer. The Arch package includes them; a direct DKMS install requires `sudo make install-udev`. Reconnect the adapter or apply them now:

```sh
sudo udevadm control --reload-rules
sudo udevadm trigger --action=bind "$adapter"
sudo udevadm settle
```

Without udev rules, defaults are scan `auto`, stride `1`, tolerance `0`, scale `100`, fake EDID `auto`, and connection `auto`.

For persistent changes, add a later rule such as `/etc/udev/rules.d/90-ms912x-local.rules`:

```udev
ACTION=="bind", SUBSYSTEM=="usb", DRIVER=="ms912x", KERNEL=="1-2:1.0", TEST=="ms912x_damage_scan", ATTR{ms912x_damage_scan}="on", ATTR{ms912x_damage_scan_stride}="2", ATTR{ms912x_damage_scan_tolerance}="3-15", ATTR{ms912x_scale}="auto"
```

Replace `1-2:1.0` with your interface. See the [udev reference](https://www.freedesktop.org/software/systemd/man/latest/udev.html) for other matching options.

## Scale

`ms912x_scale` reduces the image sent over USB while retaining the output resolution. Smaller images can update faster but look softer.

| Value | USB image |
| --- | --- |
| `100` | Full size (default) |
| `80` | Four-fifths size |
| `67` | Two-thirds size |
| `50` | Half size |
| `auto` | Select for the output mode (`on`, `1`, `y`, `Y`) |
| `off`, `0`, `n`, `N` | Full size |

On USB 2, `auto` chooses a size using a 26 MB/s estimate and skips scaled widths above 1280 pixels. USB 3 uses full size. Manual scaling above 1280 pixels can corrupt output; see [adapter quirks](HARDWARE_QUIRKS.md). Changes apply on the next mode change or screen update.

## Other settings

Set these per adapter through sysfs.

| Attribute | Description |
| --- | --- |
| `ms912x_damage_scan` | `auto`, `on`, or `off`; `off` uses DRM damage. |
| `ms912x_damage_scan_stride` | Sample every 1–16 rows; higher values can miss brief changes. |
| `ms912x_damage_scan_tolerance` | UYVY tolerance `0`–`127` or a range such as `3-127`. Values round down to `0`, `1`, `3`, `7`, `15`, `31`, `63`, or `127`; queue pressure selects within the range. |
| `ms912x_fake_edid` | `auto` replaces malformed EDID, `on` forces it where supported, `off` disables it. |
| `ms912x_force_connected` | `auto`, `on`, or `off`. Forced connection may offer unsupported modes. |

Global options are under `/sys/module/ms912x/parameters/`:

| Option | Description |
| --- | --- |
| `damage_scan_method` | `auto`, `scalar`, `memcmp`, `sse2`, `avx` (`avx1`), or `neon`. `memcmp` ignores tolerance. |
| `fast_chroma` | Conversion backend: `auto`, `scalar`, `avx2`, `ssse3`, `sse2`, or `neon`. |
| `dbg_show_draws` | Show damage borders. |
| `cursor_plane` | Separate cursor plane; load-time only. |
| `vrr` | Advertise variable refresh to DRM; load-time only. |
| `debugfs` | Enable transfer and scan statistics; load-time only. |

The DRM plane accepts XRGB8888 and XBGR8888. See [pipeline benchmarks](docs/uyvy-first-pipeline.md) and [hardware capabilities](HARDWARE_CAPABILITIES.md).

## Android

The main app requires Android 11+. A mirror-only build supports Android 7+. See the [Android guide](android/README.md) and [phone test guide](android/TESTING.md).

## Build and tests

```sh
make android             # Android debug APK
make linux KVER=...      # Linux module for an installed kernel
make tests               # Linux and Android host tests
make dkms-package        # DKMS source archive
make clean               # Clean build outputs
```

For Android device tests, run `make android-test-device DEVICE_SERIAL=<adb-serial>`. Set `MIRROR_ONLY=true` for the mirror-only APK. See [test coverage](docs/testing.md).

Install the DKMS archive:

```sh
make dkms-package
version=REPLACE_WITH_VERSION  # Use the printed version.
sudo tar -xzf "build/dkms/ms912x-ng-${version}.tar.gz" -C /usr/src
sudo dkms install -m ms912x-ng -v "$version"
sudo make install-udev
sudo udevadm control --reload-rules
```

On Arch Linux, install DKMS and matching kernel headers, then run `makepkg -si`. The PKGBUILD downloads and verifies the tagged release source.

See [SIMD tools](tools/simd_bench/README.md), [multi-adapter measurements](docs/multi-adapter-measurement.md), and [protocol research](docs/README.md) for more details.

## License

GPL-2.0-only. See [LICENSE](LICENSE). New contributions by Tiago Tagliaferri are copyright (C) 2026; existing attribution remains in place.
