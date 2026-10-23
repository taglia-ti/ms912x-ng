# Vendor protocol analysis

Run the reproduction commands below from the repository root.

[Multiple outputs: vendor evidence](multi-output-vendor-analysis.md) distinguishes
the Windows application's three-adapter limit from independent outputs inside
one device, with Windows/macOS addresses and the observed HDMI/VGA topology.

[Multiple USB adapters: support plan](multi-adapter-support-plan.md) records the
existing per-device foundation, shared automatic-profile coupling, and phased
configuration, lifecycle, diagnostics, and hardware validation work.

RGB565 primary plane, RGB565 USB output, shared scan code, scaling and the
desktop drivers' pixel-byte substitutions.

[Adapter capabilities](../HARDWARE_CAPABILITIES.md) summarizes pixel formats,
input/output configuration, scaling, transfer modes, multi-rectangle batches,
SDRAM/bypass operation, and display controls.

[Adapter quirks](../HARDWARE_QUIRKS.md) summarizes vendor stale-page replay,
the Linux driver's separate-rectangle batching policy, batch boundaries,
geometry, scaling, and recovery.

[VIDEO_IN and VIDEO_OUT limits](video-in-limits.md) traces when Windows
and macOS program stream geometry, their size/alignment and format policies,
and the distinction between caller restrictions and hardware limits.

[Android driver analysis](android-driver-analysis.md) documents
the commands, register map, frame transfer paths, and differences from this Linux
driver recovered from the supplied MSDisplay 2.0.0.3 APK. To reproduce the local
extraction, Java decompilation, and native disassembly with installed tools, run
`bash tools/android/analyze-apk.sh`. This performs static analysis without USB
device access; the report distinguishes recovered code from hardware validation.

[macOS versus Android analysis](macos-vs-android-analysis.md)
compares the supplied macOS binary with the APK, including manual rectangle
updates, FF80/FFC0 markers, color conversion, and control differences. Reproduce
its static extraction with `bash tools/macos/analyze-dmg.sh`.

[Windows versus Android/macOS analysis](windows-vs-android-macos-analysis.md)
covers both executables embedded in the Windows installer, USB commands,
rectangle framing, EDID reads, hotplug, and the virtual display drivers.
Reproduce the static extraction with `bash tools/windows/analyze-installer.sh`
(requires `innoextract`, LLVM, Python 3 and `pefile`).

[MS9132 Linux source hints](ms9132-vendor-linux-analysis.md)
compares the supplied MacroSilicon-attributed source with this driver,
including whole-frame transfers, frame triggers, chip-specific controls,
EDID, audio separation, implementation defects, and a current-kernel build check.
This source is untrusted: use it as research hints, not hardware capability evidence.

[Transfer-mode mixing and adapter scaling](transport-mixing-and-scaling.md)
traces a macOS diagnostic that alternates modes 3/0, smaller USB images with
larger output timings, Android's 720p-input/1080p-output configuration, and
the implications for reducing USB bandwidth.

[Partial-update ordering and stale image history](partial-update-ordering.md)
traces vendor previous-damage replay in both Windows versions and macOS, then
documents the Linux driver's separate-rectangle replay batching policy. It also
covers alignment, initialization repeats, periodic repair, and why the Windows
mode-5 path sends full scaled images. Reproduce the focused excerpts with
`python tools/extract-partial-update-evidence.py` after vendor extraction.

[Vendor VIDEO_OUT timing inventory](video-out-modes.md) catalogs 30 adapter
VICs across all supplied drivers, including Android's 1920x1200 timings and
Windows' larger virtual-monitor descriptor tables. Reproduce the numeric
inventory with `python tools/extract-video-out-timings.py` after vendor extraction.

[Mode-3 multi-rectangle batching](mode3-multi-rectangle-batching.md)
checks Windows first, then macOS and Android: rectangle-count evidence,
aggregate byte budgets, FF80/FFC0 boundaries, and Windows' 64 KiB USB
segmentation. Its excerpts use the same partial-update evidence extractor.
