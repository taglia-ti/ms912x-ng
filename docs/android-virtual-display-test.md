# Shell-assisted virtual-display test

Date: 2026-10-23. APK: `0.9.0-display-test`.

The APK creates an app-owned virtual display and keeps its ImageReader, native
conversion/scanning, and USB output in the existing process. PC ADB launches a
different application onto that display and injects input. No frame data travels
through ADB, no MediaProjection is used for this source, and no root is used.
This tests the display/output path; built-in on-device ADB bootstrap remains open.

## Observed results

| Check | Physical Samsung API 37 | API 30 emulator |
|---|---|---|
| APK-created private display | 1920×1080 at requested 50/60 Hz | 640×480 at Android-managed 60 Hz |
| Driver / independent fixture UID | 10464 / 10465 | 10130 / 10131 |
| Separate app launched through shell | Settings and standalone fixture | Standalone fixture |
| Native capture of independent content | Physical HDMI recording | 190 accepted images, 0 rejected at checkpoint |
| Pointer / key receipt | HDMI shows `Pointer test: 1`, `KEYCODE_A` | Accessibility reports `pointer=1; key=KEYCODE_A` |
| Phone/default display | Driver controls while HDMI shows the other app | Driver controls while fixture draws on display 4 |

Physical 1080p60 test-scene recordings contain 599 card samples each, with no
marker trails or static-bar ROI changes. The 1080p50 recording has one malformed
startup buffer after changing the capture-card format; it is retained in the raw
recording. Its remaining 299 samples pass after explicitly excluding that first
buffer. No samples are excluded from the 1080p60 pattern checks. The card's
60 Hz sampling is not the rate of fresh Android frames or USB updates.

Consented phone mirroring also reaches HDMI at 1920×1080/60, with the portrait
screen correctly letterboxed. A checkpoint reports 182 native copies accepted,
0 rejected and 305 completed USB updates; these are cumulative counts.

## Reproduce on a phone with an adapter

Install the driver APK first, then the separate fixture APK from
`android/app/build/outputs/phone-test/`. Both are debug-signed. The fixture is an
androidTest-only exported activity with its own UID, no launcher entry, and no
dependency on the driver's classes when run standalone. It does not request
`allowEmbedded` or run under the driver's UID.

1. In the driver, select **Create a new screen**, read the monitor EDID, select
   1920×1080 and explicitly choose 60 Hz. The first listed 1080p timing is 50 Hz.
2. Start sharing. Confirm the live pattern and its 1920×1080 label on HDMI.
3. Open the gear menu, choose **Debug**, then **Hide test scene**. This dismisses the
   Presentation without replacing the virtual display or capture/USB workers.
   Read the display ID from the diagnostic. Never assume an old ID is still valid.
4. Launch the separate fixture with PC ADB. Replace `DEVICE` and `DISPLAY_ID`
   below with the selected phone and the current app-owned display ID:

```sh
adb -s DEVICE shell am start --display DISPLAY_ID -f 0x18000000 \
  -n io.github.ms912x.desktop.test/io.github.ms912x.desktop.ExternalDisplayFixtureActivity
```

Wait until the fixture is visible and its clock is advancing. At 1920×1080,
the following commands tap the green test button and send A to that display:

```sh
adb -s DEVICE shell input -d DISPLAY_ID tap 960 648
adb -s DEVICE shell input -d DISPLAY_ID keyevent KEYCODE_A
```

The fixture should show `Pointer test: 1` and `Key test: KEYCODE_A`. Its UID
must differ from the driver UID. The physical phone should still show the
driver's controls. At the emulator's 640×480 fallback size, use tap coordinates
320,288 instead. Wait for first draw before tapping: a tap during launch can
arrive before a window exists.

An immediate first tap was sometimes lost even after `am start -W` returned.
Later taps worked and their receipt was verified separately. Waiting for first
draw alone therefore does not establish input readiness; the future launcher
must handle window/focus readiness before forwarding interactive input. The
final recording verifies a positive pointer count after two tap attempts, not
one-to-one delivery during startup.

Finish the fixture before stopping the display:

```sh
adb -s DEVICE shell input -d DISPLAY_ID keyevent KEYCODE_BACK
```

Then press **Show test scene** to verify the same display resumes its pattern,
or **Stop sharing** to release it. The scene button is disabled outside an owned
screen session. Stopping also clears its running-screen diagnostic. Launching
Settings with its own task/embedding policy can leave a task pending or move it
to the phone when a display closes; use the isolated fixture for repeatable tests
and clean up only tasks created by the experiment.

For capture-only API 30 testing, select the new screen and use **Test capture
without USB output**, then hide the scene. No adapter is needed, so output stays
at the 640×480 diagnostic fallback. Accessibility exposes the fixture's UID,
display, pointer count and last key for verification without an HDMI card.

## Interpretation and next implementation step

The normal APK's Settings launch was denied on the physical phone. Shell launch
and input worked on the APK-owned private display on both tested Android builds.
The first shell launch while the Presentation was visible remained pending;
after dismissing that window, independent activities resumed and drew pixels.
The diagnostic toggle therefore keeps window visibility separate from capture
and transport lifetime.

This supports a simpler initial desktop backend: retain the APK-owned display
and all current pixel/output code, and use a shell-privileged launcher/input
controller through the existing embedded ADB bridge. The experiment did not
require Binder Surface transfer or a shell-created display. Integrate and verify
real on-device pairing/TLS/shell before enabling this as a normal APK flow.
Keep the planned privileged display backend available for existing-screen capture
and features that require trusted-display permissions; this experiment does not
establish those capabilities or arbitrary app compatibility.

No vendor Android or scrcpy implementation code is imported. Linux and native
pixel/output sources are unchanged by this diagnostic/UI test.

## Evidence

Phone reports are under `android/app/build/reports/phone-sm-s928b/`:

- `59-pattern-1080p60.mkv`, `61-1080p60-analysis.json` and
  `89-0.9.0-pattern-1080p60.mkv`, `90-0.9.0-pattern-analysis.json`.
- `78-independent-settings-1080p60.mkv`, `81-independent-settings-hdmi.png`.
- `84-external-fixture-1080p60.mkv`, `87-external-fixture-after-input.png`.
- `85-external-fixture-phone.png` and `86-external-fixture-after-input.txt`
  establish separate default/virtual display content and activity identity.
- `93-phone-mirror-1080p60.mkv`, `94-phone-mirror-1080p60.png`.
- Final exported-build checks: `99-final-pattern-1080p60.mkv`,
  `100-final-pattern-analysis.json`, `125-final-input-receipt.xml`,
  `127-confirmed-independent-input.mkv`, `128-confirmed-independent-input.png`.

API 30 reports are under `android/app/build/reports/api30-independent/`.
`05-pointer-receipt.xml` reports the delivered pointer/key state;
`06-capture-after-independent-app.xml` reports accepted capture images, and
`stopped-displays.txt` checks display removal. Final APK identity and instrumentation
results are recorded in [android-validation.md](android-validation.md).
