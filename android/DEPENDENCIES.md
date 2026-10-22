# Android dependency decisions

| Component | Pinned choice | Status |
|---|---|---|
| Android Gradle Plugin | 8.13.2 | Selected for API 36; official minimum Gradle is 8.13 and JDK 17. |
| Gradle | 8.13 | Pinned in `gradle/wrapper/gradle-wrapper.properties`. |
| Android SDK | API 30 and 36; Build Tools 35.0.0 | Installed at `/mnt/data/LINUX/android-sdk`; Gradle may install compatible components such as CMake there. |
| NDK | 30.0.16248370 | Installed under the custom SDK root; ARM64 and x86_64 builds pass. |
| Embedded wireless ADB | None | **Open A00 gate.** Pairing/TLS/shell/file transfer and license compatibility must be resolved before adding a provider. |
| USB transport | None | Planned Android framework USB permission plus a reviewed native backend; not started. |

The ADB provider must support Android 11 wireless pairing, TLS connection,
persistent app-owned identity, shell streams, and helper transfer. Record the
exact source revision, license, transitive notices, and on-device pairing
result before inclusion. In particular, do not import `libadb-android` until
its GPL-3.0-or-later / Apache-2.0 choices have been reviewed against this
repository's GPL-2.0-only driver files. No ADB dependency is declared or
vendored by this increment; Gradle resolves the pinned Android build plugins
when the wrapper runs.

One additional candidate found during implementation is
[DroidMux](https://github.com/poapoauu/DroidMux): its current README advertises
Android 11 pairing/TLS, shell, sync transfer, mDNS, and an `MIT OR Apache-2.0`
license. The `control` feature that bundles a scrcpy server is optional and
would stay disabled. It is currently version 0.1.0 with a small public commit
history, and this repository has not checked its Android arm64 build,
transitive dependency notices, or actual pairing against a phone. Treat it as
an evaluation candidate only; do not depend on it until those checks pass and
the source revision is pinned.

The version pairing is based on Google's
[AGP 8.13 release notes](https://developer.android.com/build/releases/agp-8-13-0-release-notes),
which list Gradle 8.13, JDK 17, and support through API 36.1. This project uses
API 36. The current local build was verified with Java 21 and the custom SDK
root. Google's [NDK downloads page](https://developer.android.com/ndk/downloads)
lists r30 / 30.0.16248370 as an LTS release.
