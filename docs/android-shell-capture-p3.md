# Service-owned capture and USB output: P3

**Historical P3 checkpoint.** The PC-assisted procedure below records the
acceptance setup used at that phase. The app's current setup can start the
helper on the phone; see the [Android README](../android/README.md).

P3 connects the authenticated PC-assisted shell helper to the normal screencast
button. The Desktop APK can output an existing secondary display through the
existing ImageReader/JNI/Linux-derived frame pipeline and USB transport.
P4 now starts the helper through the embedded same-phone ADB client; see
[the P4 implementation and use](android-shell-capture-p4.md).

## Use

1. Connect the adapter and allow USB access. Opening the source/output dropdowns
   refreshes USB detection, EDID modes and the connected helper's inventory.
2. Open Debug and choose **Prepare ADB helper**. On the PC run:

   ```sh
   python3 tools/android/launch_helper.py --serial DEVICE --apk PATH_TO_INSTALLED_APK
   ```

   Keep its stream open. Enrollment still requires the matching bundled helper,
   actual shell UID 2000, the single-use challenge and the verified protocol.
3. Return to Screencast, select the existing screen and an advertised output
   resolution/refresh. Source/output labels include their aspect ratios. Helper
   rotation is available on API 30+; public virtual-screen rotation remains
   gated to API 36+. Choose **Start screencast**.
4. Back opens Home on the interface's display. Recreating/rebinding the Activity
   keeps the service-owned capture and USB session. Use the same button or the
   foreground notification's **Stop** action to stop.

Normal helper output retires its helper process at Stop. Prepare and launch a
fresh connection for another output session. Debug capture-only previews retain
an idle connection for repeated tests, and stop when their interface backgrounds.
Allow notification permission on API 33+ to expose Stop in the notification
drawer; declining still permits output and the in-app Stop action.

The original display remains owned by scrcpy or its creator. This feature does
not create a receiver route for YouTube/Cast, add MediaRouter, transfer pixels
over Binder/ADB, or change protected-content capture rules.

## Ownership

`CaptureSessionService` is the started and bound owner of one
`SessionCoordinator` and one application-context `UsbOutputController`.
`MainActivity` and `ScreencastController` observe immutable snapshots, choose
settings/adapters, request Android permissions and send commands. Their teardown
unbinds the interface without stopping an active USB foreground session.
Adapter-choice dialogs belong to the observing Activity; detaching cancels their
pending choice and invalidates late callbacks.

The common `CaptureSource` contract separates producer stop from reader release:

- `OwnedCaptureSource` owns the existing virtual screen and test Presentation.
- `ProjectionCaptureSource` binds the consented `ProjectionCaptureService`.
  Only that service uses `mediaProjection` foreground ownership. Cancelled binds
  cannot create a late producer; initial state from an old binding cannot be
  treated as a result of the new command.
- `HelperCaptureSource` owns the verified helper connection, current inventory,
  Surface attachment, release acknowledgement and Binder death reconciliation.
  Normal output disables its diagnostic encoder so USB is the only batch owner.

The coordinator validates USB presence/permission, EDID mode and helper source
identity before foreground promotion. Actual USB sessions use `connectedDevice`
and `FOREGROUND_SERVICE_CONNECTED_DEVICE`; a shell session never manufactures a
MediaProjection token. USB starts after the first accepted native image, and
`STREAMING` follows the first completed USB batch. Metadata/source removal,
startup failure, detach, Stop and notification Stop enter the same teardown.

Stop initiates USB drain and producer release independently. Both must finish
before the reader/native capture closes on a cleanup worker. Helper output then
waits for confirmed helper retirement before ending foreground ownership. New
starts remain blocked throughout cleanup. Generation/owner checks reject late
callbacks. The shared C conversion, scan, encoding, pacing, replay and USB batch
completion algorithms remain unchanged.

## Validation

The exact APK installed on the API 37 phone was saved at
`android/app/build/outputs/apk/debug/app-debug-validated.apk`.
See [the validation log](android-validation.md) for digests and sample counts.
Desktop instrumentation passes 17 groups on API 30/36; Mirror passes six groups
on API 24/30/36. New fixtures cover permission-before-foreground, cancellation
before an image, late callbacks, concurrent stops, reader lifetime, identity
reuse and helper-retirement-before-foreground teardown. Fixture foreground roles
and USB responses are explicitly injected and do not establish Android privilege.

Real phone acceptance exercises the normal Start button, actual USB permission,
EDID, helper UID 2000, Home, Activity recreation/rebind and notification Stop.
The source and reader identities survive UI changes; Stop removes only our
mirror, closes the reader, retires the helper and preserves the source. The same
lifecycle passes with real Android MediaProjection consent and USB output.

```sh
python3 tools/android/test_session_output.py \
  --serial DEVICE --source-id TEST_DISPLAY_ID \
  --source-width 2560 --source-height 1080 --apk PATH_TO_INSTALLED_APK \
  --report android/app/build/reports/helper-p3-hardware
```

The source must be dedicated to testing: the harness launches its known geometry
Activity there. The physical fixture explicitly launches the interface on phone
display 0. HDMI uses only UltraSemi's persistent by-id path, YUYV capture and a
lossless recording. `check_hdmi_geometry.py` checks labelled corner/color regions,
a circle, a changing cyan/magenta marker and fitted black bars. Each four-second
recording excludes the first 15 UVC samples for stream startup, and reports all
remaining samples/errors. One explicit retry is available for transient source
notifications; both recordings and results are retained. Samples are capture-card
samples, not display FPS. Phone-mirror recordings have no geometry oracle and
are recorded as lifecycle/USB evidence separately.

PC-assisted capture is validated for this phone/adapter/HDMI setup. Embedded
bootstrap, pairing/TLS reconnection and helper transfer/start were the remaining
P4 gates at this checkpoint. They now pass separately in the P4 validation log.

References: [connected-device foreground requirements](https://developer.android.com/develop/background-work/services/fgs/service-types#connected-device),
[notification permission](https://developer.android.com/develop/ui/compose/notifications/notification-permission),
[bound-service lifetime](https://developer.android.com/develop/background-work/services/bound-services).
