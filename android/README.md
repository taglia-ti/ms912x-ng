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

## Current capture prototype

The app has a local diagnostic button that creates a 720x1280
`OWN_CONTENT_ONLY`/presentation display backed by an app-owned `ImageReader`.
It copies images synchronously into native owned BGRX slots and reports accepted
and rejected offers. A host-side emulator shell can launch an exported system
activity on that display to exercise cross-UID composition during development.
This is only a framework/capture diagnostic; it is not the built-in pairing or
shell-helper product flow.

Validation evidence and the observed display permission boundary are in
[`docs/android-validation.md`](../docs/android-validation.md). The current
implementation checkpoint and hard gates are tracked in
[`docs/android-driver-plan.md`](../docs/android-driver-plan.md).
