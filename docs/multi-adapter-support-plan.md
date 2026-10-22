# Multiple USB adapters: support plan

Date: 2026-10-22. Source baseline: `dd9c1a7`.
Status: **M01 validated by reload on one adapter. M02 is implemented and builds; sysfs runtime validation is pending. Mixed-adapter validation remains unavailable.**

## Objective

Support two and three simultaneously connected MS912x/MS913x USB display
adapters with independent image state, hotplug, recovery, and pacing. Three
devices are an initial validation target, not a driver-imposed maximum.

Retain one DRM device per matching USB display instance. The Windows limit
of three opened adapters does not justify adding a Linux count limit or
exposing three CRTCs on one adapter. See the
[vendor evidence](multi-output-vendor-analysis.md).

This plan covers separate USB display devices, including separate chips
behind a hub. Independent HDMI/VGA outputs inside one chip need their own
protocol investigation before choosing a multiple-head DRM design.

## Existing foundation

| Area | Current implementation | Assessment |
|---|---|---|
| Enumeration | `ms912x_usb_probe()` allocates an `ms912x_device` and DRM device per matching interface | Basic multi-adapter structure already exists; no explicit three-device cap found |
| Display objects | Connector, encoder, CRTC, primary plane, and cursor plane belong to that instance | Keep this ownership |
| Images | Capture slots, baselines, conversion snapshots, cursor buffers, damage history, and request buffers are per device | Preserve isolation and current buffer ownership |
| Concurrency | Per-device ordered USB and conversion workqueues; per-device scan work queued on `system_dfl_wq` | Separate adapters can make progress independently; actual fairness needs measurement |
| Transport | USB handle/interface, bulk pipe, control mutex, and request timers are per device | No new shared USB sender is needed for separate adapters |
| Pacing | Throughput estimate, pending bytes, watermarks, and refresh state are per device | Useful foundation for shared-bus contention |
| Lifecycle | Disconnect, suspend/resume, reset work, and DRM teardown use the instance | Audit and validate cross-device isolation |
| Statistics | Counters belong to the device; debugfs entries resolve their DRM device | Add clear identity and resolved configuration |
| Parameters | Most requested settings and generation counters are module-global | Keep module parameters as defaults; add per-device overrides through USB-interface sysfs |

Primary source areas: [probe and lifecycle](../ms912x_drv.c),
[device state](../ms912x.h), [configuration](../ms912x_params.c),
[pacing](../ms912x_transfer.c), [USB accounting](../ms912x_usb_transfer.c),
and [debugfs](../ms912x_debugfs.c).

These are source-level findings. Only one connected adapter was available
during the investigation; simultaneous operation is not yet hardware-verified.

## Configuration issue addressed by M01

Before M01, `ms912x_profile_configure_device()` resolved `profile=auto` from
the first adapter's negotiated USB speed and retained that module-global
result. A later adapter with a different speed class inherited the earlier
profile. Probing also published shared effective scan settings.

Consequences:

- USB 2 and USB 3 adapters can inherit inappropriate defaults depending on
  connection order.
- Reversing device order can change the effective policy of the setup.
- A module-global `effective_profile` cannot accurately describe a mixed setup.

M01 removed the probe-order dependency from automatic profile selection. The
user confirmed that reloading the module works with the available adapter;
mixed USB-speed behavior still needs a multi-adapter setup to validate.

## Planned changes and review gates

Implement one phase at a time. For each implementation phase, run its focused
validation when authorized, request hardware confirmation where needed, and
commit after the user's confirmation.

### M01 — Resolve automatic configuration per adapter

Status: **Implemented; single-adapter reload confirmed, mixed-adapter validation pending.**

- [x] Separate requested module policy and explicit overrides from effective
  device configuration.
- [x] Store each adapter's effective profile and resolved scan defaults in its
  device state. Resolve `auto` from that adapter's own negotiated USB speed.
- [x] Preserve existing profile values, override precedence, tolerance buckets,
  and the RGB565 wire-mask exclusions.
- [x] Publish a coherent configuration snapshot at existing frame/configuration
  boundaries. Reuse the established locking and generation mechanisms.
- [x] Make global runtime parameter changes update every registered adapter's
  resolved settings, without making a new probe change another device's
  automatic profile.
- [x] Keep module policy inputs separate from per-device effective settings;
  report effective settings in per-device debugfs and probe logs.

Keep parameter interpretation outside pixel loops. Scan, conversion, masking,
and downscale SIMD implementations remain reusable without algorithm changes.

**Gate:** mixed-speed resolution is independent of probe order; explicit
overrides take precedence consistently; a single adapter retains its intended
behavior.

### M02 — Add per-adapter sysfs overrides

Status: **Implemented and runtime-confirmed on the available single adapter; mixed-adapter isolation remains pending.**

Expose a writable `ms912x_profile` attribute on the bound USB interface so
udev can identify a specific adapter without relying on transient DRM `cardN`
numbering. It accepts `inherit` to return to the module parameter default. A
per-device profile of `auto` resolves from that interface's own USB speed.

- [x] Keep the `profile` module parameter as the default for devices that
  inherit it.
- [x] Store the per-device requested profile and override flag in
  `struct ms912x_device`; apply it only to that adapter's scan policy.
- [x] Create the sysfs group on the USB interface after per-device state is
  ready, before probe completes, so attributes are present when the driver bind
  event reaches udev.
- [x] Report configured/effective values per adapter in debugfs and probe logs.
- [x] Keep `damage_scan_method` and `fast_chroma` global because they select
  host ISA/CPU implementations shared by every adapter.
- [x] Preserve global `primary_format` and `cursor_plane` load-time overrides
  as diagnostic controls; do not make them per-device in this phase.

**Gate:** one adapter accepts and reports its profile override; `inherit`
restores the module default; changing one adapter's profile does not change
another's.

### M03 — Move remaining runtime policy to per-adapter sysfs

Status: **Scan overrides validated and committed; per-adapter scale implemented and builds, runtime validation pending.**

- [x] Add per-device `damage_scan`, stride, and tolerance settings. These take
  effect on the next scan and invalidate only that adapter's scan state.
- [x] Add per-device scale setting. It takes effect at that adapter's next
  modeset/reconfiguration.
- [ ] Add per-device fake-EDID and force-connected policy if they remain useful
  as runtime controls; keep host-wide controls global.
- [x] Keep `damage_scan_method` and `fast_chroma` module/host-wide.
- [x] Keep module parameters as defaults and `inherit` as the reset value for
  per-device scan and scale overrides. Preserve the existing parsing and
  validation rules.
- [ ] Apply the same inheritance model to any retained EDID controls.
- [x] Keep load-time-only global `primary_format` and `cursor_plane` controls.
- [ ] Document which controls take effect immediately and which require a DRM
  modeset or reprobe.

Current M03 scan attributes are `ms912x_damage_scan` (`auto|on|off`),
`ms912x_damage_scan_stride` (`1` through `16`), and
`ms912x_damage_scan_tolerance` (a bucketed value or range from `0` through
`31`). Each accepts `inherit`. These settings take effect on the next scan;
changing one invalidates scan state only for that adapter. The per-device
override takes precedence over an explicit module parameter, which takes
precedence over the device's profile default.

The `ms912x_scale` attribute accepts `auto`, `50`, `67`, `80`, `100`, and
`inherit`. An override triggers a hotplug notification for that adapter only;
the selected setting is applied when that adapter next reconfigures its mode.
Scale `auto` continues to apply the existing per-adapter USB-speed policy.

**Gate:** writes are isolated by adapter and invalidate only that device's
cached scan/configuration state where possible.

### M04 — Make global notification fan-out lifetime-safe and bounded

The device registry is protected by `ms912x_devices_lock`. Current notification
paths can call hotplug handling or force-refresh logic while holding this lock.
That is an audit target, not a demonstrated multi-device deadlock.

- [ ] Review lock order across parameter setters, registry operations, frame
  publication, hotplug handling, and disconnect.
- [ ] Where callbacks can block or re-enter driver code, take references to a
  registry snapshot under the registry lock and perform notifications after
  releasing it. Handle allocation failure and release every reference.
- [ ] Pair retained DRM/device lifetime with an explicit disconnected/stopping
  protocol and the appropriate unplug-aware entry guard. A retained allocation
  alone must not authorize USB work after unplug; callbacks already in progress
  must participate in the device's stop/drain synchronization.
- [ ] Ensure a probe failure or disconnected adapter cannot leave a registry
  entry, hold up notifications indefinitely, or cause access to another
  adapter's queues.

**Gate:** parameter changes racing with removal of one adapter remain safe;
the remaining adapters continue presenting frames.

### M05 — Identify devices and keep display policy consistent

- [ ] Expose the USB device/interface path, VID/PID, negotiated speed, current
  primary format, effective profile, and effective scale in per-device debugfs.
- [ ] Use USB topology/interface identity to correlate DRM devices, logs, and
  measurements. Treat `cardN` numbers and USB device addresses as transient.
- [ ] Add optional serial identity only when available; identical VID/PID pairs
  must still be distinguishable.
- [ ] Check DRM client setup against `primary_format`. The current probe passes
  RGB565 unconditionally to `drm_client_setup()`; client preference must agree
  with each device's exposed primary formats, including XRGB-only policy.
- [x] Keep fixed load-time settings (`primary_format` and cursor-plane
  exposure) global, as diagnostic overrides.
- [ ] Audit VRR policy separately; keep it global unless adapter-specific use
  is demonstrated.

**Gate:** each display maps unambiguously to an adapter; client setup and
advertised formats agree for all four primary-format policies.

### M06 — Validate transport, CPU, and memory scaling

- [ ] Measure per-adapter throughput and queue pressure on separate controllers
  and on a shared USB 2 hub/root-port path. Record topology rather than assuming
  all adapters share one bandwidth budget.
- [ ] Confirm that each device's throughput estimator and watermarks respond
  to contention, and that one busy adapter does not starve an interactive one.
- [ ] Preserve the existing USB 3 `scale=auto` policy selecting 100%, and the
  established USB 2 input-width restrictions. Shared-bus observations alone
  must not silently redefine these policies.
- [ ] Retain per-device ordered transfer/conversion queues. Investigate worker
  scheduling or bounded concurrency changes only if measurements show CPU
  starvation or excessive runnable work.
- [ ] Account for successful requests, timeouts, short transfers, pending work,
  and scan/conversion latency separately for every adapter.
- [ ] Measure driver allocation cost and allocation-failure cleanup at two and
  three devices before changing buffer allocation strategy.

Current probe allocates seven full-size image rasters per adapter: two scan
baselines, one RGB565 scale scratch image, latest/conversion snapshots, and two
capture slots. At `1920 * 1200 * 4`, these total **61.5 MiB per adapter**, or
**184.6 MiB for three**, before USB request buffers, cursors, metadata, and DRM
framebuffers. Packed RGB565 reduces active bytes touched, but these maximum
capacity allocations remain. This is allocation accounting, not measured RSS.

The initial implementation keeps the current buffer ownership and SIMD packing.
USB bandwidth is arbitrated by the kernel and hardware; a new module-global
transfer lock or global scheduler is not part of the initial design. A
topology-aware admission or fairness mechanism needs a demonstrated problem
and a separate proposal.

**Gate:** report individual and aggregate results; preserve responsiveness and
bounded queues. Do not equate completed USB requests with displayed FPS.

### M07 — Exercise multi-instance lifecycle and error isolation

- [ ] Add direct fixtures with at least two independent device instances for
  configuration resolution, generation changes, registry membership, and
  notification fan-out.
- [ ] Cover removal during scan, conversion, USB transfer, mode changes, and a
  global configuration update. Include partial probe/allocation failures.
- [ ] Check that EDID caches, cursor state, failed-damage replay, scale state,
  and counters cannot migrate between instances.
- [ ] Exercise suspend/resume and an adapter reset while other adapters remain
  active. Driver recovery must target the affected device; a hub/controller
  failure can legitimately affect multiple devices.
- [ ] Complete the hardware matrix below and record any untested combinations.

**Gate:** no stale registry entries, work-after-unplug, cross-device image
corruption, counter contamination, or driver-induced stall of another adapter.

## Hardware validation matrix

All rows below are planned, not completed.

| Setup | Workload/action | Required observation |
|---|---|---|
| One adapter | Existing scalar/SIMD, scan and scale cases | Baseline behavior retained |
| Two identical adapters | Distinct static images, then independent scrolling | Correct adapter/image mapping and isolated damage history |
| Two identical adapters | Video on A, cursor/typing on B | Responsive B and bounded queues on both |
| USB 2 plus USB 3 | Connect in both orders; reconnect the first | Per-device automatic profiles independent of order |
| Two adapters | Different modes, refresh rates, and scales | Independent geometry, pacing, and recovery |
| Two adapters | RGB565 framebuffer on A, XRGB8888 on B when both formats are exposed | Correct native wire path and scan masks on each |
| Exclusive primary-format policy | Reload with each exclusive setting | Every instance advertises and initializes a compatible format |
| Two or three on one hub | Simultaneous motion, then one idle display | Contention visible in per-device accounting; no starvation |
| Three adapters | Sustained updates on all outputs | Stable operation within measured CPU/USB limits |
| Two or three adapters | HDMI unplug, USB unplug, reset, and suspend/resume | Other devices continue unless the shared bus itself is disrupted |
| Two or three adapters | Repeated probe failure and reconnect | No leaked references, registry entries, work, or image buffers |

For performance comparisons, use matched workloads and record distributions
for input/update intervals, scan/conversion time, queue pressure, and completed
USB work per adapter. Keep observed display behavior separate from CPU timing
and transfer counters. Record transfer sizes because small-transfer throughput
can differ substantially from large-transfer throughput.

## Initial delivery criteria

- [x] Existing per-device ownership and shared policy audited at source level.
- [x] Vendor three-device limit distinguished from multiple outputs per chip.
- [x] M01: automatic profile resolution per adapter (single-adapter reload confirmed).
- [x] M02: per-adapter sysfs profile override (build passed; sysfs runtime pending).
- [ ] M03: remaining runtime per-adapter controls.
- [ ] M04: bounded, lifetime-safe global notifications.
- [ ] M05: device identity and consistent primary-format/client policy.
- [ ] M06: concurrency, memory, and shared-bus measurements.
- [ ] M07: lifecycle/error fixtures and two-/three-adapter hardware confirmation.

Next step: validate M02 with the available single adapter, then continue to M03.
