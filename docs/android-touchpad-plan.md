# Touchpad for the helper-created screen

Date: 2026-10-25. Status: implemented in the working tree. Desktop and Mirror
assemble/lint checks pass. Runtime input delivery and cursor rendering have not
been exercised on a device. Baseline: `2e918af`, independent screen creation
through the embedded ADB helper. See [screen creation](android-created-screen.md).

## User experience

Add a **Touchpad** button immediately beside the existing **Options** gear in
`MainScreenView`'s top bar. This placement is confirmed by the user. Keep the
Options button in its current location.

Show Touchpad only for a live, independent display created by this app's shell
helper: the active request uses `CREATE_NEW`, the helper is connected, the
session is `STREAMING`, and `createdDisplayId` is positive. Selecting the option
before Start is insufficient. It stays hidden for phone mirroring, app sharing,
the diagnostic test scene and capture of an externally owned display. A failed
input capability check disables the button with a short reason.

The button opens an app page on the phone while the service keeps HDMI running.

```text
Back to screencast                       Screen #ID

+------------------------------------------------+
|                                                |
|                  TOUCHPAD                      |
|                                                |
|    Move one finger to move the HDMI pointer     |
|    Tap to click; use two fingers to scroll      |
|                                                |
+------------------------------------------------+

 [Left click]                       [Right click]

 [Back]       [Home]             [Apps]
```

- One-finger motion moves a relative pointer; lifting and repositioning the
  finger does not jump the pointer to a new location.
- A tap clicks; two taps click twice. Holding and moving starts a drag, continued
  movement drags, and lifting releases it.
- Two-finger movement scrolls vertically or horizontally. Transitioning between
  one and two fingers must not generate an accidental click or pointer jump.
- Explicit left/right click buttons provide discoverable alternatives to gestures.
- Back targets the remote app. Home opens the created screen's home, reusing the
  current secondary-home/welcome fallback. Apps reuses the existing picker.
- Recent apps is not included in this implementation.
- The phone's system Back and the page's Back to screencast return to the main
  page. They do not send remote Back or stop output.

Use the existing appearance settings, system insets and accessibility labels,
with at least 48 dp button targets. Keep the phone awake while the touchpad page
is visible. The page fits the usable viewport rather than placing the touchpad
inside a scrolling form. Support portrait/landscape layouts. Closing the page,
backgrounding the app or losing touch focus releases held input and cancels
incomplete gestures; HDMI continues if the session is healthy.

## Implementation boundaries

| Owner | Planned responsibility |
|---|---|
| `MainScreenView` / `TouchpadSurfaceView` | Conditional entry, page layout, gestures and click/Home/Back/Apps controls |
| `MainActivity` / `ScreencastController` | Page navigation, lifecycle cancellation and UI wiring |
| `SessionCoordinator` | Created-display eligibility, active phase and input dispatch |
| `HelperCaptureSource` | Authenticated Binder transport, monotonic sequence and capability/status updates |
| `DisplayInputController` | Bounded/coalesced events, pointer/button state and cancellation |
| `FrameworkInputCompat` | Cached InputManager/InputEvent reflection and targeted injection |
| `ShellDesktop` | Owned display identity and targeted Home launch |

The source/coordinator snapshot should expose a single eligibility/capability
result so visibility, page navigation and dispatch cannot disagree. Keep input
state and sequence ownership outside Activity/View instances so recreation
cannot reset a running session's command sequence. A reattached view begins a
fresh gesture while the helper retains the pointer position.

## Helper input path

The helper probes shell input injection when it creates the display and carries
the result in the READY message. The UI enables Touchpad only for a live created
screen with that capability; the page's exit action remains separate from remote
Back. The capture session and USB output continue while the page is open.

Extend the shared AIDL protocol with a small typed input command/batch and
capability/error reporting, then bump the protocol version. Pass session,
capture generation and monotonic input sequence. The helper derives the target
from its own live desktop; the UI cannot select an arbitrary display ID.
Validate command kinds, finite/bounded deltas, button states and batch size.
Reject stale, duplicate, stopped or externally owned targets before injection.

Use framework `MotionEvent`/`KeyEvent` injection inside the existing shell process,
explicitly associating every event with the created display. Cache compatibility
reflection and construct balanced mouse button/key sequences with consistent
timestamps. Home should use the already targeted desktop launch method rather
than a global system action. Recents requires separate platform verification.
Report denied/unsupported input while leaving healthy HDMI output running.

Keep a relative pointer in created-display pixels and clamp it to its bounds. The
virtual screen uses the selected output dimensions, so input coordinates do not
use the phone's touch coordinates. Check rotated output and geometry changes.
Cursor visibility in the adapter output remains to be confirmed on a device.

Coalesce adjacent pointer motions in the helper's bounded event buffer and drain
in short batches on its serial handler. Preserve ordering around button/key
transitions. Saturation triggers explicit input cancellation/reset rather than
discarding a release. Stop invalidates the generation before queued input runs,
clears pending motion and releases/cancels held input before display teardown.
Never fall back to display 0 if targeting fails. Reopening the page must not
replay old commands.

This reuses the retained embedded ADB helper and Binder connection. It does not
spawn `adb shell input` commands per gesture. Pixel capture, shared Linux/native
conversion, damage scanning, USB transport and session ownership stay as they are.

The upstream reference for this approach is scrcpy's
[display-targeted injection](https://github.com/Genymobile/scrcpy/blob/master/server/src/main/java/com/genymobile/scrcpy/device/Device.java),
[InputManager wrapper](https://github.com/Genymobile/scrcpy/blob/master/server/src/main/java/com/genymobile/scrcpy/wrappers/InputManager.java)
and [mouse/scroll dispatch](https://github.com/Genymobile/scrcpy/blob/master/server/src/main/java/com/genymobile/scrcpy/control/Controller.java).
These establish the proposed mechanism, not acceptance on this app or device.

## Verification

Desktop and Mirror assemble/lint checks pass. Runtime display-input events,
visible HDMI pointer, gesture behavior and navigation shortcuts still require
verification with the real helper on the supported emulator/device matrix.

Add focused tests for gesture classification, inverse mapping, ordered/coalesced
commands, stale generations and release under cancellation/overflow. Android UI
fixtures cover the visibility matrix, navigation and recreation. A separate
fixture activity on the created display records actual delivered events and
coordinates; use that to verify dispatch rather than counting Binder calls.
Exercise targeted input on API 30/36 emulators and the API 37 phone with real
USB/HDMI. Repeat Desktop and Mirror builds/lint and their supported fixture
matrices; Mirror must retain its helper/input exclusions. Emulators establish
event delivery, while physical HDMI establishes cursor/rendering and interaction.

On the phone, open an app from the page, move/click the cursor, scroll, drag and
use Back/Home. Confirm the corresponding app changes on HDMI while the touchpad
remains on the phone. Test stop/helper loss while dragging. There is no Recents
shortcut yet. Report injected-event rate, queue behavior and observed response
separately from display frame rate.

Text/IME forwarding, pinch-to-zoom, gamepad input, audio and generalized control
of other displays are later work. The current page provides pointer control,
scrolling and the Home/Back shortcuts described above.
