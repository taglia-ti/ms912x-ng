# Android driver validation log

Date: 2026-10-25 (latest entry). This is a dated validation log for those
artifacts, not a current compatibility matrix. Scope: APK, native/Android
fixtures, API 30/36 capture, and one physical API 37 phone/USB/HDMI combination.
Build reports and APKs under ignored `build/` directories can be removed by clean.
For subsequent mouse-sharing results, see the [DeX experiment](android-dex-topology-experiment.md).

## Independent screen: P5

P5 accepted Desktop APK SHA-256:
`eb9291efbb00bc3b093d0610562dbf6b45115937ab9db19fa100fecc7bc52b16`.
Bundled helper SHA-256:
`db7bc5aa20fa4d9ae678afd62d379bd4e446aa3e568ca7d22b4e2275b31a5414`.
The accepted APK was saved as
`android/app/build/outputs/apk/debug/app-debug-validated.apk` at the time; that
generated file may no longer be present after cleaning.
See [implementation and usage](android-created-screen.md).

| Check | Result |
|---|---|
| Desktop build/lint | Passed |
| Desktop API 30/36 | 18/18 instrumentation groups on both |
| Mirror build/lint | Passed |
| Mirror API 24/30/36 | 6/6 groups on all three |
| Helper host control/parser/bootstrap framing | Passed, including launch generation/stop fences |
| Real-helper independent creation, separate app launch and Stop on API 30/36 | Passed; native images received and owned display removed |
| Embedded same-phone bootstrap, independent creation and physical HDMI on API 37 | Passed |
| Phone Home, Activity recreation and notification Stop | Passed |
| Phone app picker launches YouTube onto created display | Passed; activity placement and HDMI interface observed |

Reports: `created-desktop-regression/*-accepted.json`, compatibility creation
reports in `created-desktop-regression/*-created-final/`, and phone hardware
acceptance in `created-desktop-phone-accepted/`. Those compatibility runs created
displays 9/7 and accepted 4/5 native images on API 30/36 respectively. Both used
the final Desktop APK. Mirror APK SHA-256:
`23bb9b5a317e63f6ef82f3b0dde31245b25fc5f8443d9fbfde7272823f8eca70`.
Its helper assets/ADB bridge exclusions were verified.

The phone (Samsung SM-S928B, Android 17/API 37,
`samsung/e3qxxx/e3q:17/CP2A.260605.016/S928BXXU6EZIF:user/release-keys`)
created display 327, `ms912x-desktop-7396043811432151547`, with actual shell UID
2000 ownership. No scrcpy/client or host-created display supplied the source.
The app connected through its embedded TLS client with stored credentials,
transferred/verified its bundled helper and created a 1920×1080 screen. Adapter
`534d:6021`, monitor `28E850`, output 1920×1080 at its advertised 50 Hz.

| Stage | Decoded / checked / excluded card samples | Checked geometry errors |
|---|---|---|
| Foreground | 120 / 105 / 15 | 0 |
| Home | 120 / 105 / 15 | 0 |
| Activity recreated | 120 / 105 / 15 | 0 |

All 315 checked UltraSemi YUYV/FFV1 samples had correct full-screen bounds,
corner/color regions, circle proportions and both live marker states. Only the
declared first 15 samples of each recording were excluded for UVC settling.
These samples are not display FPS or independent Android frame counts.
The app then selected `com.google.android.youtube` through its actual app picker;
the activity dump confirms placement on display 327. That fourth recording has
no geometry oracle and establishes interface output, not video playback/audio.

Native capture accepted 133 images, rejected zero, and diagnostic encoding was
disabled with USB owning encoding. Notification Stop acknowledged display release
and removed the owned display. Neither a helper process nor a live created display
remained after cleanup. The API 30 system launcher refused both home attempts
(`-96`); its tested fallback welcome activity did not prevent separate app launch.
An API 36 emulator System UI ANR interfered with an initial UI test run; after
dismissing the emulator's Wait dialog, the final full suite and creation fixture
passed. The host build again required the documented OpenJDK 21 JIT limits.

This validates APIs 30/36 and the single API 37 phone, not every intermediate
API/vendor implementation. Input forwarding, audio and protected video playback
remain untested/unimplemented in this milestone.

## Shell-helper P4: embedded same-phone bootstrap

Historical P4 Desktop APK SHA-256:
`25d927d33c9da7e693de10ddc06a73ca9ddaf02793c4183b08122d82209585b3`.
Bundled helper SHA-256:
`ef9ac77300ed719023bdf20bf1761ee9ee666108e2a30f6d830f52b00c831dff`.
That milestone's exact APK was preserved as `app-debug-validated.apk` before
P5 replaced the current validated artifact. The generated helper, its digest
and the pinned DroidMux provider are built with the APK.

The service now retains an opaque Rust session handle and Tokio runtime for local
TLS ADB, UID-2000 verification, Sync upload, remote SHA-256 validation and a fixed
helper command. One-use bootstrap metadata is sent over stdin; raw helper output
is bounded/discarded. No alternate ADB implementation or pixel path was added.
Home/recreation preserves the Rust session, helper, reader and USB owner. Reader
cleanup still waits for producer termination and USB drain; foreground teardown
also waits for process/stream retirement.

Checks and artifacts under `android/app/build/reports/`:

| Check | Result |
|---|---|
| Rust tests / Clippy with warnings denied | 5 tests passed / passed |
| Helper host control and dump parser | 8 control groups plus parser passed |
| Bootstrap framing tests | Passed bounds, validation and exact stdin consumption |
| Desktop API 30/36 | 18/18 instrumentation groups on both |
| Mirror API 24/30/36 | 6/6 groups on all three |
| Desktop and Mirror build/lint | Passed |
| Actual same-phone pairing/TLS and two bootstraps | Passed, `helper-p4-pairing-final/` |
| Stored-credential bootstrap and live NSD/picker reconnect | Passed, `helper-p4-discovery-final/` |
| Embedded USB/HDMI + notification Stop | Passed, `helper-p4-hdmi-accepted/` and `helper-p4-final/` |
| Actual retained-stream EOF + session cleanup | Passed, `helper-p4-stream-loss-accepted/` |

Desktop instrumentation, same-phone pairing and the initial hardware/EOF runs
used APK `3b86ae7a53982ce7e2a9aaf55b341b0164f93c6efd11c11fd40e6f01310bf8aa`.
The final APK above changes only the initial helper/setup status strings; ZIP
payload comparison found only `classes3.dex` changed, with identical native
libraries/helper. Live NSD/picker reconnect and the final HDMI run use that final
APK. Mirror fixture APK SHA-256:
`c2bc84dd163f2c5f930a5f2e951e105a6a1da03e49c806e829a75f44ae978dac`.
Reports are in `helper-p4-regression-final/`. An OpenJDK 21 launcher/JIT crash
occurred during one host build; retrying with `TieredStopAtLevel=1` and
`ActiveProcessorCount=4` completed both builds/lint. Its diagnostics remain in
`helper-p4-host/`; this was not an Android application failure.

Phone: the same Samsung SM-S928B Android 17/API 37 build as P3. Wireless debugging
was already enabled. The optional pairing fixture keeps the secret Settings
dialog on the phone and calls the production pairing bridge; no pairing code is
passed through host arguments, logs or persistent files. It verifies actual UID
2000 after pairing, persisted credentials, transfer/start and a second bootstrap.
The reconnect-only test also discovers the current endpoint through real Android
NSD and selects it through the app's Connect action/picker. Normal usage still
requires the user to enable Wireless debugging and pair in Settings, preferably
with both panes visible while the code server is active.

The physical source was a dedicated scrcpy-owned display 318, identity
`virtual:com.android.shell,2000,scrcpy,312`, at 2560×1080. These are observed values,
not defaults. Adapter `534d:6021`, monitor `28E850`, advertised output 1080p50.
The app verified shell UID 2000, uploaded its own generated helper and verified
its remote digest. PC ADB drove instrumentation and the independent source, but
no PC helper launcher/upload was used for these embedded runs.

| Run / stage | Decoded / checked / excluded card samples | Checked errors |
|---|---|---|
| Embedded Stop, foreground | 119 / 104 / 15 | 0 |
| Embedded Stop, Home | 120 / 105 / 15 | 0 |
| Embedded Stop, recreated | 120 / 105 / 15 | 0 |
| Stream EOF, foreground | 120 / 105 / 15 | 0 |
| Stream EOF, Home | 120 / 105 / 15 | 0 |
| Stream EOF, recreated | 120 / 105 / 15 | 0 |

Those six accepted recordings contain 629 checked card samples with no geometry
errors, using UltraSemi YUYV/FFV1. Content bounds are `(0,135)-(1920,945)`, with
correct bars, color/corner regions, circle proportions and both live marker states.
The final UI-text APK was additionally tested at foreground/Home/recreation:
three accepted recordings each checked 105 samples with zero errors. Its initial
foreground attempt had three startup geometry/marker failures among 105 checked
samples; the predeclared retry was clean. Both attempts/results remain in
`helper-p4-final/`. Only the declared initial 15 UVC samples per recording are
excluded; capture-card samples are not Android frame counts or display FPS.

The first embedded Stop run accepted 54 native images, zero rejected, mirror 319,
and acknowledged release. EOF accepted 69, zero rejected, mirror 320; its report
correctly records **no release acknowledgement**, followed by confirmed helper
death and reader/USB cleanup. The final APK run accepted 74, zero rejected, mirror
321, acknowledged release and USB update snapshots 1→31→43. Diagnostic encoding
was disabled and USB owned native batch encoding in every run. The same original
source survived Stops/EOF until its test-owned client was explicitly closed.
No helper process or live helper mirror remained after cleanup.

Stream EOF exercises transport/lifetime cleanup; a Wi-Fi radio toggle or disabled
Wireless-debugging setting was not tested. Automatic reconnect after transport
loss, new-desktop creation and application launch/input remain outside P4. See
[P4 implementation and use](android-shell-capture-p4.md).

## Shell-helper P3: service-owned USB and capture

Accepted Desktop APK SHA-256:
`f85ee40da7bef3c689788186936f85e4f582159ca3fa7c47080326b2cae48256`.
Helper SHA-256 remains
`9f8fbf4280a74d56d92b5321487cabcd4551ff03d1acf8a2c57bfd109561a2cc`.
At the P3 checkpoint this was the installed/validated APK. The current preserved
artifact and phone installation are the P4 APK recorded above. Switching Gradle
variants can recreate the output directory, so acceptance also retained a copy
outside it. Rebuilding Desktop reproduced every ZIP entry payload of this APK.

`CaptureSessionService` owns one coordinator/USB controller. The interface is an
observer and permissions/selection client. USB permission and an advertised EDID
mode precede connected-device foreground promotion. Only real consented phone/app
capture starts the separate mediaProjection service. Reader destruction waits for
both USB drain and producer stop; helper output additionally waits for actual
helper retirement before ending foreground ownership. Source identity and session
generation checks reject stale starts/results. Normal helper output leaves batch
encoding exclusively to the existing USB/native pipeline.

Final instrumentation, under `android/app/build/reports/helper-p3-regression/`:

| Target | Result |
|---|---|
| Desktop API 30 | 17/17 groups, accepted Desktop APK |
| Desktop API 36 | 17/17 groups, accepted Desktop APK |
| Mirror API 24/30/36 | 6/6 groups on each target |
| Desktop and Mirror builds/lint | Passed |

The new coordinator group checks permission ordering, startup cancellation,
late READY, first-image gating, completed-batch activation, simultaneous failures,
reader retention, reused display IDs and confirmed helper retirement before
foreground teardown. These use real readers/native encoding with injected USB
and foreground roles; they do not prove physical output. Full UI regression also
passed 11 checks on API 30 and 13 on API 36, including actual consent, rejection,
restart, Home/resume and both app-picker choices on API 36. That UI-only run used
intermediate APK `af9e321457e941e239ca118d1ec0e8748b33c3dc8a5857b79c44896a9831b4b8`;
final instrumentation and all physical runs below use the accepted APK above.

Physical target: Samsung SM-S928B, Android 17/API 37, build
`samsung/e3qxxx/e3q:17/CP2A.260605.016/S928BXXU6EZIF:user/release-keys`.
Actual adapter `534d:6021`, monitor `28E850`, output **1920×1080 at 50 Hz**.
This EDID advertises 1080p50 but not 1080p60; no unadvertised mode was forced.
The helper ran as actual shell UID 2000 using the DisplayManager compositor-fit
backend. Two temporary scrcpy-owned sources were independently created for tests:
display 298 at 1920×1080 and display 306 at 2560×1080. These IDs are observations,
not selection defaults. Both original owners survived every application Stop.

| Geometry run and UI stage | Decoded / checked / excluded card samples | Errors |
|---|---|---|
| Matching, foreground | 119 / 104 / 15 | 0 |
| Matching, Home | 120 / 105 / 15 | 0 |
| Matching, Activity recreated | 120 / 105 / 15 | 0 |
| Ultrawide, foreground | 120 / 105 / 15 | 0 |
| Ultrawide, Home | 120 / 105 / 15 | 0 |
| Ultrawide, Activity recreated | 120 / 105 / 15 | 0 |

The final runs used one recording attempt per stage. Each recording excludes its
first 15 UVC startup samples, declared before checking. All **629 checked
samples** passed solid colors/corners, circle proportions and the live marker;
ultrawide content bounds were `(0,135)-(1920,945)`. No CPU scaling was introduced.
These are card samples, not independent Android frames or display FPS.
The ultrawide cleanup report records 39 accepted native images, zero rejected,
mirror 313, acknowledged release, and `encoding_owner=usb-output` with diagnostic
encoding disabled. Its zero diagnostic batch count is not a USB update count.
Snapshot USB updates advanced 1→15→25 across foreground/Home/recreation. Matching
snapshots advanced 1→10→22. Each run used the notification's actual Stop
PendingIntent; cleanup removed only our mirror, closed the reader and retired the
helper while keeping the source alive. Service dumps confirmed connectedDevice
foreground type and no mediaProjection owner for helper output.

Reports: `helper-p3-final-matching/`, `helper-p3-final-ultrawide/` and
`helper-p3-regression/` under `android/app/build/reports/`. Capture used only
`/dev/v4l/by-id/usb-UltraSemi_USB3_Video_00210623-video-index0`, YUYV and FFV1.
Earlier exploratory MJPEG capture had malformed dimensions; an initial trial also
had a Samsung source-notification overlay. Those failures were retained separately
and were not counted as accepted geometry samples. The final YUYV runs above had
no retry or post-startup exclusions.

Actual default-display MediaProjection plus USB passed the same Home/recreation/
notification Stop lifecycle on the phone, with explicit Android consent and both
foreground roles. The reader remained identical and accepted-image versions
advanced 32→40→77 across the three stage snapshots. Its HDMI recordings show the
phone interface/Home with pillarboxing; they have no automated geometry oracle.
See `helper-p3-phone-projection-final/`. Notification permission was granted for
hardware acceptance; the app now requests it from Start on API 33+, while denial
still allows the in-app Stop action.

At this P3 checkpoint the user flow was PC-assisted. Embedded same-phone
transfer/start and reconnect now pass separately in P4 above. See [P3 implementation and use](android-shell-capture-p3.md).

## Shell-helper P2: asynchronous control and lifetime

Accepted desktop APK SHA-256:
`45132bf7df22ec3e4260a42fa9a0cd459876189f265f91ab4e04593595bca7e4`.
Helper SHA-256:
`9f8fbf4280a74d56d92b5321487cabcd4551ff03d1acf8a2c57bfd109561a2cc`.
The helper runs as actual UID 2000 and uses protocol 3. Inventory/control are
asynchronous; a 64-operation serial queue and cancelled-generation fences prevent
late starts from publishing owners after Stop. Reader destruction follows matched
release acknowledgement or confirmed Binder death. USB output now exposes a
drain callback after batch/session cleanup, ready for P3 integration.
At the P2 checkpoint this was the installed/validated APK. The current preserved
artifact and phone installation are the P3 APK recorded above.

All four live termination modes pass on the API 30 emulator and API 37 phone:

| Target | Heartbeat withdrawal | ADB EOF, heartbeats still enabled | App death | Helper death |
|---|---:|---:|---:|---:|
| API 30 | 9901 ms | 728 ms | 439 ms | 461 ms |
| API 37 | 9989 ms | 455 ms | 157 ms | 328 ms |

These are host-observed completion waits after the termination trigger, including
instrumentation/reader cleanup and ADB reporting, not video latency or display FPS.
The watchdog mode also passes missing-display/null-Surface/invalid-size rejection,
pending-start cancellation, retired attach replay, release of an older generation
while a newer capture continues, and duplicate Stop. Each mode starts with actual
accepted images, confirms a live compositor mirror, then confirms helper exit,
source identity preservation and absence of live helper displays. Reports are
`android/app/build/reports/helper-p2-{api30,phone}-{watchdog,eof,app-death,helper-death}/`.

An initial phone attempt correctly failed to receive source images while the
device was dozing. The dedicated test fixture now uses KEEP_SCREEN_ON. A later
cleanup assertion found only a RenderEngine cache entry for an already removed
display. The parser now examines live display records and fails on unknown dump
formats; positive live-display checks and parser fixtures cover both observed
formats and the historical-cache case. It does not equate cached names with live
mirrors or claim that all framework GPU caches are empty.

Plain-Java tests pass eight production control-core groups, including a blocked
factory with an independent termination deadline and constructor/release faults.
Android controller fixtures cover stale replies, live-peer transaction failures,
scoped deadlines, release/death collisions and startup timeouts. Desktop passes
16/16 groups on API 30/36. The existing geometry matrix passes 18 cases and six
live source transitions per target with protocol 3. The Debug preview also passes
two rotated native-encoding sessions and active helper death on both targets:
API 30 accepts 13/13 images; the phone accepts 16/17; all reject zero images and
acknowledge normal releases. Reports are `helper-p2-geometry-{api30,phone}/` and
`helper-p2-capture-{api30,phone}/`. The normal API 36 capture UI passes 13 checks.

Local controller peers do not establish shell privilege. Real blocked framework
services and every private-API permission/signature failure are not exhaustively
injected on hardware. Actual helper lifetime tests are distinct from those host
fault models. See [P2 architecture and commands](android-shell-capture-p2.md).
The helper remains a PC-assisted capture-only preview. P3 service/USB integration
and P4 embedded same-phone bootstrap remain open; no new HDMI result is claimed.
Mirror passes 6/6 groups on API 24/36 and excludes helper/IPC assets/classes and
the Rust bridge. App/helper/IPC lint and host-tool syntax checks pass. Regression
artifacts are under `android/app/build/reports/helper-p2-regression/`.

## Shell-helper P1: fitting, rotation and source geometry changes

P0 was committed as `4638af0`. P1 adds an optional helper-side EGL transform for
rotated output, preserving the direct compositor path at 0°. No Linux/native
conversion or USB protocol implementation changes are included.

Accepted desktop APK SHA-256:
`f0ba52f1a94722cac3578afba73a184913972f1e0b239b58336ad1aacf4492ec`.
Helper SHA-256:
`56a3054b15fdb265c5deea76819816c47867172c4e12f785c51a634a8d4c618f`.
Both helpers run as real UID 2000 and use IPC protocol 2.
These hashes identify the historical P1 acceptance artifact. The current P2
artifact and installed phone package are identified above.

The API 30 emulator and API 37 Galaxy S24 Ultra each pass 18 capture cases:
16 combinations of four output sizes and four clockwise output rotations, plus
two live sessions at fixed 640×480 output (rotations 0°/90°). Each live session
passes three source transitions: 2560×1080 → 1600×1200 → logical 1200×1600 at
source rotation 90° → restored 2560×1080. All use a separate test-APK UID's
known pattern on a dedicated scrcpy display. These are sampled RGB/corner/edge/bar
checks with 12/255 per-channel tolerance, not whole-frame quality measurements.

Source identity and the APK reader/output dimensions remain fixed. The legacy
backend updates logical projection and recreates its virtual framebuffer when
intermediate dimensions change. The recent-phone backend recreates only its own
mirror to discard a stale framework source projection. Matching release
acknowledgements and helper process exit pass; neither DisplayManager nor
SurfaceFlinger retains a helper mirror after the tests.

Reports: `android/app/build/reports/helper-geometry-api30/` and
`helper-geometry-phone/`, including artifact/build hashes, typed geometry,
transition results and before/after display inventories. Setup and remaining
milestones: [geometry guide](android-shell-capture-p1.md).
The Debug UI also passes two native-encoding previews at 90° plus helper death
during active capture on both targets. API 30 accepts 13/13 images in the two
normal sessions, and the phone accepts 16/15, all with zero rejected images and
confirmed release. Reports are `helper-p1-ui-api30/` and `helper-p1-ui-phone/`.
Desktop fixtures pass 15/15 groups on API 30/36; Mirror passes 6/6 on API 24/36.
App/helper/IPC lint, script syntax and Mirror package isolation checks pass.
This preview still requires PC-assisted startup and does not send frames to USB.
No new HDMI or embedded pairing/bootstrap result is claimed.

## Shell-helper P0: capture an existing display

The previous UI/USB work was committed as `7419e94`. The subsequent helper P0
uses the existing native encoder and adds no pixel-conversion or USB protocol
implementation. Build/commands and remaining gates are in
[the P0 guide](android-shell-capture-p0.md).

The accepted debug APK SHA-256 is
`4c4d570b5041aef1975bae33e4660a91dd37cfaf368debd34383b1e42f798995`.
Its generated helper SHA-256 is
`687e4797ba39f30d7a21a51297483ca226367bd864c9e0dd6340dce717c4d295`.
The real ADB/helper UID is 2000 on both targets. Enrollment uses the actual
ActivityManager/IContentProvider Binder route, then passes the app's ImageReader
Surface to the helper through AIDL. The source is a separate test-APK UID's
animated fixture on an independently owned scrcpy display.

| Target | Mirror backend | Source → reader | Accepted images in two sessions | Encoded batches | Changed payload signatures |
|---|---|---|---|---|---|
| Android 11/API 30 x86_64 emulator | SurfaceControl | 1280×720 → 640×480 | 13 / 14 | 22 / 22 | 11 / 13 |
| Galaxy S24 Ultra/API 37 | DisplayManager | 1280×720 → 1920×1200 | 25 / 21 | 27 / 24 | 15 / 12 |

Both targets report zero rejected images and confirmed mirror release in both
sessions. The original source identity remains unchanged. After each release,
the acceptance script waits for asynchronous compositor teardown and verifies
that DisplayManager/SurfaceFlinger no longer contain our mirror. An additional
run kills only `ms912x-helper` during active capture; the app reports helper
death, closes its reader without claiming a release acknowledgement, and the
source display survives. Reports and before/after inventories are under
`android/app/build/reports/helper-api30/` and `helper-phone-controlled/`.

Build fingerprints:

- API 30: `Android/sdk_phone_x86_64/generic_x86_64:11/RSR1.210722.013.A2/10067904:userdebug/test-keys`.
- API 37: `samsung/e3qxxx/e3q:17/CP2A.260605.016/S928BXXU6EZIF:user/release-keys`.

An earlier phone run's original source disappeared or changed geometry during
capture. The helper detected it, released only its mirror and returned
`Source disappeared or changed geometry; select it again` (662 accepted images,
zero rejected, release acknowledged). The test was repeated against a dedicated
temporary source for the table above. This source-change observation is not a
passing repeat-capture test by itself.

Desktop fixtures pass 15/15 groups on API 30 and 36, including helper artifact,
bootstrap authorization/replay and Parcelable checks. Mirror fixtures pass 6/6
on API 24/36. Its APK excludes helper assets, helper/IPC classes, the bootstrap
provider and Rust bridge. App, helper and IPC lint pass. The normal capture UI
regression also passes 11 API 30 and 13 API 36 checks, with its APK identity in
the `helper-ui-api30/` and `helper-ui-api36/` reports. These are capture/encoding
and cleanup checks. The helper preview
does not submit to USB; no new HDMI, DRM-content, geometry/rotation, standalone
ADB pairing or embedded bootstrap result is claimed.

## Shared RGBX/BGRX SIMD kernels

- Linux SSSE3/AVX2 conversion kernels now live in shared headers and specialize
  channel coefficients for Linux little-endian XRGB/BGRX or Android RGBX.
  SSE2/SSSE3 share luma/chroma helpers. No swap buffer, extra image pass or
  per-pixel format branch is introduced. Linux retains the same FPU boundaries,
  strides and tails. The compiled kernel `.text` sections are byte-identical to
  the pre-change objects: SSE2 1,204 bytes, SSSE3 1,381 and AVX2 1,662.
- Userspace selects AVX2/SSSE3 only after CPU/OS feature checks, falling back
  to SSE2, NEON or scalar as appropriate. QEMU CPU tests select SSE2 on qemu64,
  SSSE3 on Conroe/SandyBridge, and AVX2 on Haswell. Unsupported forced backends
  are rejected before conversion. Both byte orders and vector tails are checked
  against the scalar oracle and guard pages.
- Linux and Android now use the same conversion fixture, `tools/test_rgbx.c`.
  GCC and Clang pass 5,040 differential cases on the AVX2 host; ARM64 QEMU passes
  3,024. Host UBSan, native pipeline/fault/concurrency fixtures, complete Linux
  `make tests`, and `make -j"$(nproc)"` pass. The Linux code also passes exhaustive
  RGB-triple arithmetic checks. API 30 NDK ASan/UBSan passes conversion and all
  five native pipeline groups; host ASan could not reserve shadow memory in this
  execution environment and is not reported as passing.
- Desktop API 30 instrumentation passes 14/14 groups; Mirror API 24 passes 6/6.
  Both builds pass lint and include the optional x86 workers without raising the
  APK's CPU baseline. An initial phone UI attempt completed native/capture tests
  but failed two UI groups after focus moved to Firefox. The user requested
  emulator-only validation while using the phone; no new physical HDMI result
  is claimed for this change.
- An illustrative host 1920×1080 conversion benchmark measures RGBX SSE2
  1.050 ms, SSSE3 0.709 ms and AVX2 0.409 ms; BGRX AVX2 measures 0.389 ms.
  These are seven-sample medians of CPU conversion time, not end-to-end casting
  latency or display FPS. Identical kernel machine code is the regression
  evidence for the existing Linux XRGB path. Raw checks and benchmark output
  are in `android/app/build/reports/channel-order-2026-10-24/`.

## EDID probing at startup and USB attachment

- Both APKs probe the already-connected adapter when their main Activity starts.
  Supported USB attachment events start the same serialized permission/EDID
  path, targeting the attached adapter without a multi-device chooser. Duplicate
  events leave pending permission requests and running screencasts untouched.
  A reconnect while a cancelled worker drains defers the probe until cleanup;
  completion callbacks cannot publish an old result into the new operation.
- Desktop's 14 fixture groups and Mirror's 6 groups pass on the physical phone;
  both builds pass lint. USB fixtures exercise missing/unsupported/stale attach
  events, repeated attachment during permission and streaming, and automatic
  reconnect while the previous worker is still draining. System USB callbacks
  are invoked through the fixture receiver; no protected broadcast is forged.
- A cold launch of each APK on the phone reads the real adapter's EDID without opening a
  dropdown: the monitor card shows 27G50F, 1920×1080 and 60 Hz, with Start
  screencast enabled. Evidence is in the phone report's
  `edid-startup-automatic.xml` and `mirror-edid-startup-automatic.xml`.
  Physical unplug/replug is not exercised in this check; attachment/reconnect
  behavior is covered by fixtures.
- A real app-owned 1080p60 screencast also passes the HDMI pattern checker:
  299 decoded samples, zero marker/static-bar errors, no excluded samples.
  FFmpeg discarded one initial short V4L2 buffer before recording. This checks
  the known scene regions, not whole-frame corruption or fresh-frame rate.
  Video/report: `edid-startup-hdmi-1080p.mkv` and `.json` in the phone reports.
  Stop finishes the session. Both exported APKs match the installed phone
  packages and pass signature and 16 KiB alignment checks.

## Back to Home without stopping capture

- Back from the main page opens Android Home instead of finishing an Activity
  that owns an active session. Drawer/Settings/Debug navigation keeps its existing
  behavior. The MediaProjection service binding stays connected during capture
  in the background so startup and system-stop callbacks reach the USB owner.
  Reopening the app returns to the existing session; Stop performs cleanup.
- A new fixture backgrounds the running Activity, waits for both accepted
  images and USB bytes to advance, verifies capture/display identity, and
  brings that Activity back before stopping it. Desktop and mirror phone
  fixtures and the API 24 mirror suite pass. An initial presentation fixture
  timed out waiting for its first image; the repeat passed without capture-code
  changes. Native/ARM64/Rust and Linux regression/build checks also pass.
- On the physical phone, Android Back shows the Samsung launcher for both
  app-owned and consented phone capture. App-owned 1080p60 output passes 299 HDMI
  samples after Back, with zero marker errors/static-bar changes. The Mirror
  APK keeps MediaProjection active in the background and transmits fixture
  pointer/key changes in a 359-sample recording. A sampled HDMI image matches
  the downscaled phone image with mean absolute channel error 7.49/255. These
  are functional/region checks, not fresh-frame-rate or whole-frame guarantees.
- Android 7 also passes real consent, Back to its launcher and return: accepted
  images increase from 19 to 171 with zero rejects, and Stop clears projection.
  Phone evidence is under
  `android/app/build/reports/phone-sm-s928b/back-background/`, with UI dumps in
  its parent directory; API 24 evidence is under `reports/api24-mirror/back-*`.

## Appearance and shared damage drawing

- The menu offers persisted Auto/Light/Dark modes. Auto follows Android's night
  configuration; UI palette and system-bar icons change in place. Native
  light/dark themes cover dialogs and dropdowns without adding UI dependencies.
  Theme changes preserve a running capture and its virtual display.
- Show drawing in Debug uses the Linux UYVY border painter, moved unchanged in
  behavior into `shared/ms912x_draw_core.h`. The Linux wrapper retains its call
  convention. Android decorates encoded outgoing rectangles, including replay;
  it never paints source snapshots or scan baselines. Changing the toggle queues
  a full refresh from the clean baseline, including with no new captured image.
  A toggle during an in-flight batch cannot lose its newer refresh request.
- Host/ARM64 native fixtures and UBSan pass, including byte-level border and
  toggle/repair checks. Linux `make tests` and kernel `make` pass. Desktop
  instrumented fixtures pass all 14 groups on the phone; mirror fixtures pass
  all 6 on the phone and API 24 emulator. Both APK builds pass lint. Appearance
  fixtures cover system-night changes, explicit overrides, saved choices and
  capture ownership; live UI checks also exercise the actual menu button.
- Physical 1080p60 evidence is in the phone reports: `draw-off.png`, `draw-on.png`,
  `draw-cleared.png` and `draw-toggle-hardware-check.json`. Hiding the scene
  freezes capture at 54 accepted images and zero rejects. Enabling outlines
  changes the outer border; disabling them restores the HDMI image exactly
  (zero mean image difference), while the accepted-image count stays at 54.
  Switching Dark to Auto retains virtual display 96. Phone/API 24 screenshots
  verify dark appearance. Auto was restored and screencasting stopped afterward.

## Simplified screencast page and automatic monitor probing

- Opening the screen dropdown refreshes display inventory and starts a serialized
  EDID read. A supported manual timing survives a reread of the same monitor.
  The monitor card shows its name; the manual Read monitor modes action,
  technical explanations and capture/USB statistics live in Debug.
- One main-page button starts and stops the screencast. Controls stay locked
  during capture; stopping drains output before Start becomes available again.
  Permission, capture and connection errors remain visible on the main page.
- Desktop fixtures pass all 14 groups on the phone. Mirror fixtures pass all
  6 groups on the phone and API 24 emulator; both builds pass lint. New checks
  open the real dropdown, verify inventory/EDID refresh and manual timing
  retention, and start/stop fixture output using the same button.
- On the physical phone, opening the dropdown detects 27G50F and enables its
  1080p60 timing. Starting and stopping work through the new button. A five-second
  UltraSemi HDMI recording passes 299 samples with zero marker errors/static-bar
  changes and no excluded samples. Evidence is in
  `android/app/build/reports/phone-sm-s928b/simple-ui-*` and Android fixture
  reports. This checks the app-owned scene, not a whole-frame corruption metric.

## Compact screencast selectors and EDID defaults

- Rotation sits beside the screen selector and refresh beside resolution, with
  content-sized secondary controls. Physical phone and API 24 emulator layouts
  were inspected; older Android shows disabled Auto rotation.
- Mode lists descend by pixel count and Hz. Initial selection follows a
  supported base EDID preferred DTD or CTA native timing, falling back to the
  highest supported timing. Rebuilding the refresh menu preserves manual choices.
- Host fixtures cover descending order, EDID 1.3/1.4 preferences, CTA native
  VICs/DTDs, unsupported hints and malformed blocks. The phone passes 14 desktop
  groups and 6 mirror groups; the API 24 emulator passes 6 mirror groups. UI
  fixtures verify preferred 1080p50 despite higher modes, manual refresh choice,
  and delayed selection callbacks. USB fixtures verify preference propagation
  for all 29 catalog timings.
- The physical monitor probe reports 27G50F, 14 compatible timings, and initial
  1920×1080 at 60 Hz. This check reads EDID without starting USB video output.
  Updated APK exports match both installed phone packages and pass signature and
  16 KiB alignment checks.
- The interface now uses Screencast, Start screencast and Stop screencast in
  controls, navigation and output status. Before this checkpoint commit, the
  full Android host suite (including ARM64 NEON/QEMU, frame ownership/fault
  fixtures, EDID and four Rust tests), phone desktop/mirror groups, both APK
  lint builds, Linux `make tests` and kernel `make` pass.
- Both real capture paths were retested on the API 37 phone through the USB
  adapter at 1920×1080/60 Hz. The app-owned scene passes 599 HDMI samples with
  zero marker errors/static-bar changes and no excluded samples. A separate
  consented MediaProjection run mirrors another UID's fixture on phone display
  0, including its changing counter, pointer count 1 and KEYCODE_A result.
  Its 359-sample HDMI recording includes live changes; a sampled HDMI image
  matches the downscaled phone screenshot with mean absolute channel error
  5.99/255 (including capture-card color conversion and asynchronous counters).
  This is functional presentation evidence, not a whole-frame corruption or
  fresh-frame-rate measurement.
- The phone-screen run reports 239 accepted native copies and zero rejects at
  the final checkpoint. Stop clears MediaProjection and both driver-owned
  display names. Evidence, recordings and check JSON files are under
  `android/app/build/reports/phone-sm-s928b/checkpoint-selectors-screencast/`;
  UI dumps are in its parent directory. The verified UltraSemi persistent
  capture path excludes the KVM webcam.

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
- Updated diagnostics and build instructions for that milestone.
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

## C ADB port: 2026-10-27

The desktop APK now uses `android/adb-c`, replacing the DroidMux/Rust bridge.
Cargo manifests, lockfile, Rust sources and the Gradle Cargo task are removed.
Pairing, TLS/identity, packet/stream flow, Shell v2, Sync, JNI credential callbacks
and retained helper ownership are separate C modules. The dependencies are
Mbed TLS 3.6.7 (verified release archive) and vendored Monocypher 4.0.2.
The Java interface and encrypted credential store are preserved.

Validation on the final C build:

- `make -j8 tests` passes the Linux and Android host suites, including the new
  C crypto, protocol and JNI tests. Compatibility vectors cover SPAKE2 and
  pairing AES-GCM; PKCS#8 import preserves a Rust-era RSA identity. Protocol
  fixtures cover TLS 1.3/client certificates/exporters, pairing, classic AUTH,
  STLS, fragmented Shell v2, interleaved output/acknowledgements, 150 KB Sync
  upload, bootstrap wiping, output limits, cancellation and graceful EOF.
- The host JVM's `-Xcheck:jni` passes input/exception checks and 50 retained
  worker cancel/join/release cycles. UBSan passes the C core and full protocol
  fixture. Valgrind reports zero errors and zero definitely/indirectly/possibly
  lost bytes; 2,904 bytes of process-global crypto state remain reachable.
- Desktop ARM64/x86_64 and mirror-only ARM32/ARM64/x86/x86_64 debug builds and
  lint pass with `CARGO` and `RUSTC` pointing to nonexistent paths. The desktop
  APK packages only the C capture/ADB libraries and third-party notices.
- API 30 emulator instrumentation passes all 21 desktop groups.
- A real Samsung SM-S928B/API 37 passes the optional `embeddedAdb` test with
  `reconnectOnly=true`: the existing encrypted key reconnects over TLS, verifies
  shell UID 2000, uploads and verifies the helper, starts it through authenticated
  enrollment, stops it, discovers the endpoint through NSD and reconnects through
  the app's Connect action. Final process inspection finds no helper left running.

The phone initially exposed an adbd simultaneous-close ordering absent from the
first peer fixture: a second CLSE reply for an already retired local stream ID
could arrive before the next stream's OKAY. The C client now discards bounded
late CLSE replies for earlier monotonic IDs, and the independent peer fixture
reproduces that ordering. The phone acceptance above passes with this fix.

The tested desktop debug APK SHA-256 is
`5ca6c6f41fa27b6b8d376b58bbfabf7f45f667ef2960d09899be927419b73973`.
Local evidence is retained under `android/build/reports/adb-c/`; the ordinary
API 30 instrumentation report remains under the app's Android fixture reports.
The phone runs this C debug build; its previous APK was copied to
`/tmp/ms912x-before-c-adb-phone.apk` before installation.

ASan cannot start on this host: `ReserveShadowMemoryRange failed` while reserving
its shadow mapping, including with ASLR disabled. This is recorded as unavailable,
not passing. Fresh on-phone SPAKE2 pairing and physical HDMI presentation were
not rerun; the real-phone result establishes saved-key reconnect/helper behavior.

## C ADB review: 2026-10-27

The follow-up review fixed several interface and framing issues and narrowed
the ownership of each layer:

- Partial exact reads now invalidate the appropriate packet/stream/shell state.
  Cancellation midway through a packet, shell header or shell payload closes the
  connection instead of attempting another operation with a discarded cursor.
  Interrupted writes invalidate the TLS context before its transmit buffer can
  be reused. Clean-boundary cancellation still sends stdin EOF and drains.
- PEM import no longer reads or wipes an assumed extra terminator byte supplied
  by a storage adapter. Allocated callback results are freed on failure too.
  Regression fixtures return exactly the PEM byte count without a terminator.
- Verification commands reject embedded NUL output and zero-capacity buffers.
  Stream IDs remain exhausted after their counter wraps, preventing ID reuse.
- Feature negotiation matches exact tokens in the features property. Both
  length-delimited and NUL-terminated CNXN banners are accepted; the phone
  exposed the former representation during review. Misleading substrings and
  embedded NUL properties are rejected.
- JNI address callbacks preserve non-BMP Unicode, and pairing rejects nonnumeric
  addresses before calling a Java policy that could perform DNS resolution.
  Credential-callback failures are distinguished from an absent paired device.
- Packet allocation/wiping, connection negotiation, stream flow control,
  operation deadlines, input validation and JNI utilities have separate modules.
  Connection-owned packet buffers are borrowed through an explicit receive/
  release interface. Third-party crypto sources remain unchanged.

Gathered writes remove temporary shell-frame allocation and coalesce Sync
headers/payloads. The independently decoded 150,000-byte transfer uses 39 WRTEs
at a 4,096-byte packet limit, versus 43 for the separate-header layout.
Acknowledgements no longer trigger a full 64 KiB buffer wipe. SPAKE2 caches a
fixed point once per multiplication. Alternating host CPU microbenchmarks
(15 samples of 200 two-peer exchanges per revision) measured median CPU time
of 1,068.75 to 965.05 microseconds per exchange, a 9.7% reduction. These are
cryptographic CPU and protocol-packet measurements, not display frame rates.

Validation passes the Android host suite, four native/JNI groups, all three C
groups under UBSan, all three C executables under Valgrind with zero errors and
no definitely/indirectly/possibly lost allocations, and Clang static analysis of
all 16 production translation units with zero warnings. Desktop ARM64/x86_64
debug/test APK builds and lint pass with Cargo/rustc paths disabled. API 30
instrumentation passes all 21 groups.

On the API 37 phone, the optional fixture passes with `reconnectOnly=true` and
`directReconnect=true`: saved-key TLS, UID 2000, helper upload/digest validation,
authenticated enrollment, stop, another native connection and final cleanup.
No helper process remains running. The ordinary endpoint-picker variant was
blocked by the phone's keyguard and is not reported as passing in this review;
the new direct mode isolates native transport acceptance from UI interaction.
Fresh phone pairing and physical HDMI presentation remain outside this review,
and the previously recorded host ASan mapping limitation remains applicable.

The final desktop APK SHA-256 is
`622b3488502ec3eb7f9f683966a33a79df29c6357c96e2821e8921f1ee79c8a7`.
Logs, benchmark samples and the benchmark harness are retained locally under
`android/build/reports/adb-c-review/`. The phone runs this reviewed debug APK;
its previous C APK is backed up at `/tmp/ms912x-before-adb-c-review-phone.apk`.

### Temporary fuzz validation

The subsequent campaigns completed 67,402,450 executions: 39,762,369 under host
UBSan and 27,640,081 under Android emulator ASan/UBSan. They exercised native
text validation, packet/handshake/stream/Shell/Sync handling, PEM import, SPAKE2
and AES-GCM with simulated socket execution. No crashes or sanitizer findings
were observed. Valgrind replay of 2,015 minimized corpus inputs reported zero
errors and no lost allocations. The four ordinary native/JNI groups passed
after cleanup.

The temporary fuzz sources, build hooks, binaries and corpora were removed as
requested; diagnostic logs remain outside the checkout in `/tmp`. These results
do not constitute a security audit or fuzz coverage of JNI/session concurrency,
real-device pairing, or the TLS authentication policy described above.
