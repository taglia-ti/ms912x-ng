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
- An API 30 x86_64 AVD was also configured from the installed image. Its first
  boot remained `offline` in ADB for about two minutes, so it was stopped and
  the API 30 runtime probe is still incomplete. The API 36 AVD was relaunched
  afterward and remains the validated target.

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

## Current gate

A00 remains open until one APK performs wireless ADB pairing, TLS connection,
helper transfer, and shell UID verification. A01 remains open until that
helper creates the independent display, receives the app-owned Surface over
Binder, and launches/captures an exported Activity from another installed app.
Repeat the display permission probe on API 30 after its AVD boots successfully,
and on the user's AOSP phone before claiming Android 11 or device compatibility.
