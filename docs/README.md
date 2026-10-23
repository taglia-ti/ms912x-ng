# Vendor protocol analysis

Run the reproduction commands below from the repository root.

[Vendor version changes and relevance to ms912x](vendor-driver-version-comparison.md)
compares old and new implementations against our current driver, separating
existing mechanisms from useful ZLP/repair experiments and future bypass or
compressed-transport work.

[New vendor driver review (2026-10-22)](vendor-drivers-review-2026-10-22.md)
compares the newly supplied Windows, macOS, Android, and Linux packages, including
chip-dependent Android frame completion, the newer macOS zero-length bulk
transfer, Windows mode-4 bypass setup, and the separate 9133 JPEG driver path.

[Linux framebuffer and DRM package review](vendor-linux-packages-review-2026-10-22.md)
compares the fbdev whole-frame mode-0 sender with the newer DRM mode-3 UYVY
rectangle sender, including damage retention, cursor composition, ZLPs,
application interfaces, and source-level recovery/lifetime concerns.

[Multiple outputs: vendor evidence](multi-output-vendor-analysis.md) distinguishes
the Windows application's three-adapter limit from independent outputs inside
one device, with Windows/macOS addresses and the observed HDMI/VGA topology.

[Multiple USB adapters: support plan](multi-adapter-support-plan.md) records the
existing per-device foundation, shared automatic-profile coupling, and phased
configuration, lifecycle, diagnostics, and hardware validation work.

[RGB565 DRM implementation plan](rgb565-drm-plan.md) covers a preferred native
RGB565 primary plane, RGB565 USB output, shared scan code, scaling and the
desktop drivers' pixel-byte substitutions.

[Android virtual-screen and USB-output plan](android-driver-plan.md) covers an
Android 11+ APK with on-device wireless ADB pairing, a shell display/input
helper, a Linux-derived C core through JNI, explicit IPC/buffer ownership,
and implementation phases with acceptance tests.

[Android implementation](../android/README.md) tracks the new isolated APK
subproject, wireless ADB discovery, shared Linux pixel/mode cores, EDID-filtered
output selectors, and native capture/USB submission.
[Phone test instructions](../android/TESTING.md) describe the installable APK
and screen/monitor test flow. Physical 640×480 mirroring and app-owned test-scene
output are verified; the shell display helper remains an open gate. See
[Android validation](android-validation.md) for the tested device and limitations.

[Shell-assisted virtual-display test](android-virtual-display-test.md) records
1080p output, independent app pixels and pointer/key receipt on an APK-created
screen using PC ADB for launch/input, with API 30 and physical-phone evidence.

[Android display permissions: source investigation](android-display-research.md)
records the vendor APK's mirroring path and scrcpy's shell execution model.

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

[Android conversion and ownership review](android-conversion-review.md)
documents reusable RGBX/BGRX scalar/SSE2/NEON conversion, submission completion,
thread lifetime, and the differential, sanitizer, and production JNI tests.

[Android fixture suites](../android/tests/README.md) maps each implemented layer
to native, USB/EDID, discovery, credential, JNI, activity/service, and API 30/36
capture tests, with reproducible runners and explicit hardware acceptance gaps.
