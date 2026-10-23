# Android and Linux shared pipeline

The Linux `main` revision `f4181bb` is authoritative. The Android branch merges
that history in `ba80411`, including the UYVY snapshot pipeline and RGB565
removal. The earlier stash remains available as a backup; its old Linux
integration edits are superseded by the merged implementation. Its untracked
vendor review documents remain in the working tree.

## Common production sources

| Source | Linux and Android use |
| --- | --- |
| `ms912x_batch.c` | Current/previous damage batch construction, indexed interleaved span merging, area-dependent compaction and byte-budget fallback. Android no longer sorts its own replay union. |
| `ms912x_scan_runs.h` | The Linux changed-run detector. Android's bounded snapshot adapter calls it with exact word comparisons. |
| `ms912x_mode3.c`, `ms912x_mode3.h` | One validated pitched UYVY rectangle serializer, wire header and final/nonfinal trailer. Linux's wrapper preserves its success return convention and fixed kernel pitch; Android supplies its snapshot pitch and output capacity. |
| `ms912x_mode_catalog.h` | The single 29-timing Linux catalog, instantiated by KMS and the Android catalog adapter; EDID generation reads this same file. |
| `shared/ms912x_color_core.h` | Scalar color arithmetic already used by Linux and Android. Android's RGBX/BGRX converters retain the latest Linux branch's backend selectors and NEON load helper. |
| `shared/ms912x_edid_core.h` | The existing Linux checksum-guarded header repair, called from its connector wrapper and Android JNI. |

`ms912x_platform.h` supplies the small type/rectangle/sort environment needed by
the common sources. Kernel builds retain kernel types and DRM rectangles;
userspace builds use the C runtime. Kernel fixtures can provide their own type
environment. No kernel workers or USB interfaces are emulated inside the APK.

Android retains ImageReader ownership, JNI handle lifetime, its bounded mailbox,
USB consent and UsbManager writes. Its damage transaction adapter commits a
baseline only after the complete USB batch succeeds, retains current-only
history, and supplies the final idle replay. Those responsibilities differ from
DRM producer/workqueue ownership. It uses Linux's 8192 damage/16385 batch limits;
capacity exhaustion conservatively requests a full frame. Capture and monitor
refresh remain independent.

## Android 7 mirror build

The normal APK keeps minimum API 30. `-PmirrorOnly=true` builds minimum API 24
with a separate application ID, `io.github.ms912x.desktop.mirror`. It offers
consented phone-screen capture (and the Android app picker where available)
through the same native/USB pipeline. It omits independent-screen creation and
ADB controls, does not initialize ADB credentials, and packages no Rust ADB
library. It requires no Rust compiler to build. Its native ABIs are ARM32,
ARM64, x86 and x86_64; the desktop build remains ARM64/x86_64.

Compatibility guards cover window insets, foreground-service startup and
notification service types. Both builds target the current project API 36;
the lower minimum does not weaken the modern Android consent flow.

```sh
ANDROID_HOME=/mnt/data/LINUX/android-sdk \
ANDROID_SDK_ROOT=/mnt/data/LINUX/android-sdk \
  android/gradlew -p android -PmirrorOnly=true :app:assembleDebug :app:lintDebug

ANDROID_HOME=/mnt/data/LINUX/android-sdk \
  tools/android/test_android.sh --mirror-only --device-only DEVICE_SERIAL
```

The two configurations use the same Gradle output paths. Build them sequentially
and copy each APK before building the next. The final exports live under
`android/app/build/outputs/compat/`.

## Verification boundary

The Android 7/API 24 x86 emulator passes six focused JNI, USB-write,
ImageReader and mirror-activity groups. Its real Android MediaProjection consent
flow also runs: a recorded UI checkpoint has 166 accepted native frames and zero
rejections; Stop releases the capture session. The API 37 phone passes the six
mirror groups and all 14 desktop groups. API 24 USB hardware and ARM32 device
compatibility are not established by an emulator or an ABI build. Android 8
shares the API 24 floor but was not separately run.

A physical API 37 phone/adapter 1920x1080@60 pattern recording contains 599 card
samples in the final build, zero marker errors and zero static-bar changes, with no samples
excluded. This is a region-specific corruption check, not a display FPS or
whole-frame integrity measurement. The capture source is the persistent
UltraSemi card identity, excluding the KVM webcam. Built-in wireless ADB helper
bootstrap and a standalone third-party desktop remain incomplete.
