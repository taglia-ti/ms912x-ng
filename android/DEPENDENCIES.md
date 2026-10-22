# Android dependency decisions

| Component | Pinned choice | Status |
|---|---|---|
| Android Gradle Plugin | 8.13.2 | Selected for API 36; official minimum Gradle is 8.13 and JDK 17. |
| Gradle | 8.13 | Pinned in `gradle/wrapper/gradle-wrapper.properties`. |
| Android SDK | API 30 and 36; Build Tools 35.0.0 | Installed at `/mnt/data/LINUX/android-sdk`; Gradle may install compatible components such as CMake there. |
| NDK | 30.0.16248370 | Installed under the custom SDK root; ARM64 and x86_64 builds pass. |
| Embedded wireless ADB | None selected | **Open A00 gate.** DroidMux is the leading candidate for a bounded Android-target spike; it is not linked into the APK. |
| USB transport | None | Planned Android framework USB permission plus a reviewed native backend; not started. |

The ADB provider must support Android 11 wireless pairing, TLS connection,
persistent app-owned identity, shell streams, and helper transfer. Record the
exact source revision, license, transitive notices, and on-device pairing
result before inclusion. In particular, do not import `libadb-android` until
its GPL-3.0-or-later / Apache-2.0 choices have been reviewed against this
repository's GPL-2.0-only driver files. No ADB dependency is declared or
vendored by this increment; Gradle resolves the pinned Android build plugins
when the wrapper runs.

## A00 embedded ADB evaluation

DroidMux is the leading Rust candidate because its public facade exposes
Android 11+ pairing/TLS, TCP, Shell, and Sync, and its dual license lets this
project select the MIT terms. Its default features include `pairing`, `tcp`,
`shell`, and `sync`; do not enable `control`, which brings a scrcpy-server
component and video dependencies. Keep discovery in Android's existing NSD
wrapper and pass the resolved pairing/connection endpoints to the provider.

The candidate is still experimental: its README calls the API version `0.1.0`
and unstable, GitHub shows only 11 commits, and its published validation does
not establish an Android NDK build or an on-device Android-host pairing run.
The current upstream `main` resolved to `9adeb6258166e9b88808bc6e67845c73763f070c`
on 2026-10-22. If the candidate is evaluated, pin that revision and choose
the MIT license option; do not use a floating branch in the app. The spike
must first build both `arm64-v8a` and `x86_64` with the pinned NDK, exercise
its offline pairing/protocol tests, and report its full locked dependency
license inventory. Only after that should it become an APK dependency and
move to API 30 pairing validation.

The alternative `libadb-android` documents Android wireless pairing and shell,
but offers GPL-3.0-or-later or Apache-2.0 terms. Apache-2.0 is not compatible
with this repository's GPL-2.0-only code when combined into the same app, and
the repository also calls out an LGPL dependency concern. It remains excluded
unless a compatible packaging and license arrangement is established.

`muka_rust_adb` is an MIT-licensed Rust ADB executable, but its documented
product is a host-side `adb` CLI/server replacement, not an embeddable Android
library; its repository also has a very small public history. It is not a
better fit for this APK than the DroidMux spike.

The version pairing is based on Google's
[AGP 8.13 release notes](https://developer.android.com/build/releases/agp-8-13-0-release-notes),
which list Gradle 8.13, JDK 17, and support through API 36.1. This project uses
API 36. The current local build was verified with Java 21 and the custom SDK
root. Google's [NDK downloads page](https://developer.android.com/ndk/downloads)
lists r30 / 30.0.16248370 as an LTS release.
