# Existing-display capture geometry

The PC-assisted desktop helper now supports aspect-preserving fitting and
clockwise output rotation at 0°, 90°, 180° and 270°. Source size/orientation
changes update the capture while keeping the APK's ImageReader and output
dimensions fixed. Removal or replacement of the original source stops the session.
The helper remains a Debug preview; USB-session integration and embedded ADB
startup are separate, unfinished milestones.

## Implementation

- `CaptureTransform` computes centered content bounds from logical source size,
  output size and output rotation. For 2560×1080 into 1920×1080 at 0°, the content
  rectangle is `(0,135)–(1920,945)`.
- Unrotated output retains direct compositor fitting. Rotated output uses
  `FrameRenderer`: the mirror targets a SurfaceTexture, then an EGL/GLES pass
  clears the destination black and samples the external texture into the fitted
  viewport. The SurfaceTexture transform is applied to texture coordinates.
  There is no CPU readback or second RGB/UYVY conversion in the helper.
- The existing APK reader, native converter, damage scanner and encoder remain
  unchanged. Rendering follows incoming frames without an application frame cap.
- `DisplayCaptureBackend` owns only the helper's mirror. The API 30 layer stack
  already uses upright logical coordinates, so its projection does not add the
  source's display rotation again. When intermediate dimensions change, its
  virtual framebuffer is recreated. DisplayManager mirrors are also recreated
  on source geometry changes to discard stale framework projections. Both paths
  retain the original source and APK reader.
- IPC protocol 2 added source information, effective content bounds and output
  rotation to capture results, plus a geometry-change callback. Old protocol or
  helper digest mismatches remain rejected at enrollment.

The current protocol is version 3, adding asynchronous inventory/control and
cancellation/lifetime hardening; see [P2](android-shell-capture-p2.md).

Prepare/connect the helper using [the setup guide](android-shell-capture-p0.md).
Debug's **Helper output rotation** selects the transform for the next preview.
It is independent of the main page's API-dependent virtual-screen rotation
control. Debug reports the active source size, fixed output size, content bounds
and backend. Rotation selection is locked while the helper preview runs.

## Pixel acceptance

`HelperGeometryFixtureTests` is an optional instrumentation mode, separate from
the adapter-free fixture suite. A separate test-APK UID draws known RGB patches,
four distinct corners and white edges on a dedicated display. The test samples
received RGBA images before native conversion and checks the metadata against an
independent geometry oracle. It checks solid regions, edges and black bars with
a per-channel tolerance of 12/255; this is not an exhaustive whole-image comparison.

Build/install both desktop APKs, then create a **dedicated temporary** source
display, for example with the already-installed scrcpy. Never use a working
desktop for the optional source-change test:

```sh
python3 tools/android/test_helper_geometry.py \
  --serial DEVICE --source-id TEMPORARY_DISPLAY_ID \
  --exercise-source-changes \
  --report android/app/build/reports/helper-geometry-DEVICE
```

The script launches the known fixture after instrumentation starts. The default
matrix covers four output sizes (1920×1080, 640×480, 1080×1920 and 1000×1000) and
four output rotations. `--exercise-source-changes` adds two live capture sessions
at 640×480, with output rotations 0°/90°. Each session resizes the temporary
source to 1600×1200, rotates it to logical 1200×1600, then restores it. Only the
test driver changes this temporary source; the helper never resizes the source.
The fixture is brought fullscreen after changes to keep system taskbars outside
its known pixel pattern.

Acceptance passes on the API 30 emulator and API 37 phone: 18 capture sessions
and six live source transitions per target. Matching release acknowledgements,
source identity preservation and absence of helper mirrors in DisplayManager
and SurfaceFlinger are also checked. Reports contain build/artifact hashes,
per-case results and display inventories. The rotated preview's native encoding,
repeat stop and helper-death flow have a separate UI test:

```sh
python3 tools/android/test_helper_capture.py \
  --serial DEVICE --source-id TEMPORARY_DISPLAY_ID --rotation 1 \
  --report android/app/build/reports/helper-p1-ui-DEVICE
```

See [recorded validation](android-validation.md) for exact artifacts and limits.
There is no new USB/HDMI, protected-content or standalone pairing acceptance claim.
