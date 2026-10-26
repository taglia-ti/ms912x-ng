# Documentation index

Start with the [Linux driver README](../README.md), the
[Android README](../android/README.md), or the [phone test guide](../android/TESTING.md)
for current build and user workflows. Dated validation logs and vendor reviews
below record their own baselines; their test counts are historical snapshots,
not promises about the current checkout.

## Adapter summary

[Hardware capabilities](../HARDWARE_CAPABILITIES.md) and
[adapter quirks](../HARDWARE_QUIRKS.md) are the concise hardware references.
Their main findings are:

- Recovered vendor paths send RGB565, RGB888 or UYVY pixels over USB. Input
  dimensions and the selected output timing are separate; a format constant or
  a field width alone does not establish a supported operating mode.
- Vendor tables describe 30 distinct adapter VICs, including a 1920×1200 output
  raster. The current Linux catalog uses 29 entries. Actual modes depend on the
  adapter, output port, SDRAM and monitor.
- Mode 0 sends full images; mode 3 sends positioned rectangles. A mode-3 batch
  uses `FF80` after intermediate rectangles and `FFC0` after the last. Each
  rectangle adds 16 framing bytes; no fixed on-wire rectangle count is known.
- Retained-image updates can briefly restore stale pixels. Vendor paths repair
  this by replaying the previous update's damage with the latest pixels. The
  exact internal page mechanism remains unverified.
- Scaling is configured for the complete input image, and mode 3 can update
  portions of it. Corruption above a 1280-pixel scaled input was reported on a
  tested setup; a family-wide limit and arbitrary scale ratios are not proven.
- SDRAM and bypass support vary by adapter. One observed SDRAM-less mode-5
  configuration uploads a complete image; sparse bypass updates are not
  established. Some variants expose audio as a separate USB Audio function.

The research reports distinguish observed vendor behavior, static inference,
driver behavior and physical results. Local `drivers/` vendor packages and
extracted evidence are ignored by Git; links to them work only when those local
artifacts have been supplied or reproduced.

## Current implementation and tests

- [Linux test targets and fixture boundaries](testing.md) and
  [multi-adapter measurements](multi-adapter-measurement.md).
- [Android/Linux shared pipeline](android-linux-reuse.md),
  [Android fixture suites](../android/tests/README.md), and
  [UYVY scan, compaction and benchmarks](uyvy-first-pipeline.md).
- [Independent Android screen](android-created-screen.md),
  [touchpad](android-touchpad.md), and
  [DeX mouse-sharing experiment](android-dex-topology-experiment.md); Android
  framework and OEM compatibility, protected content, and physical presentation
  remain subject to the limits recorded in those documents.

## Hardware and vendor research

- [Input geometry and VIDEO_IN limits](video-in-limits.md),
  [output timings and VICs](video-out-modes.md), and
  [transport-mode mixing and scaling](transport-mixing-and-scaling.md).
- [Mode-3 batches](mode3-multi-rectangle-batching.md) and
  [partial-update ordering and replay](partial-update-ordering.md).
- [Multiple-output vendor evidence](multi-output-vendor-analysis.md) and
  [MS9132 Linux source hints](ms9132-vendor-linux-analysis.md). The latter is
  untrusted source material, not proof of hardware capabilities.
- Static comparisons of the supplied [Android driver](android-driver-analysis.md),
  [macOS and Android](macos-vs-android-analysis.md), and
  [Windows, Android and macOS](windows-vs-android-macos-analysis.md).
- The 2026-10-22 [vendor package review](vendor-drivers-review-2026-10-22.md),
  [Linux package review](vendor-linux-packages-review-2026-10-22.md), and
  [version comparison](vendor-driver-version-comparison.md).

The reports give artifact identities and detailed reproduction steps. Local
extraction entry points include `tools/android/analyze-apk.sh`,
`tools/macos/analyze-dmg.sh`, `tools/windows/analyze-installer.sh`,
`tools/extract-partial-update-evidence.py`, and
`tools/extract-video-out-timings.py`.

## Android history and implementation

- [Implementation review, 2026-10-25](android-review-2026-10-25.md),
  [conversion and ownership review](android-conversion-review.md), and the
  dated [validation log](android-validation.md).
- [Display permission research](android-display-research.md) and the earlier
  [app-owned virtual-display experiment](android-virtual-display-test.md).
- Shell-helper checkpoints: [P0 capture](android-shell-capture-p0.md),
  [P1 geometry](android-shell-capture-p1.md),
  [P2 control and cleanup](android-shell-capture-p2.md),
  [P3 service-owned output](android-shell-capture-p3.md),
  [P4 embedded ADB](android-shell-capture-p4.md), and
  [P5 independent screen](android-created-screen.md). Earlier phase documents
  retain the workflow and limitations as they stood at that checkpoint.
