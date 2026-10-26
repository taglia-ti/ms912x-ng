# Android display permissions: source investigation

Date: 2026-10-22. These are static findings, not Android runtime validation.
This evidence file records the 2026-10-22 design investigation. For the current
Desktop and Mirror variants, see the [Android README](../android/README.md).

## What the original Android driver actually does

The supplied `MSDisplay_Android_v2.0.0.3.apk` implements **user-approved screen
mirroring**. Its virtual display is the destination of a MediaProjection
capture, rather than a separate desktop hosting other applications.

| Evidence in the locally decompiled APK | Finding |
|---|---|
| [MediaProjectionActivity.java](../drivers/android-analysis/jadx/sources/com/ms/ms2160/myapplication/MediaProjectionActivity.java), lines 23–44 and 48–54 | Requests `createScreenCaptureIntent()`, requires an approved result, and saves the capture authorization |
| [CaptureService.java](../drivers/android-analysis/jadx/sources/com/ms/ms2160/service/CaptureService.java), lines 764–780 | Obtains a MediaProjection, creates an RGBA ImageReader, and calls `MediaProjection.createVirtualDisplay()` with flag `16`, meaning `AUTO_MIRROR` |
| Same service, lines 742–755 and 629–672 | Reads the default screen's metrics and consumes captured images with `acquireLatestImage()` and stride handling |
| [AndroidManifest.xml](../drivers/android-analysis/jadx/resources/AndroidManifest.xml), lines 10–19 | Minimum Android 5.0/API 21, target API 26; declares USB host and overlay permissions, with no trusted-display permission |

The app's Java sources contain no independent-display activity launcher using
`setLaunchDisplayId()`, no `Presentation` implementation, and no app-owned
`DisplayManager.createVirtualDisplay()` path. The capture authorization is what
allows it to read the already displayed applications. Its overlay permission
supports floating UI; it does not grant trusted-display or arbitrary activity
launch privileges. The active capture path therefore avoids needing to solve
the independent-desktop problem.

These are static findings from reconstructed Java and manifest inspection,
not a successful run of that APK on current Android. The package contains only
32-bit ARM native libraries and supplies no evidence of Android 4 support.
See the [existing APK analysis](android-driver-analysis.md) for hashes, USB
transport, and the distinction between its frame-mode output and our mode-3
rectangle output. Android's [MediaProjection documentation](https://developer.android.com/media/grow/media-projection)
describes capture of an existing display or app window into an application
Surface; it does not make that destination a general independent desktop.

## How scrcpy handles the permission boundary

Inspected `/tmp/scrcpy-master.zip`, whose build files identify version `5.0.1`.
Archive SHA-256:

```text
3f53a81c822dd6c9a426ea8fd20df1bdc3daaf0ea3b9aafca1f0c0b73517b431
```

Paths and line numbers below refer to that archive, not an assumed upstream
commit or the current contents of GitHub's moving `master` branch.

| Source within `scrcpy-master/` | Mechanism |
|---|---|
| `app/src/server.c:218–223` and `doc/develop.md:63–87` | Starts the on-device server with `adb shell`, `CLASSPATH`, and `app_process`; it runs with the shell identity, UID 2000 |
| `server/src/main/AndroidManifest.xml:1–2` | Explicitly describes a manually launched program; this is not an ordinary installed application |
| `server/src/main/java/com/genymobile/scrcpy/FakeContext.java:19,60–79` | Uses `com.android.shell` as its package/attribution context while already running as shell |
| `.../video/NewDisplayCapture.java:211–235` | Creates an own-content public presentation display, requests system decorations when enabled, and requests `TRUSTED` and related flags on Android 13+ |
| `.../wrappers/DisplayManager.java:176–181` | Constructs a DisplayManager through reflection and calls virtual-display creation with that context |
| `.../device/Device.java:298–320` | Targets the display with `ActivityOptions.setLaunchDisplayId()` and launches the selected package through its ActivityManager wrapper |
| `.../Server.java:279–289` | Even drops an initial root identity to UID 2000; root is not its normal prerequisite |

The relevant advantage is **ADB-authorized shell privileges**, not permission
escalation through `FakeContext`, reflection, native code, or the APK format.
For example, the [Android 13 Shell manifest](https://android.googlesource.com/platform/frameworks/base/+/android-13.0.0_r1/packages/Shell/AndroidManifest.xml)
requests `INTERNAL_SYSTEM_WINDOW`, `INJECT_EVENTS`, `ADD_TRUSTED_DISPLAY`, and
`ADD_ALWAYS_UNLOCKED_DISPLAY`. The [activity launch checks](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/services/core/java/com/android/server/wm/ActivityTaskSupervisor.java)
contain a privileged-caller path, and [display creation checks](https://android.googlesource.com/platform/frameworks/base/+/main/services/core/java/com/android/server/display/DisplayManagerService.java)
enforce permission for trusted displays. Exact grants and behavior vary by
Android release and OEM build; shell is not a universal compatibility guarantee.

Putting these wrappers into a regular APK would retain the regular application's
UID and permissions. Launching `app_process` from that APK would also retain its
identity. Wireless ADB can supply that authorization on the phone itself on Android 11+.
The implementation uses this user-authorized setup with a separate shell
helper; the installed APK retains its own permissions.

Scrcpy's general Android 5.0 minimum is not proof that its independent desktop
features work on every supported release. Its video path also encodes frames
for a host client; our adapter needs pixel payloads, so there is no reason to
add a video encoder/decoder round trip. The upstream [developer documentation](https://github.com/Genymobile/scrcpy/blob/master/doc/develop.md)
explains its shell process and hidden API wrappers; the [virtual-display documentation](https://github.com/Genymobile/scrcpy/blob/master/doc/virtual-display.md)
describes launching content and device-dependent launcher availability.
