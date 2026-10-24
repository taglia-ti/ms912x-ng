# Android fixture and runtime tests

The test suite covers each implemented Android layer with deterministic input,
error injection, and Android framework execution. It does not claim 100% line
or branch coverage. Real adapter acceptance and a live Wireless debugging
pairing exchange remain hardware/runtime acceptance tests.

## Run

Use installed tools and the repository's pinned SDK/NDK and Cargo lockfile.
Nothing needs root, a USB adapter, or a real pairing code. Instrumentation
installs the debug APK on the explicitly selected test device. Credential files
and paired-device preferences use isolated fixture storage; test keys are not
real ADB identities.

```sh
# Repository root: scalar/SIMD, mailbox/damage, EDID, Java ownership, Rust.
tools/android/test_android.sh

# Build/lint/install and run the production JNI + Android fixture groups.
ANDROID_HOME=/mnt/data/LINUX/android-sdk \
  tools/android/test_android.sh --device-only emulator-5554
ANDROID_HOME=/mnt/data/LINUX/android-sdk \
  tools/android/test_android.sh --device-only emulator-5556

# English 1080x1920 AVDs: actual system consent, capture, restart and cleanup.
ANDROID_HOME=/mnt/data/LINUX/android-sdk \
  python3 tools/android/test_capture_ui.py --serial emulator-5554
ANDROID_HOME=/mnt/data/LINUX/android-sdk \
  python3 tools/android/test_capture_ui.py --serial emulator-5556

# Address/undefined-behavior sanitizers inside Android, including C fault injection.
ANDROID_HOME=/mnt/data/LINUX/android-sdk \
  tools/android/test_native_android.sh emulator-5554
```

The local AVDs are `ms912x-api30` (port 5554) and `ms912x-api36` (port 5556).
Use `ANDROID_AVD_HOME=/mnt/data/LINUX/android-sdk/avd` when launching them.
The commands select devices explicitly and do not start or stop someone else's
emulator. UI tests remove active app sessions in a `finally` block. All
capture display/service leak assertions run before that forced cleanup.

## Fixtures and path map

| Production code | Exercise |
|---|---|
| `shared/ms912x_rgbx_*` | 1,008 layout/order cases per available backend: RGBX/BGRX, scalar/SSE2/SSSE3/AVX2/NEON, crop/offset, row/pixel strides, unaligned buffers, ignored alpha, vector tails, unsupported-backend rejection, bounds/overlap rejection and guard pages; Linux/Android use the same `tools/test_rgbx.c` fixture |
| `frame_pipeline.c`, `ms912x_frame_damage.c` | Full/partial/unchanged frames, slot replacement/exhaustion, immutable submitted slots, independent producer/submission tokens, current/previous damage replay with latest pixels, bounded history, idle repair, packet-budget fallback, reconfiguration, stale/duplicate completion, abort/full repair, reset during transfer and concurrent producers |
| `ms912x_edid_core.h`, JNI repair | Known zeroed header byte repaired only when the resulting block checksum matches; invalid sizes, valid input, wrong header and additional corruption rejected without mutation |
| Native allocation/locking errors | Fail each of 7 creation allocations, 6 replacement allocations and both mutex initializations; assert failed reconfiguration preserves a usable original session and permits retry |
| `jni_capture.c`, `NativeCapture` | Invalid direct-buffer arguments, position/limit/crop/size/generation rejection, closed/unknown handles, encode token validation, reconfiguration, all 29 geometries, independent handles and concurrent destroy |
| `NativeTestPattern`, shared scanner/mode-3 encoder | Exact headers/payload/trailers, rectangle alignment and capacities, color bars and invalid pattern destinations |
| `FrameSubmission` | Read-only packet view; exactly-once commit/abort; transfer exception, stale completion, idempotent close |
| `UsbHost`, `UsbAdapterSession` | Framework-shaped Parcelable descriptors; five supported product IDs; single-interface and composite HID-0/video-3 ownership; force only the non-boot control HID; second-claim and release failures; wrong/missing/ambiguous interfaces/endpoints; permission/open failures; all 29 modes with exact command bytes/order; SDRAM; valid/bad base/extension EDID; monitor name; packet writes and close |
| USB control errors | Inject negative and short results at every control transfer in a complete setup; cleanup remains observable and invalid EDID cannot program video |
| `UsbBulkWriter` | Multi-chunk and short writes preserve exact bytes and caller position; invalid packets, negative/zero/oversized results, cancellation and deadlines inside a chunk |
| `UsbOutputController` | Missing host/device; automatic attach probe and reconnect while a cancelled worker drains; unsupported/stale/duplicate attach rejection and active-screencast protection; wrong-device, denied, granted, cancelled and throwing permission callbacks; multi-adapter dialog selection/cancel and targeted attach; invalid capture/mode; real ImageReader frame to fake USB; bulk failure, stop, detach and closed controller |
| `AdbEndpointDiscovery` | Both service types, unrelated services, valid/invalid resolution, error callbacks, thrown registration/stop, idempotent close, late callbacks and no restart after close |
| `AdbCredentialStore` | Android Keystore encrypt/decrypt with fresh nonce, atomic file replacement, corrupt/truncated ciphertext, filesystem failure, case-normalized/missing/malformed metadata and local-address checks |
| `NativeAdbBridge`, Rust boundary | Production library load/version, Java callback and exception translation, null text, local-address policy, invalid ports, mismatched endpoints, pairing-code validation and TCP restrictions |
| Rust async runtime | Success, ordinary error, timeout, panic containment, suppressed payloads and bounded/control-character-filtered debug payloads |
| `ScreenSource`, `VirtualScreenOptions` | Phone/secondary/new/app sources, API 30 vs 36 controls, refresh request, all four supported rotations and invalid/unsupported rotation |
| `CaptureController`, `TestScreenPresentation` | Actual virtual display and animated scene through ImageReader/JNI; consecutive queued frames without timed drops, full packet, pending submission, abort/repair, repeated close and close from the borrowed-image callback |
| `MainActivity` | Gear drawer navigation, Settings/Debug pages, predictive/legacy Back handling and test-control placement; all 29 resolution/rate selections, EDID gating/fallback, secondary display limitation, selected 1920×1080@60 virtual-screen capture and fake USB output, settings lock/unlock, start/stop, launch probe, endpoint matching, pairing dialogs/async validation and discovery restart/timeout |
| Scene visibility/lifetime | Hide/show preserves the same display/capture owner; Stop disables scene controls and clears the running-screen diagnostic |
| `ExternalDisplayFixtureActivity` | Standalone test-APK UID, shell launch onto the APK-owned display, independent captured pixels and pointer/key receipt; optional PC/physical test |
| `ProjectionCaptureService` | Binding/initial state, null/missing/invalid consent, foreground permission failure, typed failure/stop callbacks, and cleanup; approved/cancelled capture, background/resume, restart, and app-window selection use system UI tests |

`AndroidFixtures.java` owns the reusable descriptor, EDID, register-response,
USB host, connection, and assertion fixtures. Fakes terminate at the framework
boundary: production mode programming, encoding, capture, JNI and worker logic
remain in use. `DiscoveryFixtureTests` injects NSD callbacks. The application
contains interfaces and framework bindings only; fake implementations live in
`src/androidTest/`.

`DisplayModeOptionTest` additionally checks all established-timing mappings,
all four standard-timing aspect ratios, every supported CTA code/native flag,
and malformed CTA offsets/blocks. Source format checks consume the same timing
catalog as the app, while expected USB report layouts are independent fixtures.

## Results and remaining acceptance gates

Reports are written under `android/app/build/reports/android-fixtures/` with
API level, serial, APK SHA256, per-group output, and UI screenshots/dumps.
The runner exits unsuccessfully on any failed instrumentation group; it checks
`INSTRUMENTATION_CODE`, since `adb shell am instrument` alone may exit zero
when tests fail. The UI runner pulls the installed APK and verifies its hash.

The app-window UI regression selects both MS912x Desktop itself and Settings.
`MainActivity` discards a duplicate launcher activity above its existing task
root so the picker preserves the original consent owner and capture controls.

The fixtures prove software behavior for the supplied responses. They cannot
establish physical monitor presentation, actual short-write behavior of an OEM
USB stack, a real monitor's EDID, or successful wireless pairing/TLS/shell against
adbd. The built-in independent desktop helper remains unimplemented; the
PC-assisted separate-app test is described below. JNI allocation/VM
failures and Android Keystore/filesystem-provider failures not reproducible on
the installed AVDs are not exhaustively injected. These limits are not reported
as passing hardware tests.

Observed runs are recorded in [the validation log](../../docs/android-validation.md).

## Physical HDMI recording check

Use the capture card's persistent `/dev/v4l/by-id/` path and verify its USB
vendor/product/serial before recording. Do not use `/dev/videoN`: a KVM webcam
can appear or disappear and change those numbers. The tested card was UltraSemi
`345f:2131`, serial `20210623`; the Logitech webcam was excluded.

With **Create a new screen**, 640×480, and 0° selected, record after HDMI sync:

```sh
ffmpeg -f v4l2 -input_format yuyv422 -video_size 640x480 -framerate 60 \
  -i /dev/v4l/by-id/usb-UltraSemi_USB3_Video_20210623-video-index0 \
  -t 10 -an -c:v ffv1 -level 3 -threads 2 pattern.mkv
python3 tools/android/check_hdmi_pattern.py pattern.mkv --report pattern.json
```

The checker needs host FFmpeg and NumPy. It reads the saved video only, checks a
static bar ROI and one moving cyan marker, and exits unsuccessfully on errors
or a marker that never moves. This is a scene-specific stale-damage check;
the card's sample rate is not fresh Android/USB frame rate or monitor FPS.

The checker uses resolution-relative regions and also checks unscaled 1920×1080
recordings. Set the capture-card resolution to match the selected adapter mode.
It excludes no samples by default. `--skip-frames N` explicitly records any
excluded startup buffers; retain the unfiltered recording and initial failed
report when investigating a card-format transition.

The [shell-assisted display test](../../docs/android-virtual-display-test.md)
describes installation/launch of the separate test-APK activity, waiting for first
draw, pointer/key checks and cleanup. It is not part of the adapter-free 14 groups.

## Mirror-only API 24 fixtures

Use `tools/android/test_android.sh --mirror-only --device-only SERIAL`. This
build runs six groups covering JNI submission/close, mode boundaries, USB writes,
ImageReader lifecycle and the mirror-only source inventory. It excludes ADB and
private-presentation fixtures. Android 7 system consent and projection startup
were also exercised through UI; native fixture USB writes do not prove legacy
USB hardware compatibility.
