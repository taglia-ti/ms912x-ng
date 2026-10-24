# Driver test targets

Correctness runners compile current production translation units or extract
complete function definitions from the current checkout without replacing their
bodies. Independent oracles generate expected results; they are not presented as
implementations of the driver. Tests must not keep a production alias alive solely
for a fixture. A fixture adapter may supply arguments to an active API, but must
not implement conversion, scanning, filtering or serialization itself.

| Coverage | Current production entry | Driver caller |
| --- | --- | --- |
| DRM RGBX/BGRX conversion, all RGB triples and guards | `ms912x_convert_rgb_to_uyvy` | `ms912x_transfer.c` |
| Android image bounds, stride, crop and conversion | `ms912x_rgbx_prepare` / `ms912x_rgbx_execute` | Android `frame_pipeline.c` |
| UYVY backend selection and comparison | `ms912x_core_scan_rows` | `ms912x_damage.c`, shared `ms912x_scan_core.c` |
| Damage runs, sampling and margins | `ms912x_damage_scan_process_row` / `ms912x_damage_scan_extend_bottom` | Linux damage worker |
| Shared scan geometry and packet encoding | `ms912x_core_scan_words_backend` / `ms912x_core_mode3_encode_rectangles` | Android shared damage pipeline |
| Batch compaction and replay | `ms912x_rects_compact_indexed` / `ms912x_batch_build` | Linux transfer and Android shared damage pipeline |
| 3:2 and 5:4 crop/filter behavior | Production `*_region` entries | Capture and scale dispatchers |
| Packet staging, cursor and transfer completion | Production packer, compositor and complete USB worker | Linux output pipeline |

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

`make tests` no longer builds a historical Git baseline. The opt-in UYVY route
benchmark is an experiment and is not included in correctness results. Historical
SIMD result documents retain the commands and assumptions of their original
revisions.

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
