# Touchpad for a helper-created screen

The touchpad is implemented in the Desktop APK for a live independent screen
created by the shell helper. The original design was recorded on 2026-10-25;
this page summarizes the current flow. See [screen creation](android-created-screen.md)
and the [Android README](../android/README.md) for setup.

## Use

When the helper-created screen is streaming and input is available, **Touchpad**
appears beside the Options button. It opens a page on the phone while the HDMI
session continues. The entry is unavailable for phone mirroring, app-window
capture, the Debug test scene and externally owned screens.

- Move one finger to move the pointer; tap to click and double tap to double-click.
- Hold then move, or double tap and hold while moving, to drag. Lift to release.
- Move two fingers to scroll. Left and right click buttons provide explicit clicks.
- **Apps**, **Home** and **Back** target the created screen. The page's
  **Screencast** button and the phone's Back return to the controls without
  sending a remote Back.

The page follows the app's Auto, Light or Dark appearance. The adapter pointer
uses a small trusted cursor layer offset one pixel from the click coordinate and
hides after five seconds without movement. Using the touchpad pauses automatic
physical-mouse sharing for that session; an explicit sharing start, position
choice or new screen can resume it.

## Ownership and verification

The view classifies gestures, while the session and authenticated helper own
input sequencing and target the helper-owned display. Pointer movement is
relative and clamped to that display; release, cancellation and shutdown clear
held input. No per-gesture `adb shell input` process is needed.

The [2026-10-25 implementation review](android-review-2026-10-25.md) records
real-helper input delivery, held-button and cursor-lifecycle checks on API 30
and 36 emulators. The earlier [app-owned display experiment](android-virtual-display-test.md)
records separate shell input and physical HDMI observations for its own build.
These checks do not establish touchpad behavior on every Android version or OEM.
Physical DeX mouse handoff and cursor-parking evidence is tracked separately in
the [mouse-sharing experiment](android-dex-topology-experiment.md).
