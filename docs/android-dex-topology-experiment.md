# DeX mouse sharing and native topology experiments

This experiment explores moving a physical mouse between a native Samsung DeX
monitor and an MS912x monitor displaying a helper-created screen. The first
experiment uses Android's native display topology, so Android would own pointer
crossing, acceleration, buttons and its cursor. Creating a virtual screen alone
does not establish that it belongs to this topology.

Samsung excluded the helper screen from native routing, so the APK now includes
an opt-in shell router. The standalone topology tool remains available for
testing native admission on other devices; it does not intercept input.

## APK mouse sharing

Select **Create a new screen**, then choose **Virtual / Left / Right / Up /
Down** in **New screen placement**. A native external desktop must already be
active. Virtual disables automatic sharing; a side persists the choice, starts
sharing when the screen is ready, and retries with bounded backoff after a
failure or device reconnection. **Settings → Mouse sharing · Experimental**
chooses the physical mouse and offers manual controls. The entire adjoining
edge permits crossing, with the position along it scaled between resolutions.
Remaining motion beyond the edge uses the display-density ratio. Position
changes apply while sharing. Debug's clear-options button also clears the saved
choices.
Retries refresh device identities, avoid duplicate pending starts, and stop when
the source stops. They work while the Activity is closed. Manual Stop chooses
Virtual; Touchpad input pauses retries for the current session until an explicit
Start/position choice or a new screen. Host policy checks cover these transitions.

The helper runs as shell UID 2000 and opens only the selected relative mouse,
checking its identity and released buttons before `EVIOCGRAB`. Keyboard interfaces
and the helper's own devices are excluded. A small JNI bridge reads ordered
evdev reports and supplies an unassociated uinput mouse for native DeX. The
router disables variable scaling on that display, calibrates Android's remaining
fixed gain, and converts motion to display coordinates. The adapter endpoint
reuses the production touchpad injector and trusted cursor overlay, including
its one-pixel offset and five-second idle timeout. Native topology is unchanged.

Buttons and wheel events go to the active display. A held drag remains there
until release; cross-desktop task/window dragging and shared DeX taskbars are
outside this implementation. The adapter must use 0° output rotation. Hidden
cursor/topology APIs and shell evdev/uinput permissions vary by Android/OEM;
unsupported devices report unavailability instead of claiming a mouse.

Evdev reading and geometry run on a dedicated thread, independently of framework
delivery. A bounded mailbox replaces superseded absolute motion after resolving
clipping and crossing. Button, wheel and crossing boundaries remain ordered.
This prevents a slow Android input target from overflowing the physical mouse's
kernel queue. If `SYN_DROPPED` still occurs, the reader discards the incomplete
report through the next `SYN_REPORT` and queries current buttons with `EVIOCGKEY`,
following the [kernel input protocol](https://docs.kernel.org/input/event-codes.html).
Lost relative motion cannot be reconstructed; sharing continues from the last
known position. A disconnect or an unrecoverable button/wheel backlog still
releases input explicitly.
Hover moves use asynchronous injection and update the adapter cursor before
dispatch. Button, scroll and held-drag operations keep ordered dispatch. This
removes a window-dispatch wait from ordinary movement; USB presentation still
has capture and transfer latency. The native DeX cursor remains managed by
Android. While the adapter is active, uinput parks that cursor at a corner of the
selected side (Left: bottom-left; Up: top-right; Right/Down: bottom-right).
Android clamps the hotspot to the last pixel, so this can leave a small part of
the icon visible. Logical pointer coordinates remain independent of parking.
On return, the cursor jumps to the entry edge at the proportionally mapped
height/width. Normal stop restores its pre-parking location before removing
uinput. No overlay-based hiding or persistent cursor override is installed.

Stop, mouse removal, display geometry/density changes, helper teardown, or
Touchpad input release capture and held buttons. Normal shutdown restores the
original display scaling state. A record written before changing scaling lets
the next helper startup restore it after abrupt process death, checking the
display identity and owner process. Until that startup, a forcibly killed helper
can leave scaling disabled. Kernel file closure releases the physical mouse
immediately on process death. This feature handles mouse input only.

## Controlled QEMU input test

The Android emulator accepts QMP relative motion and buttons. Its PS/2 frontend
did not produce a guest evdev mouse in the tested image. Adding a QEMU USB HID
mouse produced a real guest mouse and supported deterministic hot-unplug tests.
Boot the API 36 AVD with the normal emulator options followed by:

```sh
-qemu -qmp unix:/tmp/ms912x-router-qmp.sock,server=on,wait=off \
  -device qemu-xhci,id=ms912x-test-usb \
  -device usb-mouse,id=ms912x-test-mouse,bus=ms912x-test-usb.0
```

Install matching desktop/debug-test APKs, then run:

```sh
python3 -B tools/android/test_mouse_router_qmp.py \
  --sdk /mnt/data/LINUX/android-sdk --serial emulator-5556 \
  --qmp /tmp/ms912x-router-qmp.sock \
  --report android/app/build/reports/mouse-sharing/qmp-usb
```

This drives QEMU → guest evdev → the production native bridge/router → production
display-input endpoints → separate-UID receiver activities on two isolated
displays. It checks crossing and return, a drag held beyond one second with
stable `downTime`, release, wheel, stop while held, restart, and USB removal
while held. The final check removes the test USB device; restart the emulator
or add it again through QMP before rerunning. It does not emulate Samsung's
native DeX compositor or prove physical HDMI cursor presentation.
The test also stalls screen delivery by 150 ms while sending rapid motion, then
separately pauses evdev reading for three seconds to force a kernel overflow.
It verifies continued movement and recovery before the held-button unplug check.

## Run

Use the installed Android SDK, Java compiler and project Gradle cache. The
launcher builds the helper classes offline, compiles the experiment, pushes a
temporary JAR, and removes that JAR after execution. It requires shell UID 2000,
does not request root, and does not install an APK. Reports go under an ignored
build directory in the examples below.

Inspect the current displays and their native mouse-topology membership:

```sh
python3 -B tools/android/experiment_display_topology.py \
  --sdk /mnt/data/LINUX/android-sdk --serial PHONE_SERIAL \
  --report android/helper/build/reports/topology/inventory.json
```

Check whether a screen created with the production helper's flags is admitted:

```sh
python3 -B tools/android/experiment_display_topology.py \
  --sdk /mnt/data/LINUX/android-sdk --serial PHONE_SERIAL \
  --probe-screen --width 1920 --height 1080 --density 160 \
  --report android/helper/build/reports/topology/probe.json
```

This creates a temporary screen, tries its secondary launcher, drains its
ImageReader without retaining pixels, waits up to five seconds for topology
membership, and releases the screen. It does not stream HDMI or test mouse
crossing. Exit code `0` means admitted, `2` means excluded or an unavailable
topology API, and `1` means an operational failure. Inspection returning `0`
only establishes that the topology could be read. Orderly host cancellation
returns `130` after cleanup.

For a real crossing test, first connect the native DeX monitor and a physical
mouse. Start **Create a new screen** in the app, then inspect again. Both the
native DeX display and the app's `ms912x-desktop-...` display must have
`inTopology: true`. Use their explicit IDs; the phone's display `0` is not
automatically the DeX monitor.

```sh
python3 -B tools/android/experiment_display_topology.py \
  --sdk /mnt/data/LINUX/android-sdk --serial PHONE_SERIAL \
  --screen ADAPTER_DISPLAY_ID --anchor DEX_DISPLAY_ID \
  --position right --seconds 30 \
  --report android/helper/build/reports/topology/crossing.json
```

Positions are `left`, `right`, `top` and `bottom`, centered along the shared
edge. Geometry uses Android's display-independent coordinates, accounting for
each monitor's density. The tool rejects missing topology members, overlaps,
and arrangements Android cannot represent without moving another display.
It preserves the primary display and membership, verifies the applied
arrangement, and restores the original topology after the timer or orderly
host cancellation. If a display/settings change has replaced its arrangement,
it leaves that newer state intact. Android offers no atomic compare-and-set
for topology, so avoid concurrent layout changes during this temporary test.
Forcibly killing the remote process cannot guarantee restoration.

Check physical pointer motion, clicks, held drags and scrolling in both
directions on all four edges. Passing geometry checks or a topology setter
does not prove physical cursor routing. This experiment also does not establish
shared DeX taskbars or dragging application windows between displays.

## Initial Samsung result

On 2026-10-26, the connected SM-S928B reported API 37. The shell could read
the native topology; only the phone display was present. A production helper
screen was created at 1280x720, remained absent from the topology after five
seconds, and was released successfully. The screen was a usable Android
display even though it was excluded from native mouse routing.

Separate temporary probes also tried removing the own-display-group flags,
removing system decorations, and requesting the newer
`VIRTUAL_DISPLAY_FLAG_ALLOWS_CONTENT_MODE_SWITCH`, alone and with decorations.
None admitted the virtual screen in this setup. An attempted addition through
`setDisplayTopology` was rejected by the device's
`DisplayTopologyCoordinator.verifyRearrangement`. The checked-in experiment
only rearranges existing members; it does not attempt to add rejected screens.

Native DeX and a physical mouse were inactive during these initial checks.
The follow-up below records the result after connection.

## Follow-up with native DeX and a USB jiggler

Later on 2026-10-26, the phone reported DeX active on display 382, an AOC
27G42HE at 1920x1080. The external USB device `1997:2452` exposed a relative
mouse on `/dev/input/event12`; the user identified it as a jiggler. The native
mouse topology contained only the DeX display. A fresh 1920x1080 helper screen
was still excluded after five seconds and was released. Native placement was
not attempted because the helper screen failed its membership precondition.

Two fallback building blocks were verified as shell UID 2000, without root:

- The selected mouse could be opened, queried and exclusively claimed with
  `EVIOCGRAB`, then immediately released. An identity-mismatch test refused to
  claim the device. A separate read observed four positive `REL_X` reports over
  40 seconds. This was not a controlled edge-crossing test.
- Android's hidden `InputManager.createVirtualMouse` could associate a native
  virtual mouse with a shell-owned helper display outside the topology. A
  requested relative move of `(20, 10)` changed that display's reported cursor
  from `(960, 540)` to approximately `(982.46, 551.23)` with system acceleration.
  The native DeX cursor was unchanged during that sample. The virtual mouse and
  screen were released; DeX remained the default mouse display. Cancelling
  after virtual mouse creation also released both resources and removed the
  temporary JAR.

Repeat the virtual mouse test with:

```sh
python3 -B tools/android/experiment_display_topology.py \
  --sdk /mnt/data/LINUX/android-sdk --serial PHONE_SERIAL \
  --probe-mouse --width 1920 --height 1080 --density 160 \
  --report android/helper/build/reports/topology/virtual-mouse.json
```

An exit `0` here verifies the temporary display's reported native cursor
movement. It does not test physical HDMI presentation, button handling,
scrolling, crossing or a complete router. The test sends only relative motion
to its own temporary screen; it does not claim the physical mouse.

The separate native access probe accepts an explicit device path and expected
USB IDs. Recheck these after reconnecting; event numbers can change. Compile
and run it using the existing SDK/NDK:

```sh
mkdir -p android/helper/build/reports/topology
/mnt/data/LINUX/android-sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang \
  -std=c11 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror \
  tools/android/mouse_access_probe.c \
  -o android/helper/build/reports/topology/mouse_access_probe
adb -s PHONE_SERIAL push android/helper/build/reports/topology/mouse_access_probe \
  /data/local/tmp/ms912x-mouse-access-probe
adb -s PHONE_SERIAL shell chmod 700 /data/local/tmp/ms912x-mouse-access-probe
adb -s PHONE_SERIAL shell /data/local/tmp/ms912x-mouse-access-probe \
  /dev/input/event12 1997 2452 --grab
adb -s PHONE_SERIAL shell rm -f /data/local/tmp/ms912x-mouse-access-probe
```

Omit `--grab` for a read-only capability query. The probe refuses a device
without both relative axes and a primary mouse button, a different identity,
or a mouse with a held button. It immediately releases a successful grab;
closing its file descriptor also releases it on failure or process death.
This checks access, not sustained input forwarding or hot-unplug recovery.

These probes established prerequisites for the implemented router above. A
jiggler supplies periodic movement but cannot exercise held dragging or
scrolling by itself; the QMP test supplies those controlled reports.

## Checks and framework references

Run geometry checks with `bash tools/android/test_helper_control.sh`. They cover
four directions, mixed densities, negative coordinates, root translation,
preservation of other monitors, overlaps and membership changes. Runtime reports
separate inventory, screen creation, membership and restoration events.
Restoration tests also exercise stale preparations, newer arrangements, setter
rejections and Binder failures after a request was applied.

Validation on 2026-10-26:

| Environment | Result |
|---|---|
| Host helper control suite | Existing control/geometry groups plus placement and restoration tests passed |
| API 30 emulator | Reported an unavailable topology class with exit `2` |
| API 36 emulator | Reported disabled topology with exit `2`; its display topology feature flag was off |
| API 37 Samsung phone | Inventory, screen creation/exclusion/release, missing-member rejection and cancellation cleanup checked |
| Native DeX and USB jiggler | DeX active; helper screen excluded; selected-device claim/release and native virtual mouse movement verified |
| API 36 QMP USB mouse | Production raw-input/router and separate-UID receivers passed crossing/return, held drag, release, wheel, stop and unplug cleanup |
| API 37 native DeX cursor | Calibrated uinput movement reached the requested coordinate within a fraction of a pixel; original scaling restored after stop and interrupted-process recovery |
| APK on the API 37 phone | Started a 1920x1080 HDMI screen, shared the USB jiggler, observed right-edge handoff to the adapter, and changed placement live; monitor reconfiguration and Touchpad handoff released sharing and restored scaling; capture remained active after Touchpad Home, with 173 completed USB updates reported |
| App regression fixtures | All 21 groups passed on the API 37 phone and API 30/36 emulators; mirror-only API 24 passed all 10 groups; host helper suites and both APK lint builds passed |
| Physical HDMI pixels | No capture-card recording available for this mouse-sharing test |

The subsequent Logitech USB receiver test exposed fatal `SYN_DROPPED` handling
under sustained movement. The dedicated reader/mailbox and state recovery above
address that failure. The extended QMP test passed with a 150 ms delivery stall,
rapid movement and a deliberately forced kernel overflow, as well as the original
drag, wheel, stop and unplug checks. The API 36 regression suite passed 21/21.
Phone full-suite retries reached 20/21, with touchpad gesture/palette fixture
failures; a previous-APK baseline also produced UI fixture failures (17/21).
The user was interacting with the phone during these runs, so they are not a
controlled phone regression result. The user subsequently reported the updated
mouse sharing working overall. Controlled mouse stress results are recorded
separately above.

The placement/automatic-retry policy and proportional four-edge geometry pass
host tests. The updated APK passes all 21 API 36 fixture groups and the QMP mouse
stress suite, including release after stop with the physical grab released before
uinput teardown. Native corner parking needs an API 37 phone check: the API 36
AVD lacks the cursor-position Binder method used by that endpoint. Run the
shell-only `run_mouse_probe.py ... park` mode with one physical mouse to check
all four corners, edge return and normal-stop restoration. It uses DeX when
active, otherwise the phone display, and restores scaling after each case.

Framework references:

- [Android multi-display practices](https://source.android.com/docs/core/display/multi_display/recommended-practices)
- [DisplayTopology](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/android17-release/core/java/android/hardware/display/DisplayTopology.java)
- [Topology membership and rearrangement validation](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/android17-release/services/core/java/com/android/server/display/DisplayTopologyCoordinator.java)
- [InputManager virtual mouse creation](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/android17-release/core/java/android/hardware/input/InputManager.java)
- [Virtual mouse permissions and display ownership checks](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/android17-release/services/core/java/com/android/server/input/InputManagerService.java)
