# Android implementation review — 2026-10-25

Reviewed the desktop and mirror APK source sets, UI/session ownership, USB
permission/probing/streaming, capture services and readers, native pipeline/JNI,
ADB discovery and credentials, embedded Rust transport, shell helper, IPC,
manifests/build configuration and the available regression tools. The review
started from `c268af9` on `android-virtual-desktop`.

## Changes and evidence

Each implementation change was committed after its relevant checks passed.

| Finding | Result | Commit | Regression evidence |
|---|---|---|---|
| Saved choices depended on 300–500 ms layout delays; rebuilds could overwrite a manual refresh choice or repopulate a cleared cache | Explicitly acknowledge programmatic spinner selections; separate preference storage; preserve the visible running-session configuration when clearing preferences | `6f5fa22` | Framework spinner tests cover initial layout, manual refresh, reprobe, fallback and idle/active clearing |
| A quick double tap collapsed into one click; delayed gestures could outlive the view | Distinguish quick taps from held/moved second touches; cancel callbacks and held input on detach; remove draw-time geometry allocations | `e137ac0` | Real View/MotionEvent tests cover tap, double tap, move, both hold gestures, release after more than one second and detached-view cancellation |
| Cursor moves allocated unclosed native transactions, queued duplicate updates and shifted the hotspot at the right/bottom edges | Reuse one transaction, coalesce pending position updates, clean up failed construction and close native resources | `f402c91` | Real shell-created display test asserts cursor layer creation and removal |
| Android rejected mouse input obscured by our cursor; rotated output used unrotated input coordinates; temporary target rejection disabled the touchpad | Mark the shell-owned cursor as trusted, use the actual layer stack, share fit geometry between rendering/input, and tolerate temporary input rejection without treating it as revoked permission | `ce2201b` | Separate-UID receiver verifies button state, stable downTime, held movement and exactly one release at all four rotations on API 30/36; 48 host geometry round trips |
| Legacy NSD resolves could overlap; lost endpoints remained selectable; remote devices appeared in the local setup flow | Serialize/deduplicate resolves, apply the local-address policy before publishing, retire lost endpoints and handle lost/refound ports | `993dabd` | Callback fixtures cover serialization, filtering, loss, rediscovery, thrown operations and shutdown |
| Setup rendering repeatedly read/decrypted the ADB key on the UI thread | Coalesce availability checks on the existing worker; memoize per host; invalidate after pairing and ignore stale completions | `d606b3d` | Controlled worker/UI tests verify no synchronous lookup, coalescing, invalidation, storage failure and close |
| Each pointer command allocated a Binder callback; first-frame readiness waited for the two-second diagnostic timer | Reuse one callback per capture owner and publish the first accepted projection frame immediately | `08a4522` | 100-command callback identity fixture plus actual system-consent capture/restart tests |
| Debug preview used the first catalog entry (720×480), ignoring the selected HDMI geometry and documented fallback | Use the selected output mode, or 640×480 when no monitor is available; keep the test scene in Debug | `3519c68` | Actual Debug UI preview checks dimensions, accepted frames, restart and cleanup |
| A late USB permission reply was matched only by device and could complete a newer request for that same device | Give each permission request a unique Intent data URI; register its callback filter separately from attachment broadcasts | `ba5bdeb` | Real PendingIntent delivery and deterministic stale/missing-identity checks on API 24/30/36 |

The cursor failure was observed in Android's input log as an untrusted overlay
occluding the target app. The relevant system behavior is described in
[AOSP SurfaceControl](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/android12-release/core/java/android/view/SurfaceControl.java):
the trusted-overlay state exempts the layer from input occlusion checks and
requires the shell's SurfaceFlinger access. The flag is applied only to the
helper's cursor layer; ordinary APK windows do not gain that privilege.

## Responsibility and performance decisions

`MainActivity` continues to wire UI actions and lifecycle. `MainScreenView`
owns views/navigation/appearance. `DisplaySelectionController` owns inventories
and valid selections; `DisplaySelectionPreferences` owns their persistence.
`AdbCredentialAvailability` owns worker/UI result coalescing. `SessionCoordinator`
remains the main-thread owner of capture/USB start and drain ordering.

Capture producers, ImageReader/native memory and USB completion retain separate
lifetimes. The shell process owns privileged displays and input. `DisplayGeometry`
is shared by renderer fitting and inverse pointer movement. The native damage,
RGBX conversion, packet, replay and pacing implementations continue to use the
existing Linux/shared production code. No alternative scan/conversion pipeline
was introduced during this review.

The performance changes remove repeated work with clear ownership: one cursor
transaction per overlay, one input callback Binder per capture owner, and one
background credential availability check per host until invalidated. Capture
frames remain unpaced at acquisition and the USB pipeline retains its bounded
latest-frame policy. No new USB throughput or monitor-FPS benchmark claim is
made by this review.

## Validation

The available API 30 and API 36 desktop AVDs passed all 20 instrumentation
groups. The API 24 mirror AVD passed all 9 groups, including the newly enabled
USB permission/output lifecycle group. Both build variants passed APK/test APK
assembly and lint. Lint still reports existing compatibility/deprecation
warnings; the API guards also serve the mirror build.

The real UID-2000 shell-helper tests on API 30 and API 36 passed display/app
creation, cursor resource lifetime, held mouse input and displacement at
0/90/180/270 degrees, release and cleanup. These use actual framework input and
a separate APK receiver, not a mock of input injection.

System UI tests passed on API 30 and API 36: clear saved choices, no-adapter
gating, actionable new-screen setup/close, Debug preview geometry, consent
rejection, phone capture, background/resume, restart, and display/service/crash
cleanup. API 36 also passed app-window capture for this app and Settings.

`tools/android/test_android.sh` passed host conversion, native frame pipeline,
ownership, mode/EDID, helper control, UI state and all five Rust tests.
`tools/android/test_native_android.sh emulator-5554` passed the production native
conversion, pipeline, submission/concurrency, bounds, allocation-failure and
rectangle fixtures with ASan/UBSan inside Android.

The initial desktop baseline passed 17/18 groups on both AVDs; its failing test
still expected the removed test-screen picker item. That fixture now verifies
the Debug-only placement. A later API 36 run timed out waiting for a USB
permission result and passed on rerun. The request-correlation defect was
confirmed separately by code inspection and a deterministic regression; the
timeout alone is not proof of its cause. Intermediate failed builds and test
runs were resolved before the associated changes were committed.

Generated evidence is retained under `android/app/build/reports/android-review/`
and the ordinary Gradle reports. Reproduction commands are in
[`android/tests/README.md`](../android/tests/README.md).

## Practical boundaries

These checks cover the installed API 24/30/36 emulators. They do not establish
all intermediate Android releases, OEM behavior, physical HDMI presentation,
legacy phone USB stacks, protected video capture or a fresh Wireless ADB pairing
exchange. The connected Samsung phone was not modified during this review.
Freeform window launch is a framework request and does not activate Samsung DeX.
Hardware acceptance remains a separate, explicit test.
