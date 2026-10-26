# Android phone test build

Version: 0.10.0-linux-core. Package: io.github.ms912x.desktop.

This APK is for testing the screen-capture and MS912x USB-output pipeline on
a physical Android phone. It is debug-signed and contains ARM64 and x86_64
native libraries. Minimum OS: Android 11 (API 30). ARM32-only phones are not
supported by this build.

## Install and test

1. Build and install app/build/outputs/apk/debug/app-debug.apk
   on the phone. Android may ask you to allow installation from the app opening
   the APK.
2. Connect the powered monitor to the MS912x adapter and connect the adapter to
   the phone through USB host/OTG. Use a powered hub if the phone cannot supply
   enough USB power.
3. Open MS912x Desktop. Open the screen dropdown to refresh the screen list and
   read monitor EDID, then select a screen. Grant Android USB permission if asked.
   **Phone screen** captures the default display after Android consent.
   **App window** uses Android's app picker on Android 14+.
   **Create a new screen** starts an independent screen through this phone's
   ADB helper. Start checks USB access and opens setup guidance if Wireless
   debugging or first pairing is needed. After startup, use Open app or Touchpad.
4. Check the monitor name under Monitor output. Choose resolution and monitor
   refresh from timings supported by both the monitor EDID and adapter catalog.
   Reopen the screen dropdown after changing monitors, or use **Read monitor
   modes** in Debug. Statistics and technical capture details are also in Debug.
5. Select screen rotation if the control is available, then tap **Start screencast**.
   For the phone/app source, approve the new Android capture-consent dialog.
6. Check the physical monitor: the phone source should mirror the phone, while
   the independent source should show its own launcher or selected app.
   The touchpad should move, click, hold a drag, scroll and navigate that desktop.
   USB update counts indicate completed transfers and cannot establish that the
   monitor displayed them correctly. **Preview app-owned test screen** in Debug
   validates capture without USB output; it does not validate the physical monitor.
   Press Android Back from the main page: Home should appear while the monitor
   continues updating. Reopen the app and confirm the same session still offers
   Stop screencast, without requesting capture consent again.
7. The same button now reads **Stop screencast**. Tap it before changing source,
   resolution, refresh, or rotation.
   Repeat with the phone screen. Test unplug/reconnect and read EDID again.

While output is active, open **Settings** from the gear menu. Change profile,
damage scanning On/Off, scan stride and the minimum/maximum color tolerance,
and verify that capture continues on the same display. Choose equal tolerance
values for a fixed setting or a range for adaptation to USB pressure. Choosing
a profile restores its stride/tolerance defaults; manual
overrides apply immediately and the independent scan On/Off choice is preserved.
With scanning Off, new captured frames are sent in full. Reopen the app after
stopping to verify that the choices and both bounds were saved. Debug's **Clear saved screen and output options**
also clears these preferences; during capture it resets image matching live
while preserving the current source/output geometry. See [README.md](README.md)
for profile values and the sampled-row and color-bit tradeoffs.

Phone/app capture and the Debug test scene need no ADB. The independent desktop
requires Wireless debugging pairing and a shell helper; it does not require root.

## Experimental DeX mouse sharing

With native DeX active, select **Create a new screen** at 0° rotation. Choose
**Virtual / Left / Right / Up / Down** in the placement dropdown directly below
the source. A side starts sharing automatically when streaming is ready; Virtual
disables it. Settings' experimental card lets you choose the physical mouse.
Check all four positions,
crossing in both directions, clicks, a drag held longer than one second, and
wheel scrolling. Held drags remain on their current display until released.
Changing position should keep the screencast and mouse sharing active. Stop,
mouse unplug, monitor disconnect and switching to Touchpad should release the
mouse and held buttons. Reopen Settings to verify saved choices; sharing must
follow the saved main-page placement. Unplug/reconnect the mouse while leaving
the screencast active and verify that sharing restarts. Manual Stop selects
Virtual. Touchpad input pauses automatic sharing for that session.
While on the adapter, verify that the native cursor parks at a corner of the
corresponding edge and returns at the proportional height/width when resolutions
differ. A small cursor fragment at the parked corner is expected. The helper
reports unsupported system APIs/access.

The [mouse implementation and QMP procedure](../docs/android-dex-topology-experiment.md)
separate controlled emulator input, native DeX cursor mapping and physical HDMI
verification. QMP tests do not replace checking the cursor on the actual monitor.

## Android capability differences

| Feature | Availability in this build |
|---|---|
| Phone-screen capture | Android 11+, with fresh Android consent |
| User-selected app window | Android 14+ Android picker |
| App-owned test scene | Debug preview on Android 11+ |
| Request new-screen refresh rate | Android 14+; Android may round the request |
| Manual virtual-screen rotation, 0/90/180/270 degrees | Helper output: Android 11+; consented/app-owned sources: Android 16+ |
| Other Android displays in the source list | Capture through the connected ADB helper |
| Independent desktop running other apps | Built-in Wireless ADB bootstrap, app launcher and touchpad; freeform requests depend on Android/OEM support |
| Mouse crossing between DeX and the helper screen | Experimental, opt-in; requires newer hidden mouse APIs and shell input access. Initial handoff was observed on one API 37 Samsung; native cursor parking still needs physical verification. |

Existing-screen refresh remains managed by Android. The app offers captured
images without a fixed frame-rate cap and keeps the latest available frames
when output falls behind. Actual capture/output throughput is independent of
the monitor's EDID refresh rate. The app does not change the phone's physical
display mode or rotation.
Secure/protected app content may appear blank under Android capture rules.

Public API references:
[virtual display refresh request](https://developer.android.com/reference/android/hardware/display/VirtualDisplayConfig.Builder#setRequestedRefreshRate(float)),
[virtual display rotation](https://developer.android.com/reference/android/hardware/display/VirtualDisplay#setRotation(int)),
[MediaProjection source selection](https://developer.android.com/reference/android/media/projection/MediaProjectionConfig).

## Build and checks

Set ANDROID_HOME and ANDROID_SDK_ROOT to the same installed SDK path. From the
android directory, run:

    ./gradlew :app:assembleDebug :app:assembleDebugAndroidTest :app:lintDebug

From the repository root, run:

    bash tools/android/test_mode_catalog.sh
    tools/android/test_frame_pipeline.sh
    tools/android/test_rgbx_conversion.sh

The normal Gradle APK is app/build/outputs/apk/debug/app-debug.apk. The exported
phone-test directory also contains SHA256SUMS for the named APK. Version 0.9.0
passed physical 1920×1080 phone mirroring and app-owned test-scene output on an
ARM64 Samsung phone. The HDMI recording checker found no static-bar changes
or marker trails in 599 captured samples. This does not establish all modes,
all phones, protected content, or independent third-party desktop support.
See [the validation log](../docs/android-validation.md) for exact scope and evidence.

The optional `ms912x-external-display-fixture-0.9.0.apk` is a separate test app
for PC-ADB virtual-display and input tests. Its reproduction steps and cleanup
are in [the virtual-display test](../docs/android-virtual-display-test.md).

The conversion architecture, sanitizer and JNI test commands are in
[the Android conversion review](../docs/android-conversion-review.md).

Run the complete fixture and emulator suites using [tests/README.md](tests/README.md).

## Android 7+ mirror-only APK

Build with `-PmirrorOnly=true`; the separate package is
`io.github.ms912x.desktop.mirror`. This APK uses the same USB output and native
frame pipeline, offers consented capture, and omits ADB/independent-screen
creation. ARM32, ARM64, x86 and x86_64 libraries are packaged without Rust.
API 24 emulator capture and JNI fixtures pass; physical Android 7 USB/OTG and
Android 8 devices still need validation. See [the compatibility record](../docs/android-linux-reuse.md).
