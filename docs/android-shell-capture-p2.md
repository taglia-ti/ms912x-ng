# Helper control, cancellation and cleanup

The desktop helper now uses IPC protocol 3. Protocol information, display
inventory and capture control use one-way requests with correlated callbacks.
Framework display operations run on a serial helper thread. The existing
capture-only Debug preview remains the available user flow.

## Ownership and cancellation

`SourceListResult` carries session/request identifiers and a bounded inventory.
The app applies five-second protocol/inventory deadlines and ignores replies for
cancelled requests or old sessions. Inventory cannot block the native encoding
and reader-close worker. Production callbacks still require real shell UID 2000.

`ControlQueue` bounds running plus queued framework operations to 64. Rejected
requests return busy without retaining another Surface. Framework compatibility
initialization runs lazily on the control thread, after watchdog startup.

`CaptureRequests` enforces increasing capture request/generation identifiers.
Release marks a generation cancelled before enqueueing its serial barrier.
Construction checks cancellation before starting and before publishing its owner;
a mirror completed after cancellation closes itself. Acknowledged release fences
older queued starts. Retired attach replay cannot create another mirror. An old
generation's release leaves a newer capture intact; duplicate release is safe.

The app retains the reader until matching release acknowledgement or confirmed
Binder death. A failed transaction stops heartbeats and waits for death instead
of pretending its producer exited. Release timers belong to a specific session,
generation and reader. Release/death collisions schedule only one reader cleanup,
behind in-flight diagnostic encoding on the serial worker.

Heartbeats run every two seconds and expire after ten, independently of frame
arrival and framework work. Stop, owner Binder death and ADB stdin EOF enter the
same idempotent shutdown. If ordinary cleanup has not exited within two seconds,
the watchdog kills only its own helper PID directly; it does not rely on either
Looper or Java shutdown hooks. Source polling remains once per second.

`UsbOutputController.stopStreaming(Runnable drained)` adds an asynchronous main
thread barrier after batch completion/abort and USB session close, including for
an already closed controller. Old results are published before a completion
callback can start replacement work. P3 must join this barrier with mirror
release/death before destroying a USB capture. The current helper preview still
does not send USB frames or own a foreground session.

## Tests and commands

`tools/android/test_helper_control.sh` compiles production queue/cancellation/
lifetime classes with plain Java. Eight groups cover capacity, rejected posts,
cancellation before/during construction, superseded requests, constructor/release
faults, exact resource release and termination while a simulated worker is blocked.

Desktop instrumentation now has 16 groups. `HelperClientFixtureTests` injects
local transport faults into the real controller/ImageReader: old inventory during
a newer start, late READY, transaction failure while a producer is alive, release
deadline scoping, release/death collision, first-image and metadata timeouts.
These fixtures use the app UID and do not establish privileged capture. USB
fixtures separately check repeated drain callbacks, session/interface cleanup,
capture retention and drain acknowledgement after controller close.

Real-helper acceptance uses a dedicated temporary secondary source:

```sh
bash tools/android/test_helper_control.sh
python3 tools/android/test_helper_lifecycle.py \
  --serial DEVICE --source-id TEMPORARY_DISPLAY_ID --termination watchdog \
  --report android/app/build/reports/helper-p2-DEVICE-watchdog
```

The watchdog case checks missing display, null Surface, invalid dimensions,
pending-start cancellation, stale replay, old-generation release and duplicate
Stop, then withdraws heartbeats during capture and awaits actual helper death.
Other termination choices are `eof`, `app-death` and `helper-death`. EOF retains
app heartbeats, proving that stream closure works independently of heartbeat
expiry. App death stops only our target package; helper death kills only the
enrolled helper PID. The original source identity must survive every case.

The host verifies a live mirror before termination and its absence afterward in
DisplayManager and SurfaceFlinger. It checks live display records: Samsung's
RenderEngine cache may retain a removed display's name and is excluded. An
unrecognized inventory fails validation. The fixture uses KEEP_SCREEN_ON so
device sleep does not invalidate its known changing test content.

All four modes pass on API 30 and the API 37 phone. The P1 geometry matrix also
passes with protocol 3. See [the validation log](android-validation.md) for exact
artifacts/results. Platform-specific hidden-API failures and actual blocked
framework services are not exhaustively injected on devices; blocked-worker and
resource faults are covered by production control-core fixtures.

Normal service-owned USB sessions and embedded same-phone ADB startup remain
P3/P4. No new HDMI or standalone pairing result is claimed.
