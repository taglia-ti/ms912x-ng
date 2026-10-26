# Android and Linux shared pipeline

Linux production algorithms are extracted into portable cores used by both
platforms. Kernel wrappers retain DRM ownership, workqueues and FPU sections;
Android wrappers retain ImageReader, JNI and UsbManager ownership.

## Common production sources

| Source | Linux and Android use |
| --- | --- |
| `ms912x_usb_ids.h` | One VID/PID catalog for kernel binding and Android discovery, including the `345f` USB3 devices. |
| `ms912x_protocol_values.h`, `shared/ms912x_control_core.h` | Register/command values, feature-report encoding and the Linux manual-block mode programming sequence. |
| `ms912x_capabilities.h`, `ms912x_limits.h` | SDRAM interpretation, frame dimensions and initial USB rate estimate. |
| `shared/ms912x_color_{neon,sse2,ssse3,avx2}_core.h` | Linux SIMD conversion arithmetic, with RGBX/BGRX channel order specialized without a swap buffer or per-pixel format branch. SSE2/SSSE3 share luma/chroma helpers. Kernel wrappers keep their existing FPU sections; userspace runtime dispatch checks optional x86 CPU/OS support. |
| `shared/ms912x_scan_backend.c`, `ms912x_scan_methods.h`, `ms912x_scan_sse.h`, `ms912x_scan_neon_core.h`, `ms912x_scan_avx_core.h`, `ms912x_scan_masked_template.h` | Linux scalar/SIMD row comparison and masked-block logic. Both platforms use one dispatcher for auto/scalar/memcmp/AVX/SSE2/NEON, backend availability, scalar fallback, mask policy and row batches. Kernel execution keeps the existing FPU wrappers with at most 16 sampled rows per section. |
| `shared/ms912x_timing_core.h`, `shared/ms912x_modes_core.h` | Linux timing validation, chip-signature discovery and custom flash timing decoding/loading. Android retains the stock catalog if optional custom timing discovery is unavailable. |
| `shared/ms912x_pacing_core.h`, `ms912x_batch.c` | USB rate averaging, queue drain-time estimates, hysteresis and bounded coalescing. |
| `ms912x_batch.c` | Current/previous damage batch construction, indexed interleaved span merging, area-dependent compaction and byte-budget fallback. Android no longer sorts its own replay union. |
| `ms912x_scan_runs.h`, `shared/ms912x_scan_rects_core.c` | The Linux changed-run detector and indexed rectangle builder. Android uses the same 16-pixel UYVY blocks and vertical unions before enforcing rectangle capacity; Linux retains its vector-growth wrapper. |
| `ms912x_mode3.c`, `ms912x_mode3.h` | One validated pitched UYVY rectangle serializer, wire header and final/nonfinal trailer. Linux's wrapper preserves its success return convention and fixed kernel pitch; Android supplies its snapshot pitch and output capacity. |
| `ms912x_mode_catalog.h` | The single 29-timing Linux catalog, instantiated by KMS and the Android catalog adapter; EDID generation reads this same file. |
| `shared/ms912x_color_core.h` | Scalar color arithmetic already used by Linux and Android. Android's RGBX/BGRX converters retain the latest Linux branch's backend selectors and NEON load helper. |
| `shared/ms912x_edid_core.h` | Linux byte-read fallback, header validation/repair and block checksum validation. Android caches validated blocks within a session and rereads EDID before programming a mode. |
| `shared/ms912x_draw_core.h` | The Linux UYVY damage-border routine, extracted from `ms912x_convert.c` and used by its existing wrapper and Android's encoded rectangle payloads. No duplicate painter or scan-baseline modification. |

`ms912x_platform.h` supplies the small type/rectangle/sort environment needed by
the common sources. Kernel builds retain kernel types and DRM rectangles;
userspace builds use the C runtime. Kernel fixtures can provide their own type
environment. No kernel workers or USB interfaces are emulated inside the APK.

Android retains ImageReader ownership, JNI handle lifetime, its bounded mailbox,
USB consent and UsbManager writes. Its damage transaction adapter commits a
baseline only after the complete USB batch succeeds, retains current-only
history, and supplies the final idle replay. Those responsibilities differ from
DRM producer/workqueue ownership. It uses Linux's 8192 damage/16385 batch limits.
Rectangle-capacity exhaustion uses the Linux bounding-union fallback;
packet-capacity exhaustion can request a full frame. Capture and monitor
refresh remain independent.

## Scanner backends

The Android transaction adapter defaults to `MS912X_SCAN_AUTO`. Native callers can
select a supported backend through `ms912x_frame_damage_set_scan_backend()`;
changes during an outstanding submission are rejected. The standalone scanner
also accepts an explicit backend through `ms912x_core_scan_words_backend()`.
Unsupported requests at the dispatcher level fall back to scalar, preserving the
requested tolerance. Linux parameter names and numeric values remain stable.

`memcmp` uses the original Linux exact comparator, ignores tolerance and refines
runs in 128-byte blocks (64 UYVY pixels). Scalar and SIMD use masked comparisons
and 16-pixel UYVY runs. These backend geometries are intentionally different.
Shared differential fixtures verify row metadata, selective baseline copying,
padded rows, tails and batches crossing 16 rows against the Linux primitives.
Transaction fixtures exercise every supported backend through encoding, successful
completion, idle replay and unchanged-frame suppression.

## Android pacing

Successful complete USB batches update the same running rate estimate as Linux.
The next encoded batch's actual byte count supplies its queued drain-time estimate;
this is a transport estimate, not monitor FPS. The shared hysteresis and coalescing
policy can defer a partial update for half a refresh interval. Publications can
replace its snapshot but never renew its deadline. Deferral cancels only the
unsent transaction, leaving the baseline and previous-damage history intact.
Full-frame initialization/repair and idle replay bypass this wait. ImageReader
notifications wake the sender, with a timed wait for the remaining deadline.

The full DRM EDID parser remains a kernel dependency. Android's established,
standard and CTA advertisement parsing stays in its platform adapter; the Linux
adapter-specific EDID I/O and custom timing algorithms are shared.

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
and copy each APK before building the next. Any compatibility exports under
`android/app/build/outputs/compat/` are generated artifacts and may be cleaned.

## Shared-core verification

Each extraction was built and passed all 14 instrumented groups on the API 30
x86_64 emulator before its commit. The final Mirror build compiles all four ABIs
and passes six groups on the API 24 x86 emulator, including paced JNI submissions. Native ASan/UBSan fixtures also cover the shared
scanner and pacing, including all available backend comparisons, fixed deadlines,
newest-snapshot replacement, USB pressure, full refreshes and idle replay. Linux
`make tests` and module builds pass. ARM64 QEMU fixtures cover Linux and Android
conversion/scanning. Custom flash timing fixtures cover both chip families and
mixed invalid/valid records. Emulator USB responses are fixtures; these checks do
not establish physical USB timing or monitor presentation.

A warm 1920x1080 scanner microbenchmark on the API 30 emulator compared the
pre-SIMD adapter (`395eeec`) with the shared backend (median of seven samples,
four scans per sample). Rectangle vectors and encoded byte costs matched exactly.
These are CPU scan times, not display or USB throughput measurements:

| Changes | Previous scan (us) | Shared SIMD scan (us) |
| --- | ---: | ---: |
| None | 286.463 | 60.655 |
| 64x64 patch | 298.285 | 62.965 |
| Entire frame | 133.097 | 188.428 |
| Scattered pairs | 301.118 | 114.215 |

The coarse comparison adds work when every word changes, while reducing work on
unchanged/sparse frames. The emulator result does not establish ARM device speed.

## Earlier device verification

The following observations predate the shared-core extractions above.

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
UltraSemi card identity, excluding the KVM webcam. At that checkpoint, built-in
wireless ADB and the third-party desktop were incomplete; later
[P4](android-shell-capture-p4.md) and [P5](android-created-screen.md) record their
implementation and physical acceptance.
