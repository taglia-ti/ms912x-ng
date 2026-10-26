# Independent screen creation: P5

Date: 2026-10-25. The Desktop APK creates its own independent Android screen
through the authenticated shell helper and sends its pixels to the USB adapter.
It can launch another installed application on that screen. A PC or scrcpy does
not need to create the source display.

## Use

1. Connect the adapter and allow USB access. Connect the helper using the app's
   [Wireless debugging setup](android-shell-capture-p4.md#use-on-the-phone).
   Initial Android pairing still requires the user's action in Settings.
2. In Screencast, select **Create a new screen**, then an advertised monitor
   resolution/refresh rate, and press **Start**. The new screen uses those pixel
   dimensions and 160 dpi. Screen creation requires the Desktop APK, API 30+.
3. Press **Open app on new screen** on the phone and select an installed app.
   The helper starts its exported launcher activity on the new display. Android
   and the selected app still decide whether that activity supports this display.
4. Home and recreation of the phone's MS912x Activity preserve the screen and
   output. App/notification **Stop** releases the created display and its content,
   drains USB, retires the helper and closes the reader.

Android's secondary launcher supplies initial content where supported. On the
tested API 30 image both system home launch attempts returned `-96`, so the APK
provides a welcome activity on the same helper-owned screen. The phone's app
picker still opens another application there. The older app-owned diagnostic
scene is opened from Debug, not the source picker.

## Implementation and ownership

`ShellDesktop` runs in the existing real UID-2000 helper. Its framework context
uses the actual shell package/UID attribution, then creates a public, own-content
VirtualDisplay with presentation/system-decoration support. It does not request
a secure display. API 34+ also requests the selected refresh rate. The helper
owns only this display, which is excluded from external-source inventory.
Existing-display capture keeps its previous ownership rules.

P5 introduced a create-screen source sentinel, bounded density/refresh
fields and a typed launch request. Launches use `ActivityOptions` with the created
display ID and the framework activity service, without constructing a shell
command. Caller UID/session checks, monotonic request IDs, generation fences,
bounded control work and stop cancellation also apply to launch requests.
An app launch can target only this session's live, helper-owned desktop. The
current protocol version is defined in [HelperProtocol.java](../android/ipc/src/main/java/io/github/taglia_ti/ms912x_ng/ipc/HelperProtocol.java).

The new display renders directly into the existing ImageReader Surface. Nonzero
output rotation uses the existing GPU transform. Native conversion, Linux-derived
damage scanning, batch encoding and the USB worker are unchanged. Diagnostic
encoding remains disabled when USB owns output. No new pixel transport or codec
pipeline is introduced.

## Acceptance

The [validation log](android-validation.md#independent-screen-p5) records exact
artifact identities and hardware results.

- Desktop build/lint and 18 instrumentation groups passed on API 30 and 36.
- Mirror build/lint and six groups passed on API 24, 30 and 36; helper assets and
  the ADB bridge remain excluded from that APK.
- `test_created_screen.py` passed on API 30 and 36: the real shell helper created
  a source, launched a separate test APK, produced native frames and removed the
  display on Stop. PC ADB only bootstrapped the helper for these compatibility
  tests; it did not create a source display.
- The API 37 phone used the embedded stored-credential TLS bootstrap and the
  actual adapter/UltraSemi HDMI capture card. Creation, foreground/Home/Activity
  recreation, native frames and notification Stop passed. The phone's actual
  app picker selected YouTube, and Android's activity dump placed it on the
  created display. Its interface reached HDMI.

To repeat creation-only acceptance, install matching Desktop/test APKs and run:

```sh
python3 tools/android/test_created_screen.py --serial SERIAL \
  --apk android/app/build/outputs/apk/debug/app-debug.apk --report /tmp/created-screen
```

Hardware acceptance uses `test_session_output.py --create-screen --source-id -1`
with the chosen output dimensions, local embedded-ADB endpoint and optionally
`--launch-package com.google.android.youtube`. It never creates a host-side source.

These P5 runs cover API 30/36 emulators and one API 37 phone, rather than every
intermediate API or vendor build. They establish display/app placement and HDMI
output, not YouTube video playback, audio, protected-content capture or input
forwarding at that checkpoint. Later input checks are described in the
[implementation review](android-review-2026-10-25.md) and
[mouse-sharing experiment](android-dex-topology-experiment.md). Hidden framework compatibility remains version/vendor dependent;
app relaunch/resize policy also varies. Wireless debugging authorization remains
necessary after reboot or loss of authorization.

Compatibility research used the upstream scrcpy
[new-display path](https://github.com/Genymobile/scrcpy/blob/master/server/src/main/java/com/genymobile/scrcpy/video/NewDisplayCapture.java),
[display wrapper](https://github.com/Genymobile/scrcpy/blob/master/server/src/main/java/com/genymobile/scrcpy/wrappers/DisplayManager.java),
[context](https://github.com/Genymobile/scrcpy/blob/master/server/src/main/java/com/genymobile/scrcpy/FakeContext.java)
and [framework setup](https://github.com/Genymobile/scrcpy/blob/master/server/src/main/java/com/genymobile/scrcpy/Workarounds.java).
The repository's helper implements its own thin wrappers and reuses its existing
capture/USB path; it does not import the scrcpy server or client.
