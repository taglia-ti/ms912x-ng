# Android driver prototype

This isolated subproject targets Android 11+ (`minSdk 30`). It does not import
Android driver code, Java, or native dependencies from scrcpy. Pairing, the
shell helper, USB transport, and adapter output remain incomplete.

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

The app shows the Android display inventory and offers two explicit sources:

- **Create a new independent display:** creates a fixed 720x1280
  `OWN_CONTENT_ONLY`/presentation display for the local framework diagnostic.
  App-UID launch permission is still denied; only a development host shell can
  launch a test activity there. This is not yet the product's independent
  desktop path.
- **Phone screen/app window:** requests MediaProjection consent and captures
  what the Android system picker authorizes. A foreground service owns the
  projection and an `ImageReader` sends images into the existing native frame
  slots. This explicitly mirrors screen/window content; it is not an
  independent desktop.

Both capture routes currently use fixed diagnostic geometry. The output
resolution selector will use the adapter modeset and sink EDID catalog after
the Linux mode table is extracted into the shared core (A02). The display
inventory is informational: the app does not claim arbitrary existing Android
displays are capturable. See [`docs/android-validation.md`](../docs/android-validation.md)
for API 30 evidence and the remaining independent-display gate.

Validation evidence and the observed display permission boundary are in
[`docs/android-validation.md`](../docs/android-validation.md). The current
implementation checkpoint and hard gates are tracked in
[`docs/android-driver-plan.md`](../docs/android-driver-plan.md).
