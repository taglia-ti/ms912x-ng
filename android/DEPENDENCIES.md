# Android dependency decisions

| Component | Pinned choice | Status |
|---|---|---|
| Android Gradle Plugin | 8.13.2 | Selected for API 36; official minimum Gradle is 8.13 and JDK 17. |
| Gradle | 8.13 | Pinned in `gradle/wrapper/gradle-wrapper.properties`. |
| Android SDK | API 30 and 36; Build Tools 35.0.0 | Installed at `/mnt/data/LINUX/android-sdk`; Gradle may install compatible components such as CMake there. |
| NDK | 30.0.16248370 | Installed under the custom SDK root; ARM64 and x86_64 builds pass. |
| Embedded wireless ADB | In-tree C11 client; Mbed TLS 3.6.7 and Monocypher 4.0.2 | Built by NDK/CMake for ARM64 and x86_64; no Rust/Cargo dependency. |
| USB transport | Android framework USB APIs | Implemented permission/control/bulk path with injectable UsbHost; fixtures run without physical USB. |

The ADB client supports Android 11 pairing/TLS, persistent app-owned RSA identity,
classic local TCP authorization, Shell v2 and Sync helper upload. Discovery
remains in Android NSD. Protocol, crypto, transport, JNI callbacks and retained
helper ownership have separate modules in [`adb-c`](adb-c/README.md).

Mbed TLS is pinned to its 3.6.7 release archive with SHA-256 verification. This
project selects its GPL-2.0-or-later licensing option under GPL version 2.
Monocypher 4.0.2's unchanged C source/header are vendored under BSD-2-Clause;
source hashes and upgrade requirements are recorded in the module guide.
Desktop APK assets include the third-party license notices. Neither dependency
requires a compiler beyond the existing C toolchain. The mirror-only APK skips
the ADB target and its dependency download entirely.

CMake downloads the verified Mbed TLS release on first configure and caches it.
For an offline build, set `FETCHCONTENT_SOURCE_DIR_MBEDTLS` to an extracted,
verified release (`MBEDTLS_SOURCE_DIR` for the host test script). No downloaded
executable, Rust toolchain or external ADB server is part of the app build.

The previous DroidMux experiment was pinned to `9adeb625` and used MIT terms.
Its Rust source, Cargo lockfile and Gradle task have been removed. Existing RSA
credentials remain readable by the C implementation. Host peer fixtures and
Android instrumentation pass; a real API 37 phone also passes saved-key TLS
reconnect, helper upload/start/stop and app-UI reconnection. A new pairing exchange
against real adbd remains separate acceptance from the pairing peer fixture.

The version pairing is based on Google's
[AGP 8.13 release notes](https://developer.android.com/build/releases/agp-8-13-0-release-notes),
which list Gradle 8.13, JDK 17, and support through API 36.1. This project uses
API 36. The current local build was verified with Java 21 and the custom SDK
root. Google's [NDK downloads page](https://developer.android.com/ndk/downloads)
lists r30 / 30.0.16248370 as an LTS release.

Android fixture tests use platform Instrumentation and in-tree fakes, without
adding JUnit, Mockito, Robolectric, or other test dependencies. See
[the fixture/path map](tests/README.md).
