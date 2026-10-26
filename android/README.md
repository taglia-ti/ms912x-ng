# Android driver prototype

The default APK targets Android 11+ (`minSdk 30`). A mirror-only build targets
Android 7+ (`minSdk 24`) using `-PmirrorOnly=true`; see
[shared Linux sources and compatibility](../docs/android-linux-reuse.md). It does not import
Android driver code, Java, or native dependencies from scrcpy. The embedded
Wireless ADB client starts a shell helper on this phone to capture existing
displays or create an independent desktop through Surface IPC. The APK
implements EDID-filtered modes, screen capture, shared UYVY damage scanning,
and USB rectangle submission. Phone-screen mirroring and the app-owned test
scene have been verified through a physical adapter at 640×480 and 1920×1080; broader mode
and device compatibility remains unverified. See [phone test instructions](TESTING.md).

## Build

The project pins Android Gradle Plugin 8.13.2, Gradle 8.13, compile/target API
36, and NDK 30.0.16248370. The debug APK contains `arm64-v8a` and `x86_64`
native libraries so it can run on ARM64 phones and the x86_64 emulator.

For the local SDK installation used during development:

```sh
ANDROID_HOME=/mnt/data/LINUX/android-sdk \
ANDROID_SDK_ROOT=/mnt/data/LINUX/android-sdk \
  make -C android app
```

`make -C android lint` runs Android lint, `make -C android tests` runs the
Android host-side unit and native fixtures, and
`make -C android instrumented-tests DEVICE_SERIAL=<adb-serial>` installs and
runs fixtures on a device or emulator. From the repository root, use
`make android`, `make android-tests`, or
`make android-test-device DEVICE_SERIAL=<adb-serial>` respectively.

The local AVDs cover API 24, 30 and 36. Gradle may install
additional SDK components such as CMake under that SDK root when building.

## Emulator

The local API 36 AVD is `ms912x-api36` under
`/mnt/data/LINUX/android-sdk/avd`. Launch it with:

```sh
ANDROID_HOME=/mnt/data/LINUX/android-sdk \
ANDROID_SDK_ROOT=/mnt/data/LINUX/android-sdk \
ANDROID_AVD_HOME=/mnt/data/LINUX/android-sdk/avd \
  /mnt/data/LINUX/android-sdk/emulator/emulator \
    -avd ms912x-api36 -no-snapshot -no-boot-anim -gpu swiftshader_indirect
```

The AVD was created with a local `config.ini` because the AUR `avdmanager`
looked for system images under `/opt/android-sdk` instead of the custom SDK
root. `/dev/kvm` is available and the emulator uses KVM acceleration.

## Current source and capture prototype

The screen dropdown lists all Android displays visible to the app, plus an
app-window picker on Android 14+ and a **Create a new screen** option. The
default screen uses MediaProjection consent. Secondary displays are listed with
their current dimensions and use the ADB helper. **Start screencast** on
**Create a new screen** checks the adapter, discovers this phone's Wireless
debugging endpoint and reuses its saved pairing. A setup dialog guides the
manual Developer options and first pairing steps when needed. The new screen
uses the selected HDMI dimensions; the helper rejects a mismatched result.

A new helper screen opens Android's secondary launcher when available. Use
**Open app on new screen** to launch an app there. The helper requests freeform
windows; Android may choose fullscreen. This does not activate Samsung DeX.
Selecting **Create a new screen** shows **New screen placement** directly below
the source dropdown: **Virtual / Left / Right / Up / Down**. Virtual uses Touchpad
without sharing a physical mouse. A side enables sharing automatically once the
screen is ready and retries if mouse sharing fails or the mouse reconnects.
Connect the native DeX monitor and a USB or Bluetooth mouse. Position and mouse
choices are saved. Position changes apply live. **Settings → Mouse sharing ·
Experimental** lets you choose a mouse or start/stop sharing manually; stopping
selects Virtual. The helper routes movement across the
shared edge, buttons and scrolling, without root. Using Touchpad pauses automatic
sharing for that session; Start mouse sharing or selecting a position resumes it.
This depends on device-specific shell input access and newer hidden mouse APIs;
unsupported devices show an explanation. It requires 0° output rotation.
Held drags stay on their current display until release. This does not join DeX
taskbars or move application windows between the two desktops. See the
[implementation and test evidence](../docs/android-dex-topology-experiment.md).
While the mouse is on the adapter, its native DeX cursor is parked at a corner
on the corresponding edge: bottom-left for Left, top-right for Up, and
bottom-right for Right/Down. Android clamps the hotspot to the screen, so a small
part of the cursor can remain visible. Returning places it at the entry edge
with proportional height/width mapping between resolutions. No cursor-hiding
overlay is installed on DeX. The adapter keeps its existing small cursor. Hover delivery is
asynchronous and its cursor is updated before dispatch; button and held-drag
events retain ordered delivery. Capture, USB and monitor latency still apply.
The gear menu opens Settings and Debug. Settings provides live profile, damage
scanning On/Off, scan stride and color tolerance controls; Debug contains capture,
ADB and scene test tools.
**Preview app-owned test screen** in Debug shows the animated color scene at the
selected resolution, or 640×480 without a monitor. It is absent from the source
picker and does not require ADB.
The side menu's Appearance button offers Auto (system), Light and Dark. Auto is
the default; the choice is saved. Appearance changes update the UI in place and
keep a running capture alive.
Debug's Show drawing switch outlines outgoing damage rectangles using the same
UYVY border routine as Linux. It marks encoded USB payloads, leaving source pixels
and scan baselines clean. Turning it off refreshes the full display even when
the source is static. This switch is separate from hiding/showing the test scene.
Debug can hide/show the test scene while capture/output continue.
An active helper-created screen enables **Touchpad** beside the gear button.
One finger moves the pointer; tap clicks; a quick double tap double-clicks;
hold then move, or double tap and hold, drags; two fingers scroll.
Apps, Home and Back shortcuts control that screen, in that left-to-right order.
The touchpad uses the app's Auto (system), Light or Dark appearance, including
when its view is created independently. The small cursor hides after
five seconds without movement. Its 12×12 plane starts one pixel right and down
from the click coordinate, keeping that coordinate outside the overlay. The
trusted-overlay flag remains enabled. Input is scoped to the helper-owned display.
The app requests the selected refresh on Android 14+ and exposes
manual virtual-display rotation on Android 16+.
The compact rotation selector sits beside the screen selector. Helper output
supports rotation on Android 11+; consented/app-owned display rotation requires
Android 16+, so earlier versions show **Auto** for those sources.
Debug's separate **Helper output rotation** supports all four output orientations
for the desktop helper preview on API 30+. It fits source content into fixed
reader dimensions and handles live source size/orientation changes. See
[the geometry implementation and pixel tests](../docs/android-shell-capture-p1.md).
The helper uses asynchronous control with cancellation fences and confirmed
release/death before reader cleanup. See
[control/lifetime behavior and acceptance](../docs/android-shell-capture-p2.md).

The app probes SDRAM/EDID at startup and when a supported USB adapter connects,
requesting USB permission when needed. Opening any enabled main-page dropdown
rechecks the adapter and requests missing USB access; the screen dropdown also
refreshes Android's display inventory. Automatic probing leaves an
active screencast or permission request undisturbed; a reconnect during worker
shutdown waits for that worker to finish. **Read monitor modes** remains
available in Debug for a manual probe. The main page shows the monitor name;
capture statistics and technical details are in Debug. Resolution and a compact
adjacent refresh dropdown contain the
intersection of the monitor EDID and the shared Linux timing catalog, ordered
highest first (resolution by pixel count, refresh by Hz). Both source-screen
and monitor resolution choices include their aspect ratio. The initial selection
uses a supported EDID preferred timing or CTA native timing; without a supported
hint, it uses the highest resolution and refresh. Rereading the same monitor
preserves a still-supported manual timing. The screencast button changes from
Start to Stop while running. Starting sizes the capture to the selected
mode, then serially sends pair-aligned UYVY damage rectangles. Source, rotation
and output geometry stay locked until the screencast stops; image matching
settings remain editable and apply immediately. Back from the main page opens Home while an
active screencast continues. Reopen the app to return to the same session and
use Stop screencast to end it. Capture offers
images as they arrive without a fixed application frame-rate cap. The bounded
pipeline keeps the latest available frames when output falls behind; actual
throughput depends on the source, conversion, and USB, independently of monitor
refresh.

The main screen caches the last source screen, rotation, HDMI output resolution
and refresh rate in app-private preferences. On restart, it restores the source
when that display is available and restores output choices only while they are
still present in the adapter and monitor mode list; unavailable choices fall
back to the current defaults. Use **Settings and navigation → Debug tools → Clear
saved screen and output options** to erase these choices and the image matching
preferences, including saved mouse-sharing choices. This keeps Wireless ADB
pairing credentials and appearance settings.
Clearing while a screencast is running preserves its visible session choices;
source/output preferences affect the next startup, while image matching resets
to its defaults immediately. Programmatic inventory and
mode refreshes do not overwrite saved user choices.
HDMI modes over the helper limits (8192 pixels per dimension or 16,777,216
pixels total) are discarded while building the adapter capability snapshot, so
they never enter the cached mode list or appear in the selector. A new adapter
probe rebuilds that list from the monitor and adapter reports.

Image matching preferences are saved independently and restored for every
capture source, including background projection and helper screens. Selecting a
profile resets the manual stride/tolerance overrides to **Profile default**.
The independent **Damage scanning** On/Off choice is preserved when changing profiles.
The effective values appear below the controls:

| Profile | Row stride | UYVY tolerance |
|---|---|---|
| Efficiency | 5 | 0 |
| Latency | 2 | 3–15, adaptive to measured USB pressure |
| Quality | 1 | 0 |

These Android profile values use the shared tolerance and pacing policy. Linux
configures scan settings per adapter through sysfs and udev. Quality is the
Android default. Android does not expose reliable negotiated USB speed, so there is no
Auto profile; older saved Auto selections restore as Quality, preserving their
stride/tolerance overrides. Existing explicit profile IDs remain unchanged.
Damage scanning defaults to **On** and compares captured pixels to send changed
regions. With **Off**, ImageReader supplies no DRM damage rectangles to use instead,
so every newly captured frame is sent in full without comparison. Off preserves
the saved stride/tolerance values for the next switch to On, and idle history
repair remains bounded. This choice is saved and applies live to every source.
Manual stride ranges from 1 to 16 rows; its sampling phase rotates between
comparisons, and larger values can delay or miss small or brief changes. Manual
tolerance has separate minimum and maximum selectors using buckets 0, 1, 3,
7, 15 and 31. Equal values keep tolerance fixed; a range adapts to measured USB
pressure. The buckets mask low bits in each U, Y0, V and Y1 byte, rather than
measuring an absolute RGB distance. Existing saved single values restore as a
fixed range. Suppressed or
unsent pixels do not advance the successful-transfer comparison baseline.
Live changes preserve a batch already being sent and rescan the latest captured
pixels with a full repair, including static content suppressed by the previous
settings. No capture restart or new source image is required.

Build, install, known limitations, and physical test steps are in
[TESTING.md](TESTING.md).
The [Android implementation review](../docs/android-review-2026-10-25.md)
records the performance, ownership and workflow fixes and their regression checks.

Validation evidence and the observed display permission boundary are in
[`docs/android-validation.md`](../docs/android-validation.md). Current screen
creation and mouse-sharing behavior are described in
[independent screen creation](../docs/android-created-screen.md) and the
[DeX experiment](../docs/android-dex-topology-experiment.md).

`MainActivity` composes the screen and controllers and forwards Android lifecycle
events and capture-consent results. `MainScreenView` owns page construction,
navigation and appearance. `DisplaySelectionController` owns display discovery,
EDID mode choices and selection preservation. `ScreencastController` owns capture
sessions, projection-service binding, presentation screens and USB-output
coordination. USB permission/transport and Wireless ADB operations remain in
`UsbOutputController` and `AdbToolsController` respectively.
`ImageSettingsController` owns settings interaction and persistence;
`MouseSharingController` binds the experimental controls and saves their choices;
the shell helper owns mouse capture, routing and cleanup.
`CaptureController` forwards live policy updates to the shared native scanner.
The desktop-only `HelperCaptureController` owns the temporary helper preview;
the separate `helper` module runs as shell UID 2000, and `ipc` shares its typed
Binder contract. The Mirror APK excludes both modules and their assets/provider.

Phone and app-window capture follow the consented Android capture path:
`MediaProjection` → `VirtualDisplay` → `CaptureController` / `ImageReader` →
shared native conversion and frame encoding → USB output. The projection service
starts its foreground notification before creating the projection and releases
the virtual display, reader and projection when capture stops.
The virtual display is a software capture destination. The app sends its pixels
to USB without registering the adapter as a native Android monitor. Protected
surfaces remain subject to Android's capture restrictions, including
[`FLAG_SECURE`](https://developer.android.com/reference/android/view/WindowManager.LayoutParams#FLAG_SECURE).

## Native conversion and submission

RGBX/BGRX converts directly to UYVY with inline scalar, ARM64 NEON, and x86
SSE2/SSSE3/AVX2 workers using the shared Linux `fast_chroma` arithmetic. x86
dispatch checks CPU/OS support before selecting an optional instruction set;
channel order is specialized without a swap buffer. Reusable C conversion
and damage tracking are separate from the Android mailbox and JNI lifetime
registry. An encoded batch advances the baseline only after USB completion;
an aborted batch forces full-frame repair. Each batch covers current and previous
damage with the latest pixels, retaining only current damage as history. A final
idle replay drains that history without waiting for a new captured image.
Linux and Android compile the Linux batch planner and mode-3 serializer, and
share the Linux run detector, timing catalog and checksum-guarded EDID repair. Android-specific lifecycle and USB ownership
stay in Java; conversion and replay use portable C. The only additional Linux
change in the phone review extracts its existing EDID repair into that common
helper, preserving behavior. See [architecture and validation commands](../docs/android-conversion-review.md).

The fixture/path map and host, instrumentation, sanitizer, and system-consent UI
commands are in [tests/README.md](tests/README.md). USB and discovery have small
injectable framework boundaries; fake implementations live only in androidTest.
