# Android driver prototype

This subproject targets Android 11+ (`minSdk 30`) and keeps all new Android
code below this directory. The first increment contains an installable app
shell and the app-side capture/native-buffer path. It does not yet create a
virtual display, pair with wireless ADB, capture a third-party app, or send data
to an adapter.

## Build setup

The project pins Android Gradle Plugin 8.13.2, Gradle 8.13, compile/target API
36, and NDK 30.0.16248370. It initially packages `arm64-v8a` only. Install those
SDK components through the user's normal Android SDK setup, then run:

```sh
./gradlew :app:assembleDebug
```

The Gradle wrapper is checked in. The host currently has Gradle 9.8.1 and Java
21, but no configured Android SDK; therefore Android APK assembly has not been
run here. Do not change the pinned versions just to make an unrelated host
setup appear to work.

## Current code

- `app/src/main/java/io/github/ms912x/desktop/MainActivity.java` is an honest
  status screen for the prototype.
- `AdbEndpointDiscovery.java` performs bounded discovery of the two Android
  wireless-debugging DNS-SD service types; discovery is not pairing or TLS
  connection.
- `CaptureController.java` owns an RGBA `ImageReader`, drains images on a
  dedicated thread, closes every acquired image, and throttles accepted frames
  to an initial 10 Hz.
- `NativeCapture.java` checks the session handle and forwards a direct image
  buffer synchronously to native code.
- `app/src/main/cpp/frame_pipeline.c` validates the accessible last pixel,
  crop, row/pixel strides, dimensions, and geometry generation; it copies into
  three owned BGRX slots and bounds total slot storage to 64 MiB. At most the
  newest pending frame is retained alongside an active frame.

The helper-to-Surface connection and native acquire/send worker are not wired
yet. The app intentionally does not request USB permission or start output
until the ADB/display and native USB phases are implemented.

## Dependency gate

See [DEPENDENCIES.md](DEPENDENCIES.md). No embedded ADB implementation has been
selected or imported. The one-APK wireless-pairing requirement remains open;
an external Shizuku/ADB service is not counted as completion.
