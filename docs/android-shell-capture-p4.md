# Embedded Wireless ADB helper bootstrap: P4

Date: 2026-10-25. The Desktop APK can now transfer and start its bundled helper
through its own pinned DroidMux client. It retains the authenticated shell stream
while capturing. The helper no longer needs a PC launcher. The selected display
still belongs to its original creator; independent display creation/app launching
are separate work.

## Use on the phone

1. Enable Android **Wireless debugging**. For the first pairing, keep its pairing
   code dialog open while using the app, for example with Settings and MS912x in
   split screen. Pairing and TLS connection ports are different and dynamic.
2. In MS912x Debug, choose **Find Wireless debugging endpoints**, then **Pair with
   this phone**. Select this phone's pairing/connect endpoints and enter its
   current six-digit code. Its reusable host identity stays encrypted with
   Android Keystore; another connection does not require another code.
3. Find the current endpoints and choose **Connect ADB helper on this phone**.
   Select the local TLS endpoint. The app verifies shell UID 2000, transfers the
   generated helper, verifies its remote digest, starts it and authenticates its
   Binder enrollment. Wait for the connected source inventory.
4. Select an existing screen and an EDID-supported output mode on Screencast,
   then Start. USB permission and connected-device foreground ownership remain
   the P3 flow. Home and Activity recreation preserve the session. App/notification
   Stop drains USB and capture, retires the helper process and closes its stream.
5. To connect again, use the stored credentials and the currently discovered TLS
   endpoint. **Disconnect ADB helper** cancels an idle or starting connection.
   **Prepare ADB helper** remains the separate PC-assisted development flow.

This uses Wireless debugging TLS, not the optional old port-5555 probe. Pairing
remains usable without USB. The API 24 Mirror APK excludes this backend/helper
and retains ordinary consented MediaProjection capture.

## Ownership and protocol

`android/adb-rs/src/helper_session.rs` owns a persistent two-worker Tokio runtime,
opaque monotonic handles and one registered helper connection. Java never retains
a Rust pointer. New starts are rejected until the previous completed handle has
been released. Connection and transfer/launch stages have explicit deadlines.
Per-handle watch cancellation is independent of the older pair/probe cancellation
flag, so destroying an Activity's pairing tools cannot cancel a running output.

The bridge reuses DroidMux revision `9adeb6258166e9b88808bc6e67845c73763f070c`,
its stored-credential TLS connection, Sync transfer and raw Shell v2 session.
Helper bytes are bounded to 2 MiB and checked against the bundled SHA-256 before
transfer. Upload uses a quoted digest-derived path under `/data/local/tmp`, mode
0600, and a remote digest verification before a fixed `app_process` command.
Only a numeric endpoint assigned to the phone is accepted; this adds no generic
shell command interface or alternate ADB implementation.

`HelperEnrollment` distinguishes embedded enrollment from debug claiming. It
creates one expiring 256-bit token and exports one bounded in-memory envelope to
the retained stream's stdin. `--bootstrap-stdin` accepts session/app identity,
protocol, provider authority, build digest and token as bounded ASCII fields.
The helper parser does not read ahead into the remaining lifetime stream. The
provider still accepts only UID 2000 and the matching one-use attempt. Embedded
attempts cannot be claimed through `claimDebug`, including in a release context.
The existing debug-only PC path remains independently tested.

The shell stream's stdout/stderr are continuously drained with a 64 KiB total
limit and discarded. JNI delivers only a bounded queue of fixed status messages;
raw output, pairing codes and bootstrap tokens never enter arguments/logs or
saved artifacts. Java erases its envelope byte array; Rust uses zeroizing storage.
The helper's embedded-error diagnostics omit exception messages. Closing stdin
has a bounded graceful wait followed by stream/client close.

`EmbeddedHelperConnection` owns the JNI handle in the service's
`HelperCaptureSource`. Polling and completion callbacks outlive UI bindings.
Actual stream loss cancels heartbeats/requests and waits for confirmed helper
Binder death; a failed transaction alone does not permit reader destruction.
The P3 coordinator still joins USB drain and producer termination before closing
native capture, and waits for helper/stream retirement before ending foreground
ownership. The ImageReader, Linux-derived C conversion/scanner/serializer and USB
batch path are unchanged.

## Validation and limits

See [the validation log](android-validation.md) for exact APK/helper digests,
reports, sample counts and which artifact each run exercised.

- Rust: five tests and Clippy with warnings denied. Existing helper control/parser
  checks pass, including eight control groups and bounded stdin parsing tests.
- Desktop: 18 instrumentation groups on API 30/36. Mirror: six on API 24/30/36.
  Both builds and lint pass. JNI fixtures cover invalid endpoint/port/digest,
  unpaired startup, duplicate starts, cancellation and released handles.
- Phone API 37: real same-phone pairing/TLS, stored-credential reconnection,
  UID-2000 helper transfer/verification/start, live NSD discovery, the Connect
  handler and endpoint picker pass. Pairing secrets are read only inside the
  optional test process and are never exported to the host.
- Actual USB/HDMI: 2560×1080 source fitted to advertised 1920×1080 at 50 Hz,
  including Home, Activity recreation, notification Stop and retained-stream EOF.
  Only UltraSemi's persistent by-id capture path was used. Source identity survives
  application Stop; PC ADB drives tests/creates the independent test source but
  does not transfer or start the helper in these embedded runs.

For the optional pairing fixture, navigate Settings to Wireless debugging without
opening its secret dialog, then run `CaptureNativeTestRunner` with `embeddedAdb`,
`wirelessPageReady`, `connectHost` and `connectPort`. The test opens/reads the code
inside the phone, pairs, boots/disconnects, discovers the TLS endpoint through
Android NSD and reconnects through the app's picker. `reconnectOnly` skips new
pairing when the app already stores this phone's identity. Do not pass a pairing
code or bootstrap envelope in host command arguments/files.

```sh
python3 tools/android/test_session_output.py \
  --serial DEVICE --source-id TEST_DISPLAY_ID \
  --source-width 2560 --source-height 1080 \
  --embedded-host PHONE_LOCAL_IP --embedded-port CURRENT_TLS_PORT \
  --apk PATH_TO_INSTALLED_APK --report android/app/build/reports/helper-p4-hardware
```

`--drop-stream` exercises actual retained-stream EOF instead of notification Stop.
This verifies the stream-loss cleanup path; it does not claim a tested Wi-Fi
radio/Settings-toggle outage. Pairing requires explicit setup in Android Settings.
Automatic rebootstrap after transport loss, creating a new independent desktop,
launch/input control and protected-content capture are outside this checkpoint.

References: [Android Wireless ADB](https://developer.android.com/tools/adb#wireless-android11-command-line)
and [AOSP Wi-Fi ADB architecture](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/main/docs/dev/adb_wifi.md).
