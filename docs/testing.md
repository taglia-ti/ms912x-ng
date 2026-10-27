# Driver test targets

Correctness runners compile current production translation units or extract
complete function definitions from the current checkout without replacing their
bodies. Independent oracles generate expected results; they are not presented as
implementations of the driver. Tests must not keep a production alias alive solely
for a fixture. A fixture adapter may supply arguments to an active API, but must
not implement conversion, scanning, filtering or serialization itself.

| Coverage | Current production entry | Driver caller |
| --- | --- | --- |
| DRM RGBX/BGRX conversion, all RGB triples and guards | `ms912x_convert_rgb_to_uyvy` | `linux/ms912x_transfer.c` |
| Android image bounds, stride, crop and conversion | `ms912x_rgbx_prepare` / `ms912x_rgbx_execute` | Android `frame_pipeline.c` |
| UYVY backend selection and comparison | `ms912x_core_scan_rows` | `linux/ms912x_damage.c`, shared `ms912x_scan_core.c` |
| Damage runs, sampling and margins | `ms912x_damage_scan_process_row` / `ms912x_damage_scan_extend_bottom` | Linux damage worker |
| Shared scan geometry and packet encoding | `ms912x_core_scan_words_backend` / `ms912x_core_mode3_encode_rectangles` | Android shared damage pipeline |
| Batch compaction and replay | `ms912x_rects_compact_indexed` / `ms912x_batch_build` | Linux transfer and Android shared damage pipeline |
| 3:2 and 5:4 crop/filter behavior | Production `*_region` entries | Capture and scale dispatchers |
| Packet staging, cursor and transfer completion | Production packer, compositor and complete USB worker | Linux output pipeline |
| EDID register bounds and partial read failures | `ms912x_core_read_edid_bytes` | Linux connector and Android USB setup |

The USB/payload integration fixture instruments the real packer and compositor,
and runs the complete worker through production completion. Only its low-level
USB execution is simulated. Serialized pixel assertions decode packet coordinates
instead of treating transport memory as a raw frame. Rectangle vector growth in
the pixel/geometry fixtures uses the production helper with a libc allocator shim.

Isolated lifecycle and concurrency fixtures also mock neighbouring helpers to
force control-flow failures and races. Their assertions cover ownership,
publication and cleanup, not the mocked helper's arithmetic. Pixel, geometry and
protocol claims come from the production integration fixtures above. Allocation,
locks, work scheduling, clocks, DRM lifetime and USB responses require userspace
models; those models do not prove physical HDMI presentation or kernel FPU/context
correctness.

`make tests` runs these Linux fixtures and the Android host suite. `make
linux-tests` includes the SIMD dispatch/filter/tolerance checks (`make test-simd`)
and capability/EDID checks (`make test-capabilities`). `make android-tests`
runs the Android host subset. Host tests need a C compiler, Python, a JDK and
the locked Rust dependencies already available to Cargo; they do not need an
Android SDK. Device instrumentation is a separate `make android-test-device
DEVICE_SERIAL=<adb-serial>` target and needs `ANDROID_HOME` or `ANDROID_SDK_ROOT`.
The opt-in UYVY route benchmark is not included in correctness results.
Historical SIMD result documents retain the commands and assumptions of their
original revisions.

```sh
make -j"$(nproc)" tests
make -j"$(nproc)"
python3 tools/test_uyvy_pipeline.py --sanitize
python3 tools/test_damage.py --sanitize
python3 tools/test_uyvy_pipeline.py --arm
python3 tools/simd_bench/run.py --arm
python3 tools/test_downscale_5to4.py --arm32
tools/android/test_frame_pipeline.sh
```

Linux sources and headers are under `linux/`; fixture runners compile or extract
from that directory and include the common root headers separately. Per-adapter
sysfs fixtures exercise explicit values and default readback, rejected input
(including `inherit`), missing-device failures, and generation/hotplug isolation. The USB
`dev_groups` registration lets the driver core publish attributes after probe
and drain callbacks before disconnect tears down buffers.

The capability fixture checks all 128 addressable EDID blocks with lengths
0, 1, 127 and 128, including register `0xffff`. Each byte-read failure is
injected separately; the fixture checks error propagation, the untouched unread
suffix, guard bytes, successful retry and rejection before I/O for overflowing
addresses or lengths. The callback simulates register responses only.

`make test-udev` uses udevadm's real rule engine with synthetic sysfs mounted by
bubblewrap. Only the architecture input is replaced. Its cases cover x86/ARM
presets, link speeds, event/device guards, later local overrides and an untouched
peer. The host filesystem is read-only and the live sysfs tree is hidden. This
validates rule evaluation, not a physical bind event or the kernel store callback.
