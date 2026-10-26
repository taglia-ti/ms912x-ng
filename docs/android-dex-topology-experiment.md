# Native DeX mouse topology experiment

This branch explores moving a physical mouse between a native Samsung DeX
monitor and an MS912x monitor displaying a helper-created screen. The first
experiment uses Android's native display topology, so Android would own pointer
crossing, acceleration, buttons and its cursor. Creating a virtual screen alone
does not establish that it belongs to this topology.

The experiment is a standalone shell tool under `tools/android`. It reuses the
production helper's `ShellDesktop` creation code. It is not bundled into the APK
and adds no settings or input interception to the application.

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

Native DeX and a physical mouse were inactive during these checks. Repeat with
both connected before concluding that this Samsung build cannot expose our
screen in that configuration. Until then, seamless native DeX-to-adapter
crossing remains unverified. If admission remains blocked, the separate fallback
is a shell mouse router using relative device motion; access, exclusive device
ownership, cursor routing and cleanup require their own hardware experiment.

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
| Native DeX and physical mouse | Inactive; crossing and successful native placement remain untested |

Framework references:

- [Android multi-display practices](https://source.android.com/docs/core/display/multi_display/recommended-practices)
- [DisplayTopology](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/android17-release/core/java/android/hardware/display/DisplayTopology.java)
- [Topology membership and rearrangement validation](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/android17-release/services/core/java/com/android/server/display/DisplayTopologyCoordinator.java)
