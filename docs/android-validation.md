# Android driver validation log

Date: 2026-10-22. Scope: initial APK/AVD and virtual-display capture probe.

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

## Current gate

A00 remains open until one APK performs wireless ADB pairing, TLS connection,
helper transfer, and shell UID verification. A01 remains open until that
helper creates the independent display, receives the app-owned Surface over
Binder, and launches/captures an exported Activity from another installed app.
The emulator checks are framework probes only; repeat them with the integrated
helper and on the user's AOSP phone before claiming Android 11 or device
compatibility.
