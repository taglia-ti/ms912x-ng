# Android capture and conversion review

Date: 2026-10-22. Build: `0.3.0-rgbx-test` (version code 3).
This is a review of that build; the [shared pipeline](android-linux-reuse.md)
and [current test map](../android/tests/README.md) describe subsequent work.

## Changes and responsibility boundaries

The Android path now converts RGBX bytes directly into UYVY before scanning.
It uses the existing `fast_chroma` Q16 coefficients, chroma pair averaging,
and truncation. There is no intermediate RGBX-to-BGRX frame. Alpha/X bytes
are ignored. Byte order names describe memory bytes, independently of the
CPU's integer endianness.

| Component | Responsibility |
|---|---|
| `shared/ms912x_rgbx_convert.[ch]` | Validate source/destination spans and select a row converter |
| `shared/ms912x_rgbx_internal.h` | Inline scalar pixel-pair arithmetic and unrolled fallback |
| `shared/ms912x_rgbx_neon.c` | ARM NEON eight-pixel worker, four-pixel vector tail, two-pixel scalar tail |
| `shared/ms912x_rgbx_sse2.c` | SSE2 four-pixel worker, eight-pixel unroll, two-pixel scalar tail |
| `shared/ms912x_rgbx_ssse3.c`, `shared/ms912x_rgbx_avx2.c` | Runtime-dispatched x86 row workers using the same channel-specialized SIMD cores as Linux, with bounded vector/scalar tails |
| `shared/ms912x_frame_damage.[ch]` | Completed baseline, damage scan, packet capacity, encoding, and transfer acknowledgement |
| `shared/ms912x_edid_core.h` | Linux/Android checksum-guarded base-header repair |
| `android/app/src/main/cpp/frame_pipeline.[ch]` | Three owned UYVY slots, frame replacement, generations, and submission pinning |
| `android/app/src/main/cpp/jni_capture.c` | JNI argument translation and native-handle lifetime |
| `CaptureController.java` | ImageReader ownership and capture callback lifetime |
| `FrameSubmission.java` | Exactly-once commit or abort for an encoded batch |
| `UsbOutputController.java` | USB worker, cancellation, and transfer completion |
| `ProjectionCaptureService.java` | Foreground capture lifecycle and typed state notifications |

The new shared modules have no JNI, Android, USB, or kernel dependencies.
They reuse the existing color formula, scanner, and mode-3 encoder. The Linux
build does not link the new conversion or damage modules. Existing Linux
sources, build files, and previously shared C files were preserved during
this review, including edits that were already pending when it began.

The subsequent phone review extracts the Linux connector's existing EDID repair
into a common inline helper called by Linux and Android JNI. No Linux transport
or damage behavior changes. Android's new damage module remains independent of
the kernel batch scheduler while calling the same scan and packet algorithms.

## Conversion contract

`ms912x_rgbx_prepare()` accepts an explicit RGBX/BGRX view with buffer size,
offset, crop, pixel stride, and row stride. It checks overflow and complete
row/crop bounds before returning a prepared row worker. The source must remain
alive until `ms912x_rgbx_execute()` returns. Execution validates destination
capacity/stride and rejects overlapping spans before writing any pixel.
Output is always the wire byte sequence U, Y0, V, Y1. Even widths are required.
Unaligned packed input is supported; non-four-byte pixel strides use scalar
conversion. The prepared plan can be reused while its source layout remains
valid; conversion does not allocate.

ARM64 selects NEON. x86 runtime dispatch selects AVX2, SSSE3 or SSE2 according
to CPU/OS support; optional instructions stay inside target-specific functions.
Other builds retain scalar code. Explicit scalar/backend selection is available
for differential tests and unsupported backends are rejected before conversion.

The Linux SSSE3/AVX2 cores now also support Android RGBX by specializing channel
coefficients, with no swap pass. Linux calls the shared kernels with constant
red byte index 2; Android RGBX uses index 0. SSE2 and SSSE3 share their 128-bit
luma/chroma arithmetic. Strides, buffer validation and OS SIMD ownership remain
in the respective wrappers. The Linux kernel's SSE2/SSSE3/AVX2 `.text` sections
are byte-for-byte identical before and after this extraction on the tested build.

Small helpers are always inlined so the channel order and coefficient choices
are constants inside each row worker. The pixel arithmetic has no branches
based on RGB values and needs no clamp. Validation, dispatch, row loops, and
width tails still use branches. NEON uses widening multiply/accumulate,
pairwise channel sums, and interleaved LD4/ST2. SSE2 uses packed integer
multiply/add and exact Q16 shifts, matching the Linux arithmetic.

The Android build compiles conversion workers with `-O3`, and scanning,
encoding, damage tracking, and the mailbox with `-O2`, including debug APKs.
These flags apply only to the Android CMake target.

## Ownership and ordering corrections

Previously, a global JNI registry mutex covered conversion and packet work.
It now covers only lookup/reference counts. Destroy unlinks the handle, waits
for its current users, then frees it. Different pipelines can run independently.

Each mailbox has a short state mutex and a consumer mutex. When both are
needed the consumer mutex comes first. Pixel conversion holds neither. A
producer reserves its sequence before converting; a slower older conversion
cannot replace an already-published newer frame. Reconfiguration refuses
filling, active, or submitted slots.

Previously, encoding advanced the baseline before the USB worker sent the
packet. An encoded batch now pins its immutable source slot and returns a
sequence token. Only a successful whole-batch transfer commits the transmitted
rectangles into the baseline. Closing an uncommitted Java submission aborts
it; failure forces a full-frame repair. Stale or duplicate acknowledgements
are rejected. Invalidating an in-flight batch prevents its old completion
from restoring an invalid baseline.

The phone review adds previous-damage replay to the portable damage owner.
An outgoing batch covers current damage plus the previous successful frame's
current-only damage, always reading the latest pixels. History never accumulates
the outgoing union. A pending idle repair pins the completed baseline and drains
history once, so a static scene does not wait for another captured image.
Insufficient packet capacity for combined rectangles falls back to full-frame
coverage. Completion commits only transmitted coverage; abort/reset invalidates
both the baseline and history. Submission tokens are independent of producer
sequence numbers so an idle batch cannot conflict with a producer still converting.

ImageReader shutdown detaches the listener, drains the image thread, then
releases the reader and native session. Typed service state replaces parsing
status-message text. An ACTIVE notification can start pending USB output even
when service binding completed before the capture session became ready.

## Reproduce validation

From the repository root:

```sh
tools/android/test_rgbx_conversion.sh
tools/android/test_rgbx_conversion.sh --bench
tools/android/test_frame_pipeline.sh
SANITIZE=undefined tools/android/test_rgbx_conversion.sh
SANITIZE=undefined tools/android/test_frame_pipeline.sh
ANDROID_HOME=/mnt/data/LINUX/android-sdk \
  tools/android/test_native_android.sh emulator-5554
```

The RGBX script also executes ARM64 NEON under QEMU when the cross compiler
and `qemu-aarch64` are installed. The Android sanitizer script builds the same
C workers with the pinned NDK and runs ASan/UBSan on an explicitly selected
ARM64 or x86_64 device; it disables leak detection because this check targets
memory access and undefined behavior on Android.

From `android/`, with both SDK variables pointing to the installed SDK:

```sh
./gradlew :app:assembleDebug :app:assembleDebugAndroidTest :app:lintDebug
adb -s emulator-5554 install -r app/build/outputs/apk/debug/app-debug.apk
adb -s emulator-5554 install -r app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
adb -s emulator-5554 shell am instrument -w -r \
  io.github.ms912x.desktop.test/io.github.ms912x.desktop.CaptureNativeTestRunner
```

Conversion tests compare 3,024 cases per architecture against the existing
shared scalar formula. They cover both byte orders, vector tails, unaligned
data, crops, padded rows, unusual pixel strides, alpha independence, guards,
overlap rejection, and checked arithmetic. Submission tests cover full and
pair-aligned partial packets, unchanged frames, abort/full repair, stale tokens,
in-flight invalidation, capacity errors, reconfiguration, and concurrent
producers. Java tests exercise commit/abort when transfer throws. Instrumentation
uses the production JNI library and tests independent handles and concurrent
close.

Measured results, APK identity, and API 30 capture results are recorded in
[android-validation.md](android-validation.md). Host conversion throughput
excludes capture, scanning, USB, and display presentation. QEMU validates
ARM64 correctness; it does not measure phone NEON performance. Physical 640×480
USB output and replay are now checked on one ARM64 phone with an HDMI recording;
other modes/devices and built-in independent desktop startup remain acceptance
gates. A subsequent PC-assisted test captures another UID on the APK-owned
display at 1080p; see [the experiment](android-virtual-display-test.md).
