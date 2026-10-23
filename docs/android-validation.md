# Android driver validation log

Date: 2026-10-23. Scope: APK, native/Android fixtures, API 30/36 capture,
and one physical API 37 phone/USB/HDMI combination.

## Shared Linux sources and Android 7 mirror build (0.10.0)

- Linux `main` through `f4181bb` takes precedence; merge `ba80411` brings its
  UYVY-first changes and RGB565 removal into the Android branch. The old stash
  remains as backup. See [the source and support map](android-linux-reuse.md).
- Android calls the production Linux batch planner, run detector, mode-3
  serializer, timing catalog and EDID repair. Its duplicate catalog, packet
  rectangle packing and replay sorting are removed. Kernel/Android allocation,
  capture and USB ownership remain platform-specific adapters.
- Version code 10 builds as a desktop API 30 APK or an API 24 mirror APK with
  `-PmirrorOnly=true`. The latter packages ARM32/ARM64/x86/x86_64 capture
  libraries and no Rust/ADB library. Both target API 36 and pass API lint.
- All 14 desktop groups pass on the API 37 phone. The mirror groups pass on the
  phone and API 24 x86 emulator. Android 7 also passed real system consent and
  MediaProjection capture: a checkpoint reports 166 accepted native images,
  zero rejected, and Stop releases the session. An initial presentation-based
  fixture required an Activity window token on that OS; the mirror fixture now
  tests its actual ImageReader path directly. API 24 physical USB/OTG, ARM32
  hardware and Android 8 runtime are not established by these checks.
- Linux `make tests`, `make`, and Android host/ARM64 fixtures pass. Android host
  fixtures also pass UBSan. A GCC 16 optimizer crash in the combined fixture was
  avoided by compiling the common serializer as a regular shared source rather
  than inlining its full implementation into every including source.
- Physical phone/adapter 1080p60 evidence is in
  `android/app/build/reports/phone-sm-s928b/`. The region-specific HDMI check
  passes 599 samples in the final build without marker errors/static-bar changes or excluded
  samples. Card sampling is not fresh Android frame rate. The persistent
  UltraSemi capture identity excludes the KVM webcam.
- Installable APKs, ABI/package metadata, signatures/alignment checks and hashes
  are exported in `android/app/build/outputs/compat/`. API 24 and API 37 fixture
  evidence lives in `android/app/build/reports/android-fixtures/`.

## 1080p and shell-assisted independent apps (0.9.0)

- On the same API 37 phone/adapter, the APK creates an actual app-owned
  1920×1080 virtual display and sends it through USB at the selected 1080p60
  timing. Both a final-build 10-second recording and earlier 1080p60 recordings
  pass the generalized HDMI checker: 599 samples each, no marker errors or
  static-bar ROI changes, and no samples excluded. Card cadence is not display
  FPS or fresh Android-frame rate. The earlier 1080p50 recording has one malformed
  card-startup buffer after changing capture format; its raw failure is retained,
  and the other 299 samples pass with that one exclusion explicitly reported.
- Added an Advanced diagnostic to dismiss/restore only the test Presentation,
  preserving the same virtual display, ImageReader, native and USB owners.
  The screen ID is shown for PC launch. Stop disables the scene control and
  clears the active-screen diagnostic. Existing activity fixtures verify these
  ownership and cleanup properties.
- With that scene hidden, PC ADB launches Settings and a standalone androidTest
  activity onto the APK-owned screen. The phone keeps displaying driver controls
  while HDMI shows independent content. Driver UID 10464 and fixture UID 10465
  differ. The fixture's pixels, live counter, positive pointer count and
  `KEYCODE_A` appear through HDMI at 1920×1080. The normal APK's launch probe was
  denied; shell launch supplies the missing privilege. This path uses neither
  MediaProjection nor ADB pixel traffic and reuses the current native pipeline.
- The same shell-assisted fixture works on the API 30 emulator at the 640×480
  capture-only fallback. Driver/test UIDs are 10130/10131. Activity state confirms
  resumed/drawn content on display 4; accessibility reports
  `UID=10131; display=4; pointer=1; key=KEYCODE_A`. A checkpoint has 190 captured
  images and 0 rejected offers. Physical Android 11 USB output is not established.
- Startup input delivery needs additional work: a first tap was sometimes lost,
  including immediately after `am start -W`. Subsequent taps and keys were
  delivered. Final phone accessibility and HDMI evidence confirm a positive
  pointer count after two tap attempts. No reliable first-tap/focus handshake is
  claimed; the future privileged launcher must implement input readiness.
- Consented phone-screen mirroring also works through physical HDMI at 1080p60,
  correctly letterboxed. At one checkpoint: 182 accepted native copies, 0
  rejected, 305 completed USB updates. These are cumulative counters.
- All 14 Android/JNI groups pass on both API 30 and the API 37 phone against the
  final APK. Assembly and instrumentation assembly pass; lint reports 0 errors
  and 49 warnings. Both exported APK signatures and 16 KiB ZIP alignment verify.
  All four packaged native libraries are byte-identical to 0.8.0; no Linux or
  native C sources changed in this checkpoint, so its prior native/sanitizer
  evidence remains applicable without a new native-performance claim.
- Final driver APK: version code 9, `0.9.0-display-test`, 6,893,548 bytes,
  SHA256 `8166e7623465631a3a75578f9726336ec6b265a4428f554338668205248c58a1`.
  Separate test-activity APK: 137,734 bytes,
  SHA256 `17e451cefa11e38fbce944bcace809d7cceda784253d88179b42578af815fe8d`.
  Both are exported under `android/app/build/outputs/phone-test/`.
- Both installed phone APKs were pulled back and match the exports. Sharing is
  stopped, the owned/capture displays and service are gone, and the inspected
  crash buffer has no driver/test-app crash. The API 30 emulator was stopped.
- The launch screen now centers on screen selection, monitor timing and Share.
  The gear menu opens Settings and Debug; profile, stride and tolerance are
  reserved on Settings, while all capture/ADB/test-scene controls are on Debug.
  Navigation, Back handling, placeholder content and test-control placement
  passed on API 30 and the API 37 phone. The activity fixture also selects
  1920×1080@60, verifies that the app-owned VirtualDisplay and ImageReader use
  1920×1080, and sends captured frames through a fake USB host. All 14
  instrumentation groups pass on both; lint has 0 errors and 50 warnings. The
  fake host means this run did not claim another physical HDMI test. The debug
  APK is installed on the phone; the fixture stops its virtual display and USB
  worker before finishing.
- Phone evidence remains under `android/app/build/reports/phone-sm-s928b/`;
  API 30 independent-app evidence is under `android/app/build/reports/api30-independent/`.
  See [the virtual-display experiment](android-virtual-display-test.md) for exact
  reproduction, screenshot/recording names, input limitations and cleanup.
- This is a PC-assisted feasibility test. Built-in wireless pairing/TLS/shell
  startup, trusted/other-display capture, general app compatibility and a usable
  standalone desktop remain open. The experiment supports retaining APK-owned
  display/capture and adding privileged launch/input through the embedded bridge.

## Physical phone, damage replay and shared EDID repair (0.8.0)

- Device: Samsung SM-S928B, Android 17/API 37, ARM64. Adapter: `534d:6021`,
  HID control interface 0, vendor video interface 3, bulk OUT `04`. Monitor EDID
  reports `27G50F` and 14 timings matching the shared adapter catalog. Physical
  output in this checkpoint is limited to 640×480 at the adapter's 60 Hz timing.
- The USB session now owns both video and interface-recipient control claims.
  Only the validated non-boot control HID uses forced claiming. Partial claim
  failures and release exceptions clean up the remaining owned resources.
  Audio and driver-storage interfaces are not claimed.
- Added reusable C current/previous damage replay. Batches contain current
  damage plus the previous successful frame's current-only damage, with latest
  pixels. Successful transfer commits transmitted coverage; failure/reset forces
  full repair. History does not accumulate outgoing unions. One idle repair
  drains history without requiring a new captured image. Producer sequences and
  submission tokens are separate, and packet-budget exhaustion falls back to
  full-frame coverage. Independent packet-decoding fixtures check latest payload,
  overlap, bounded A/A+B/B+C history, trailers, idle drain and failed submissions.
- After Stop, an earlier build returned base EDID checksum 1. Extracted the
  existing Linux connector's narrowly guarded second-header-byte repair into
  `shared/ms912x_edid_core.h`; Linux and Android JNI call that same function.
  It repairs only `00 00 FF FF FF FF FF 00` when changing byte 1 to FF restores
  the complete block checksum. Additional corruption remains an error. Version
  0.8.0 read the 14 modes after both mirror and test-scene Stop without an unplug.
  This demonstrates successful rereads; the raw physical faulty block was not
  saved, so the exact on-device corruption pattern is not independently proven.
- Consented phone-screen mirroring reached physical HDMI; the portrait phone
  screen was correctly letterboxed into the selected 640×480 output. Capture
  continued during app scrolling, with 169 accepted native copies and 0 rejected
  at a recorded checkpoint. Diagnostics are coalesced once per second to avoid
  per-frame visible status text feeding capture back into itself. Captured images
  remain unpaced. Stop released the capture service/display and unlocked controls.
- The app-owned scene restarted without reconnecting. At a later checkpoint it
  had 118 captured frames, 0 rejected offers and 223 completed USB updates. These
  are cumulative counters, not display FPS. The final 10-second HDMI recording
  contains 599 card samples. `tools/android/check_hdmi_pattern.py` found 0 marker
  errors, 0 static-bar errors and maximum static-bar changed fraction 0; the cyan
  marker visited every position, including wraparound. The earlier 0.7.0 recording
  also passes this check. Representative images show correct bars, counter and
  borders. This checks the specified regions, not every pixel or all modes.
- Capture uses the persistent UltraSemi card path
  `/dev/v4l/by-id/usb-UltraSemi_USB3_Video_20210623-video-index0`, verified as
  `345f:2131`, serial `20210623`. The KVM Logitech webcam is excluded. Capture-card
  60 Hz sample cadence is not the rate of fresh Android images or USB updates.
- All 14 production-JNI/Android instrumentation groups pass on both API 30
  x86_64 and the API 37 phone, including all 29 mode-command fixtures, control
  ownership/cleanup, strict EDID repair and capture/service lifecycle. Native
  host and ARM64 QEMU pipeline/submission/boundary/fault fixtures pass. Real ARM64
  non-sanitized fixtures pass, including 3,024 NEON differential conversion cases.
  API 30 ASan/UBSan passes conversion and all four native frame fixtures; host
  UBSan passes. Full phone ASan is incomplete: a minimal standalone pthread
  program without project code also fails with SIGILL on this phone. Conversion
  and the initial pipeline sanitizer fixture passed before that runtime failure.
- APK/instrumentation assembly, lint (0 errors, 40 warnings), signature and
  16 KiB ZIP alignment pass. `make -j4` and `make test-capabilities` pass. Of the
  87 pre-review existing Linux/build/shared source fingerprints, only
  `ms912x_connector.c` changes for the common EDID-helper extraction. Existing
  pending Linux changes are otherwise preserved; no host module was reloaded.
- Debug-signed ARM64/x86_64 APK: version code 8, `0.8.0-phone-test`, 6,785,748
  bytes, SHA256 `36cece6bfa70df502e6335b152ea0694499e5bf3facdf42faa7fc3b781a0f847`.
  Export: `android/app/build/outputs/phone-test/ms912x-desktop-0.8.0-phone-test.apk`.
  The phone's installed APK was pulled back and its hash matches. The app is left
  installed with sharing stopped, no capture display/service and no app crash in
  the inspected crash buffer.
- Evidence: `android/app/build/reports/phone-sm-s928b/`, including
  `instrumentation-final-0.8.0.txt`, `api30-final-0.8.0.txt`,
  `api30-0.8.0-asan.txt`, `38-phone-mirror-640x480.mkv`,
  `40-phone-mirror-hdmi.png`, `44-final-pattern-640x480.mkv`,
  `45-final-hdmi-analysis.json`, `46-final-hdmi.png` and final cleanup dumps.
- The newer vendor APK defines a driver-storage disable write to XDATA `0x7F00`,
  but static inspection found no direct call from its startup path. We did not
  execute that command or run the vendor APK. See
  [the storage-helper investigation](android-driver-analysis.md).
  Physical results above do not prove that storage explains every USB reenum.
- Remaining gates: other physical modes/phones, real on-device Wireless ADB
  pairing/TLS/shell, helper transfer/launch and independent third-party desktop.
  The app-owned new screen is still a test scene, not a desktop hosting other apps.

## Capture frame-rate cap removed (0.5.0)

- Removed the capture callback's 100 ms gate. Each acquired image is offered
  immediately to native conversion, with ImageReader latest-image acquisition
  and the bounded native mailbox still handling backpressure. Monitor refresh
  selection is independent of capture submission. No physical-phone frame-rate
  or monitor presentation measurement is claimed.
- Added an ImageReader/JNI regression that posts three consecutive source
  frames without a pacing timer and requires all three to be offered. On API
  30 it failed with the old gate (13 groups passed, 1 failed), then passed
  after removal. All 14 instrumentation groups passed with the updated APK on
  both API 30 and API 36. Existing consent UI reports below describe 0.4.0;
  that separate UI suite was not rerun for this callback change.
- APK assembly, instrumentation assembly, signature verification and 16 KiB
  ZIP alignment checks passed. Lint completed with 0 errors and 40 warnings.
  All four packaged native libraries are byte-identical to 0.4.0. The 87
  existing Linux/build/shared source fingerprints remain unchanged.
- Updated diagnostics, current build instructions and the implementation plan.
  The debug-signed ARM64/x86_64 APK is version code 5,
  `0.5.0-uncapped-test`, 6,783,988 bytes, SHA256
  `45824d705151c2bec5bd13c9a10b4f1f46094496d8e795f49cd85274369587e9`.
  It is exported under `android/app/build/outputs/phone-test/`. Regression
  output and per-API results are under
  `android/app/build/reports/android-uncapped/`.

## Android fixture suites (0.4.0)

The fixture inventory, code-path map, commands, and runtime acceptance gaps are
in [android/tests/README.md](../android/tests/README.md). The reusable USB and
NSD boundaries use framework bindings in the application and fake responses
only in androidTest. No extra test-library dependency was introduced.

- All 14 instrumentation groups passed on both API 30 and API 36 against the
  final production JNI libraries. Groups include all 29 adapter modes, exact
  control-report bytes, control-transfer faults at every setup position,
  malformed descriptors/EDID, permission callbacks, output cancellation/detach,
  discovery callbacks, Keystore storage, Java/Rust validation, ImageReader and
  presentation capture, activity controls and invalid-consent service lifecycle.
- Final packaged-APK UI checks passed: 11 on API 30 and 13 on API 36.
  Both cover new-screen and mirror capture, consent cancellation, background/
  resume, restart and display/service cleanup. API 36 additionally captures
  MS912x Desktop itself (4 accepted frames) and Settings (12 accepted frames)
  through the system app-window picker. Every capture checkpoint reported zero
  rejected offers; final cleanup found no capture display, service or app crash.
  Each UI run pulled the installed APK and verified its hash against the export.
- Host native, Java, and Rust checks passed. RGBX conversion still matches the
  scalar formula in 3,024 x86_64/SSE2 cases and 3,024 ARM64/NEON cases under QEMU.
  All four native mailbox/submission/boundary/fault fixtures also passed on
  both native x86_64 and ARM64 QEMU. Rust runtime fixtures cover success, error,
  timeout and panic containment. EDID fixtures cover established, standard,
  detailed and CTA formats and malformed blocks.
- The API 30 ASan/UBSan run passed conversion and all four native frame fixtures.
  Fault injection rejects each of 7 create allocations, 6 replacement
  allocations and 2 mutex initializations; failed replacement preserves a usable
  original session and permits a successful retry. Host UBSan also passed.
- Fixtures prompted fixes for deadlines during USB short writes, impossible
  bulk byte counts, interrupt handling, connection cleanup when claim throws,
  pending Java exceptions at the Rust/JNI boundary, null credential stores,
  and late/throwing discovery callbacks. App-window testing also revealed a
  duplicate launcher activity when the selected app is MS912x Desktop itself;
  the activity now discards that duplicate while retaining the consent owner.
- The debug-signed ARM64/x86_64 APK is version code 4, `0.4.0-fixtures-test`,
  10,278,296 bytes, SHA256
  `bf1d2da5688208f01aa36dc079943d1b09326dba089421ebb8d1add21c82fe3d`.
  APK assembly, instrumentation assembly, lint, signature and 16 KiB ZIP
  alignment checks pass. Existing Linux/build/shared sources were preserved.
- Reports are under `android/app/build/reports/android-fixtures/`. Fixture USB
  responses do not establish physical adapter/monitor acceptance. Successful
  Wireless debugging pairing/TLS/shell and the independent desktop helper
  remain separate acceptance gates; this suite is not a 100% branch-coverage
  claim.

## RGBX conversion and ownership review (0.3.0)

Implemented direct RGBX/BGRX-to-UYVY conversion with inline scalar, SSE2, and
ARM64 NEON workers. Conversion, completed-frame damage tracking, mailbox
ownership, JNI lifetime, and USB completion now have separate contracts. See
[the architecture review](android-conversion-review.md) for details and commands.

- Final APK: version code 3, `0.3.0-rgbx-test`, 6,797,328 bytes, SHA256
  `89caa56c34d480887fdc45d437f9fd1d5bed64faf9f40c1384de11389cb9e0bc`.
  The installed API 30 package was pulled back and its hash matched the export.
  APK assembly, instrumentation APK assembly, and lint passed. Debug signature
  verification, 16 KiB ZIP alignment, and native LOAD alignment passed for
  both ABIs. Capture library exports retain JNI entry points and hide private C
  APIs. Disassembly confirms NEON LD4/ST2/widening MACs and SSE2 packed MACs.
- 3,024 differential conversion cases passed on native x86_64/SSE2 and ARM64
  NEON under QEMU, matching the existing shared scalar formula. Cropping,
  padding, unaligned input/output, unusual pixel strides, alpha independence,
  vector tails, guard pages, invalid views, overlap, and arithmetic overflow
  checks passed. Host UBSan passed.
- The final conversion, pipeline, and submission fixtures passed ASan/UBSan
  inside the API 30 x86_64 guest. Android leak detection was disabled. Host
  ASan could not reserve its shadow mapping under this host's strict
  overcommit policy; the Android sanitizer run covers the final C workers.
- Native submission tests passed for whole/partial packets, successful commit,
  unchanged-frame suppression, failed-transfer full repair, stale/duplicate
  tokens, invalidation during a pending batch, insufficient packet capacity,
  reconfiguration, and 6,000 concurrent producer offers. Java ownership tests
  passed, including an injected transfer exception. EDID filtering still
  passed for all 29 catalog modes.
- Instrumentation passed against the packaged production JNI library on API 30:
  backend selection, packet sizing, full/partial updates, token completion,
  failed-transfer repair, independent handles, and concurrent close.
- All 11 API 30 capture/UI checks passed on the final APK. New-screen capture
  accepted 5 frames with 0 rejected offers, continued to 12 after background/
  resume, stopped without a leaked display, and restarted with 6 accepted.
  Consent cancellation restored controls. Screen mirroring accepted 7 frames,
  continued to 20 after background/resume, and restarted with 7 accepted;
  all mirror sessions had 0 rejected offers. Final cleanup found no capture
  display, service, or application crash. Capture logs confirm the SSE2 backend.
- Reports are under `android/app/build/reports/rgbx-review/`; UI screenshots,
  dumps and all 11 results are under `android/app/build/reports/api30-rgbx-retest/`.
  The AVD was stopped afterward. No physical Android USB transfer or monitor
  output was tested, and no real ARM phone performance is claimed.
- All 87 existing Linux/build/shared source fingerprints recorded at review
  start remained identical. Existing pending Linux changes were preserved.

Host-only conversion benchmark, AMD Ryzen 9 5900XT, GCC 16.2.1, `-O3`, emulator
stopped and no concurrent build: prepared packed RGBX conversion to UYVY,
seven samples per case, median per frame. Each sample performs 30 conversions
for the larger sizes and 20,000 for 6x64. Source validation/dispatch preparation
is outside timing; execution includes destination checks and row dispatch.

| Frame | Scalar ms | SSE2 ms | Scalar Mpixel/s | SSE2 Mpixel/s |
|---|---:|---:|---:|---:|
| 640x480 | 0.657013 | 0.150055 | 467.571 | 2047.248 |
| 1366x768 | 2.260188 | 0.537620 | 464.160 | 1951.357 |
| 1920x1080 | 4.521860 | 1.055396 | 458.572 | 1964.761 |
| 6x64 | 0.000880 | 0.000375 | 436.454 | 1023.704 |

These measurements exclude ImageReader capture, mailbox copies, damage scanning,
packet encoding, USB, and presentation. QEMU was used only for ARM64 correctness.
They do not establish phone conversion speed or end-to-end display FPS.

## API 30 packaged APK retest

Retested the exported 0.2.0-phone-test APK on the Android 11/API 30 x86_64
AOSP emulator. The installed APK SHA256 matched the exported package:
8214e3ca43f46da4e4bf46ece2b43f5a3655606d0a1c98d512abc23351ce1dbd.

- Launch, screen dropdown, Android-version controls, and the no-adapter flow
  passed. The dropdown offered the phone screen and Create a new screen;
  manual rotation and unprobed EDID output selectors remained disabled.
- The new app-owned test scene captured at 640×480 with 6 accepted frames and
  0 rejected offers at the checkpoint. Background/resume continued to 13;
  Stop removed the display. Restart accepted 5 frames and stopped cleanly.
- Declining MediaProjection consent restored controls without creating a
  display. Approved phone-screen capture accepted 7 native copies with
  0 rejected offers; background/resume continued to 20. A fresh consent/restart
  session accepted 7 copies. Both stops removed the capture display.
- Final cleanup found no capture display, capture service, or application crash.
  All 11 automated checks passed. Screenshots, display/service dumps, and the
  report are under android/app/build/reports/api30-retest/.
- This was an emulator capture/UI test. No physical USB adapter or monitor
  presentation was tested. No application-code changes were needed.

## Phone-test interface checkpoint

- Replaced radio buttons and informational-only inventory with a live screen
  dropdown, including the default screen, other Android-visible displays,
  the Android 14+ app picker, and Create a new screen. Secondary-display
  capture remains explicitly unavailable without the helper.
- USB probing reads SDRAM/EDID and closes its interface without sending video
  stop/power commands. The resolution and refresh dropdowns contain only the
  EDID/catalog intersection. The selected adapter is retained for the stream;
  detach cancels work and clears its mode list. Probe failures remain visible.
- A new app-owned screen renders color bars, a TOP arrow, a live seconds
  counter, a moving marker, and borders. Android 14+ gets a refresh request;
  Android 16+ gets the selected virtual-display rotation. Earlier OS versions
  show a disabled rotation control and use Android's default orientation.
- The API 36 AVD accepted the 90-degree new-screen request. dumpsys display
  reported a 640×480 backing display with 480×640 logical dimensions and
  rotation 1. The native pipeline accepted 14 frames with 0 rejected offers.
  Stop removed the display. A separate default-screen consent test accepted
  45 native copies with 0 rejected offers; Stop removed its capture display.
- Installed the exported 0.2.0-phone-test build on the API 30 AVD. The rotation
  dropdown was disabled with its Android-version explanation; the no-adapter
  probe retained empty output selectors. Default-screen consent capture ran at
  640×480 and accepted 5 native copies with 0 rejected offers. Stop removed the
  capture display. No AndroidRuntime exception was recorded. Both AVDs were
  stopped after the checks.
- APK assembly and lint pass for ARM64 and x86_64. The APK signature verifies;
  16 KiB ZIP alignment passes and both ARM64 native libraries have 16 KiB LOAD
  alignment. Host EDID filtering tests accept all 29 generated detailed
  timings, reject altered sync/interlaced records, and keep CTA codes distinct
  from adapter protocol mode IDs.
- These checks validate UI, capture, and packaging. No physical Android USB
  control/bulk transfer or monitor presentation was verified in this checkpoint.
- The named APK, SHA256SUMS, and test instructions are exported under
  android/app/build/outputs/phone-test/; the APK is debug-signed, version code 2,
  version name 0.2.0-phone-test, and 6,844,064 bytes.

## Shared modeset, scan, and rectangle streaming checkpoint

- The Android mode selector is populated directly from the shared 29-entry
  Linux timing catalog. Selected width, height, and protocol mode ID flow into
  capture setup and adapter commands. EDID base and declared extension blocks
  are checksum-validated; detailed timings must match the full selected timing,
  and established, standard, and common CTA modes are recognized.
- The capture pipeline converts RGBA pairs to UYVY before scanning. The shared
  scanner compares packed UYVY words, aligns damage to two-pixel boundaries,
  and feeds changed rectangles to the shared mode-3 rectangle encoder. A new
  stream invalidates the baseline to force a full update; subsequent unchanged
  frames submit nothing. USB failures invalidate the baseline before retry.
- The packet buffer reserves the full frame payload plus per-row rectangle
  overhead, so a worst-case row-damage scan fits without truncation.
- tools/android/test_frame_pipeline.sh passes, including scanner bounds, pair
  alignment, unchanged-frame suppression, full-frame initialization, and
  mode-3 packet encoding. make test-capabilities passes with EDID generation
  from the shared catalog. make modules passes for the host kernel.
- ANDROID_HOME and ANDROID_SDK_ROOT were both set to
  /mnt/data/LINUX/android-sdk for :app:assembleDebug; ARM64 and x86_64 passed.
  No USB adapter was attached for this checkpoint, so Android control transfers
  and physical presentation remain unverified.

## Shared packet path checkpoint

- Extracted the mode-3 rectangle header and trailer serialization to
  `shared/ms912x_mode3_core.c`; Linux and Android both use these functions.
- Added a full-frame mode-3 encoder for an owned UYVY snapshot and wired it to
  the Android JNI capture handle. Added Android USB supported-device/interface
  validation and a bulk writer that handles short writes, 16 KiB chunks,
  per-chunk timeouts, and a bounded logical-batch deadline.
- `tools/android/test_frame_pipeline.sh` passes, including exact full-frame
  mode-3 header/payload/trailer bytes and invalid-size checks. `make modules`
  passes with the shared code linked into the Linux driver.
- `:app:assembleDebug` passes for ARM64 and x86_64 using SDK root
  `/mnt/data/LINUX/android-sdk`.
- Installed and launched the updated APK on the API 30 x86_64 AVD. Tapping
  **Connect adapter and send 640×480 test image** followed the expected
  no-adapter path; the AVD reports no supported USB adapter.
- `modetest -M ms912x` confirmed the attached sink advertises 640x480@60 and
  currently runs 1920x1080@50. A host-side test-pattern modeset was attempted
  but denied because the active DRM master owns the device. The API 30 AVD has
  no USB adapter attached, so neither Android control setup nor physical USB
  output has been tested.
- The attached sink's sysfs EDID has a valid 128-byte base-block checksum and
  its established-timing bitmap advertises 640x480@60, matching the first
  Android test mode's EDID gate.
- Tried both emulator passthrough forms supported by the installed emulator:
  `-usb-passthrough vendorid=0x534d,productid=0x6021` and the documented QEMU
  `usb-host` attachment. QEMU opened host `/dev/bus/usb/007/008` (the host
  interface driver changed to `usbfs`), but the API 30 default system image
  does not advertise `android.hardware.usb.host`, exposes no Android `usb`
  service, and has no MacroSilicon VID/PID in guest USB sysfs. The APK cannot
  enumerate or claim the passed-through adapter on this image. Re-test on an
  AOSP guest image with USB host support or a physical Android device.

## Captured-frame USB stream implementation

- The adapter action now accepts an active 640×480 `CaptureController`, opens
  the selected adapter after Android USB permission, validates SDRAM and the
  sink's base-block EDID, applies the Linux-derived 640×480@60 mode-3 setup,
  and serially writes each newly acquired full-frame mode-3 packet. It uses a
  reusable direct packet buffer, the existing 10 Hz capture offer limit, and a
  Stop adapter output control. Partial or failed USB writes terminate the
  stream; USB completion is reported as transport completion, not monitor
  presentation.
- Both the app-owned diagnostic source and the consented MediaProjection source
  now request 640×480 capture geometry to match this first output timing. The
  MediaProjection route is still phone/app-window mirroring. The app-owned
  display remains unable to launch third-party content until the shell helper
  is implemented.
- Fixed a JNI queue-empty result that could previously be interpreted as a
  one-byte packet. The encoder now reports zero when no complete frame is
  pending; the stream sends nothing in that case.
- `ANDROID_HOME=/mnt/data/LINUX/android-sdk ./gradlew :app:assembleDebug`
  passes for ARM64 and x86_64. `tools/android/test_frame_pipeline.sh` passes,
  and `make modules` passes with the shared mode-3 core linked into Linux.
- The API 36 emulator retest still cannot verify USB output: QEMU opened the
  host adapter, but guest `dumpsys usb` reports `host_connected=false`, the
  image does not advertise `android.hardware.usb.host`, and Android's USB
  service exposes no passed-through adapter. The app reported no supported
  adapter. The AVD was stopped afterward; the host driver reclaimed the adapter.
- Installed the stream build on API 36 without USB passthrough and accepted the
  MediaProjection consent flow. Android created the capture virtual display at
  640×480, and the service reported 33 accepted native frame copies with 0
  rejected offers. The updated Stop screen capture control removed the virtual
  display; the AVD was then shut down. This validates capture geometry and
  teardown only, not the USB stream.

This checkpoint verifies compilation and host pipeline behavior only. It does
not establish that a live Android capture frame reaches a physical monitor.

## Host and build

- SDK root: `/mnt/data/LINUX/android-sdk`.
- Installed platforms and images: API 30 and API 36, x86_64 default images.
- NDK: 30.0.16248370. CMake 3.22.1 was installed by the Android Gradle
  Plugin under the same SDK root during the build.
- Host has `/dev/kvm`; `emulator -accel-check` reported KVM usable.
- AVD: `ms912x-api36`, API 36 x86_64. It was configured under
  `/mnt/data/LINUX/android-sdk/avd` and launched with the SDK's emulator binary.
- `:app:assembleDebug` passes for ARM64 and x86_64. The first compile exposed
  the wrong Android constant for `RGBA_8888`; changing to `PixelFormat` fixed
  compilation. Adding x86_64 to the debug ABI set made the APK installable on
  this emulator while retaining ARM64.
- The APK installed and `MainActivity` launched without an app crash.
- An API 30 x86_64 AVD was configured from the installed image. An earlier
  attempt remained `offline`, but a later cold boot completed in about 53
  seconds and reported `sys.boot_completed=1`.

## API 36 display/capture probe

The app's **Create virtual display for capture test** button creates a
720x1280 presentation display using an APK-owned `ImageReader` Surface and the
`OWN_CONTENT_ONLY` flag. The native JNI path accepted captured images and
copied them into its owned frame slots.

Observed on this AOSP emulator:

1. A public/mirroring display request from the normal APK UID failed with a
   `SecurityException`, requiring `ADD_MIRROR_DISPLAY`,
   `CAPTURE_VIDEO_OUTPUT`, `CAPTURE_SECURE_VIDEO_OUTPUT`, or a suitable
   `MediaProjection` token. MediaProjection would mirror the phone display and
   does not satisfy the independent desktop requirement.
2. The normal APK UID was denied when it tried to launch an Activity on its
   virtual display (`Permission Denial` for `launchDisplayId`).
3. A development host ADB shell command, running as UID 2000, launched Android
   Settings on the app-owned display with
   `am start --display <displayId> -a android.settings.SETTINGS`.
4. `dumpsys activity` placed Settings on the virtual display and the app's
   ImageReader/native capture path advanced to 12 accepted frame copies with 0
   rejected offers.
5. Pressing **Stop capture test** released the virtual display; no stale
   `ms912x-local-capture-test` display remained in `dumpsys display`.

This proves the API 36 emulator's app-owned Surface/ImageReader path and shows
why display/app-launch operations need shell identity. The host ADB command is
only a development probe: it does not prove built-in wireless pairing, a
cross-process Binder transfer of the Surface, a helper-created public display,
or operation on a physical phone. Settings is a system app; a user-installed
third-party app has not yet been tested.

## API 30 display/capture probe

The same APK was installed on the API 30 x86_64 AOSP AVD. Its diagnostic
Activity created the same 720x1280 `OWN_CONTENT_ONLY` display and the native
capture path accepted frames. The **Probe app-UID launch permission** button
calls `startActivity()` with `ActivityOptions.setLaunchDisplayId()` from the APK
process itself; Android 11 denied this with `SecurityException` for
`launchDisplayId`. A development shell command then launched Settings on the
same display. `dumpsys activity` showed Settings on display 3, while the APK
reported 10 accepted native copies and 0 rejected offers. Stopping the test
removed the display.

This confirms the public-API capture path and shell-UID boundary on API 30 and
API 36 AOSP emulator images. It does not prove that a helper can transfer the
Surface across Binder, that built-in pairing works, or that an OEM phone grants
compatible display behavior.

## API 30 explicit source selector and MediaProjection

The updated APK shows the current Android display inventory and two explicit
choices: create the app-owned diagnostic display, or capture a screen/window
selected through Android's MediaProjection consent UI. On API 30, the consent
dialog appeared, the user-approved session started the declared
`mediaProjection` foreground service, and the app reported 6 accepted native
frame copies and 0 rejected offers during the short observation. The system
reported a 1080x1920 virtual capture display. Pressing **Stop screen capture**
successfully releases both the projection and the app-owned test display; a
follow-up `dumpsys display` showed no `ms912x-local-capture-test` or
`ms912x-consented-screen` entry.

This validates explicit consented mirroring into the existing native capture
path on the API 30 AOSP emulator. The system picker controls which screen/window
is authorized; the app does not attach to an arbitrary display ID. This route
is a labeled mirror source and does not satisfy the independent desktop gate.
Capture geometry is still diagnostic and fixed from current display metrics;
the adapter modeset/EDID output selector awaits A02.

### API 30 screen-mirror retest

On 2026-10-22, the phone-screen/app-window source was selected and the Android
**Start now** consent action was accepted again. The capture status advanced
from 7 to 9 accepted native copies with 0 rejected offers during a further
observation. `dumpsys display` showed the app-owned
`ms912x-consented-screen` virtual display at 1080x1920 while capture was
active. Pressing **Stop screen capture** changed the app status to
`Screen capture stopped.` and removed that virtual display from
`dumpsys display`.

The accepted-copy counter proves frames reached the app's native capture slots;
it does not prove USB adapter output. Android frame conversion/transfer into
the Linux adapter's USB output path is not implemented yet. The service record
remained visible in `dumpsys activity services` while the activity was open
because the activity is bound to it; the projection and virtual display were
stopped and released.

The native frame pipeline has a host test at
`tools/android/test_frame_pipeline.sh`. It compiles the same C source used by
the APK and verifies cropped/padded RGBA reads, direct RGBA-to-UYVY pair bytes,
newest-pending replacement without modifying an active slot, generation
reconfiguration, invalid source bounds, and odd-width rejection. It also
compiles the shared scan core and checks full-word UYVY differences, padded
word strides, two-pixel rectangle alignment, odd-width rejection, and no
partial results on capacity failure. The Linux scalar row scanner uses the
same shared masked-word comparison primitive. The host test passed with the
system C compiler.

After installing this UYVY build on the API 30 x86_64 AVD, screen-mirror
capture again showed accepted native copies increasing (5 observed) with 0
rejected frames. The virtual display was 1080x1920 while active and disappeared
after Stop. This verifies RGBA-to-UYVY capture-slot acceptance and lifecycle;
the shared damage scanner is not yet connected to the capture/USB worker, and
there is still no adapter output result.

## Current gate

A00 remains open. The APK now embeds the pinned Rust provider, discovers both
Wireless debugging services, accepts a pairing code, encrypts its persistent
host identity with Android Keystore, and implements pairing/TLS plus `id`
shell verification. The API 30 AVD installs and launches this APK, but its
`adb_wifi_enabled` setting is `0` and it advertises no Wireless debugging
services; the integrated handshake has not run. Helper transfer and launch
remain unimplemented. A01 remains open until a helper creates the independent
display, receives the app-owned Surface over Binder, and launches/captures an
exported Activity from another installed app. Repeat the gates with Wireless
debugging enabled and on the user's AOSP phone before claiming Android 11 or
device compatibility.

The latest build packages `libms912x_adb_bridge.so` for `arm64-v8a` and
`x86_64`. On the API 30 x86_64 AVD, `MainActivity` invokes the JNI backend
version on startup and displays `DroidMux 0.1.0 @ 9adeb625`; installation,
native loading, and launch succeeded without a linker error or app crash. The
AVD's `cmd adb is-wifi-supported` returns `true`, but `adb_wifi_enabled` stays
`0` and the AVD does not advertise pairing/connect services. This is a JNI
smoke test only, not a pairing or shell-runtime result.

## Local adbd TCP probe

To distinguish Wireless debugging from a local TCP path, the debug APK gained a
probe that connects through DroidMux to port 5555 on an IPv4 address assigned
to the AVD, then runs `id`. I temporarily enabled `adb tcpip 5555` from the
host. The host ADB client could connect to `127.0.0.1:5555` through the
emulator's QEMU port mapping, but the APK's direct connections to guest
loopback and its guest interface (`10.0.2.15:5555`) were refused. The AVD does
not expose that host-mapped listener inside its own network namespace. I
restored the AVD to its prior USB/emulator transport (`service.adb.tcp.port=0`)
after the test.

The first in-APK attempt also revealed that the Tokio timeout future was being
created before entering the runtime, causing `there is no reactor running`.
Creating the timeout from inside `Runtime::block_on` fixed the panic; the next
attempt returned the expected connection-refused error without terminating
the app. This AVD result does not rule out connecting to a phone's own
`adbd` interface when that phone exposes a supported local endpoint, but it
does not provide a usable non-Wireless ADB path for this emulator.
