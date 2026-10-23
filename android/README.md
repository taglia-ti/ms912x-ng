# Android driver prototype

The default APK targets Android 11+ (`minSdk 30`). A mirror-only build targets
Android 7+ (`minSdk 24`) using `-PmirrorOnly=true`; see
[shared Linux sources and compatibility](../docs/android-linux-reuse.md). It does not import
Android driver code, Java, or native dependencies from scrcpy. Pairing, the
shell helper, and independent third-party desktop remain incomplete. The APK
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
  ./gradlew :app:assembleDebug
```

The SDK contains API 30 and 36 platforms/system images. Gradle may install
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
their current dimensions and an explicit helper-required explanation; this
build cannot capture them directly.

A new screen normally shows a moving app-owned color test scene. The gear menu
opens Settings and Debug; Settings reserves space for profile, stride and color
tolerance controls, while Debug contains capture, ADB and scene test tools.
Debug can hide/show the test scene while capture/output continue. PC ADB can
then launch another app on the owned display; independent fixture pixels and
input are verified on API 30 and the physical API 37 phone. Built-in privileged
launch/input remains incomplete. See [the test and reproduction steps](../docs/android-virtual-display-test.md).
The app requests the selected refresh on Android 14+ and exposes
manual virtual-display rotation on Android 16+.
The compact rotation selector sits beside the screen selector; earlier Android
versions show a disabled **Auto** control.

**Read monitor modes** requests USB permission and probes SDRAM/EDID without
turning video on. Resolution and a compact adjacent refresh dropdown contain the
intersection of the monitor EDID and the shared Linux timing catalog, ordered
highest first (resolution by pixel count, refresh by Hz). The initial selection
uses a supported EDID preferred timing or CTA native timing; without a supported
hint, it uses the highest resolution and refresh. Starting
a screencast sizes the capture to that mode, then serially sends pair-aligned UYVY
damage rectangles. Settings stay locked until Stop screencast. Capture offers
images as they arrive without a fixed application frame-rate cap. The bounded
pipeline keeps the latest available frames when output falls behind; actual
throughput depends on the source, conversion, and USB, independently of monitor
refresh.

Build, install, known limitations, and physical test steps are in
[TESTING.md](TESTING.md).

Validation evidence and the observed display permission boundary are in
[`docs/android-validation.md`](../docs/android-validation.md). The current
implementation checkpoint and hard gates are tracked in
[`docs/android-driver-plan.md`](../docs/android-driver-plan.md).

## Native conversion and submission

RGBX/BGRX converts directly to UYVY with inline scalar, ARM64 NEON, and x86_64
SSE2 workers using the existing `fast_chroma` arithmetic. Reusable C conversion
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
