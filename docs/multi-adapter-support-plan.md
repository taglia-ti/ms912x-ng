# Multiple USB adapters: support plan

Date: 2026-10-22. Source baseline: `dd9c1a7`.
Status: **M01–M05 are implemented and validated on the available single adapter. M06 source audit is complete; measurements with multiple adapters remain pending.**

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

Status: **Complete and validated on the available adapter.**

- [x] Add per-device `damage_scan`, stride, and tolerance settings. These take
  effect on the next scan and invalidate only that adapter's scan state.
- [x] Add per-device scale setting. It takes effect at that adapter's next
  modeset/reconfiguration.
- [x] Add per-device fake-EDID and force-connected policy as runtime controls;
  keep module parameters as inherited defaults.
- [x] Keep `damage_scan_method` and `fast_chroma` module/host-wide.
- [x] Keep module parameters as defaults and `inherit` as the reset value for
  per-device scan, scale, EDID, and connection overrides. Preserve the
  existing parsing and validation rules.
- [x] Keep load-time-only global `primary_format` and `cursor_plane` controls.
- [x] Document when each control takes effect and which controls are load-time
  only.

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

The `ms912x_fake_edid` and `ms912x_force_connected` attributes accept the same
`auto|on|off` values as their module parameters, plus `inherit`. Changes reset
that adapter's connector-detection cache and notify only its DRM device. A
fake-EDID policy change also clears that adapter's cached EDID blocks.

Scan settings apply on the next scan. Scale applies at the next mode
reconfiguration. EDID and connection policies apply on the next connector
probe after their per-device hotplug event. `damage_scan_method` and
`fast_chroma` remain host-wide runtime settings; `primary_format` and
`cursor_plane` remain load-time-only module settings.

**Gate:** writes are isolated by adapter and invalidate only that device's
cached scan/configuration state where possible.

### M04 — Make global notification fan-out lifetime-safe and bounded

Status: **Complete; build passed and unplug behavior was confirmed on the available adapter. Multi-adapter race validation remains pending.**

The device registry is protected by `ms912x_devices_lock`. Current notification
paths can call hotplug handling or force-refresh logic while holding this lock.
That is an audit target, not a demonstrated multi-device deadlock.

- [x] Review lock order across parameter setters, registry operations, frame
  publication, hotplug handling, and disconnect.
- [x] Where callbacks can block or re-enter driver code, take references to a
  registry snapshot under the registry lock and perform notifications after
  releasing it. Handle allocation failure and release every reference.
- [x] Pair retained DRM/device lifetime with an explicit disconnected/stopping
  protocol and the appropriate unplug-aware entry guard. A retained allocation
  alone must not authorize USB work after unplug; callbacks already in progress
  must participate in the device's stop/drain synchronization.
- [x] Ensure a probe failure or disconnected adapter cannot leave a registry
  entry, hold up notifications indefinitely, or cause access to another
  adapter's queues.

**Gate:** parameter changes racing with removal of one adapter remain safe;
the remaining adapters continue presenting frames.

The registry snapshot takes a DRM reference while holding the registry mutex,
then performs hotplug or refresh work after releasing it. Hotplug uses
`drm_dev_enter()`; full-refresh work also checks unplug state and
`transfer_stopping` under the transfer lock. Snapshot allocation failure emits
a rate-limited warning and skips that notification pass.

### M05 — Identify devices and keep display policy consistent

Status: **Complete; build and runtime behavior confirmed on the available adapter. Multi-adapter identity mapping remains pending.**

- [x] Expose the USB device/interface path, VID/PID, negotiated speed, current
  primary format, effective profile, and effective scale in per-device debugfs.
- [x] Use USB topology/interface identity to correlate DRM devices, logs, and
  measurements. Treat `cardN` numbers and USB device addresses as transient.
- [x] Add optional serial identity only when available; identical VID/PID pairs
  must still be distinguishable.
- [x] Check DRM client setup against `primary_format`. The current probe passes
  RGB565 unconditionally to `drm_client_setup()`; client preference must agree
  with each device's exposed primary formats, including XRGB-only policy.
- [x] Keep fixed load-time settings (`primary_format` and cursor-plane
  exposure) global, as diagnostic overrides.
- [x] Audit VRR policy separately; keep it global unless adapter-specific use
  is demonstrated.

The debugfs `usb_interface_path` is the current sysfs interface path; use it
with VID/PID and optional serial to correlate adapters across reconnects. The
client format hint now follows the configured preference: RGB565 for RGB565-only
and efficiency modes, XRGB8888 for XRGB-only and quality modes. VRR remains a
load-time module-wide property because no per-adapter VRR requirement was
identified.

**Gate:** each display maps unambiguously to an adapter; client setup and
advertised formats agree for all four primary-format policies.

### M06 — Validate transport, CPU, and memory scaling

Status: **Per-adapter scan scheduling implemented and source-audited; multi-adapter measurements remain pending.**

- [ ] Measure per-adapter throughput and queue pressure on separate controllers
  and on a shared USB 2 hub/root-port path. Record topology rather than assuming
  all adapters share one bandwidth budget.
- [ ] Confirm that each device's throughput estimator and watermarks respond
  to contention, and that one busy adapter does not starve an interactive one.
- [ ] Preserve the existing USB 3 `scale=auto` policy selecting 100%, and the
  established USB 2 input-width restrictions. Shared-bus observations alone
  must not silently redefine these policies.
- [x] Give each adapter an ordered scan workqueue, matching its existing
  per-device ordered USB and conversion queues. This removes scan work from the
  shared system workqueue and preserves per-adapter scan ordering.
- [ ] Measure CPU fairness and queue latency before considering priority,
  worker-pool, or bounded-concurrency changes. Separate workqueues do not
  isolate CPU, cache, memory-bandwidth, or USB-bus contention.
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

Source audit on 2026-10-22 confirmed that each adapter owns its ordered USB,
conversion, and scan workqueues, pacing state, throughput estimate, pending
queue, USB outcome counters, and scan/conversion statistics. The scan worker
was moved from the shared `system_dfl_wq` to a per-adapter ordered queue so
unrelated system work and other adapters' scans do not share that queue's
ordering domain. This does not reserve CPU time or eliminate resource
contention. The USB 3 auto-scale path selects native scale, while USB 2
selection applies the existing input-width and bandwidth rules. These are
code-level observations, not evidence of measured CPU fairness or memory cost.

**Gate:** report individual and aggregate results; preserve responsiveness and
bounded queues. Do not equate completed USB requests with displayed FPS.

### M07 — Exercise multi-instance lifecycle and error isolation

Status: **Partial; scan/removal/conversion/configuration/registry/EDID/statistics/replay/USB-completion/lifecycle-isolation and in-flight worker-drain fixtures pass `make test-entrypoints` and `make test-damage`; cursor snapshot isolation passes `make test-cursor`; scale override snapshots pass `make test-downscale-5to4`.**

- [x] Add a direct two-instance scan fixture covering independent scan queues,
  frame storage, sequence values, and generation state.
- [x] Test automatic profile resolution for mixed USB speeds and explicit
  per-adapter overrides in both probe orders.
- [x] Test per-device scan stride/tolerance and local/global generation
  invalidation with two independent scan states.
- [x] Test scale snapshots with one inheriting adapter and one explicit
  override, including independent device generations.
- [x] Add direct fixtures for registry membership and notification fan-out,
  including removal after snapshot, unplug guards, and snapshot-allocation
  failure.
- [x] Invalidate one adapter's EDID cache and verify the other adapter's cache
  remains valid.
- [x] Reset and update one adapter's statistics while verifying the other
  adapter's counters and session start time remain unchanged.
- [x] Snapshot cursor updates for two adapters and verify independent cursor
  geometry and pixel buffers.
- [x] Carry failed damage for one adapter and verify the other adapter's
  replay geometry and generation stay independent.
- [x] Complete failed USB requests for two adapters and verify each failure
  publishes replay state and schedules follow-up work only for its owner.
- [x] Stop and restart one adapter while verifying the other adapter's pending
  work, request slots, scan frames, invalidations, and generations remain
  unchanged.
- [x] Invalidate a capture during removal of one adapter and verify the other
  adapter's scan queue, frame, and epoch continue independently.
- [x] Complete a conversion after its adapter enters stop and verify it does
  not queue USB work or take replay ownership from another adapter.
- [x] Start a USB worker after its adapter is unplugged and verify it releases
  only that adapter's request slot.
- [x] Verify transfer stop waits for an in-flight USB request worker and does
  not change another adapter's active request state. The fixture models the
  synchronous drain performed by `cancel_work_sync`; it does not exercise a
  real USB transaction.
- [x] Reject stale damage-scan results after a module-wide scan configuration
  update and exercise a scan dimension change while preserving the other
  adapter's dimensions.
- [ ] Cover other mode/configuration changes and partial probe/allocation
  failures.
- [x] Cover failed-damage request requeue isolation. EDID, scale snapshots,
  statistics, cursor request snapshots, failed-rectangle ownership, USB
  completion, and requeue now have direct fixtures.
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
- [x] M02: per-adapter sysfs profile override (single-adapter runtime confirmed).
- [x] M03: remaining runtime per-adapter controls (single-adapter runtime confirmed).
- [x] M04: bounded, lifetime-safe global notifications (unplug behavior confirmed).
- [x] M05: device identity and consistent primary-format/client policy (single-adapter runtime confirmed).
- [ ] M06: concurrency, memory, and shared-bus measurements.
- [ ] M07: lifecycle/error fixtures and two-/three-adapter hardware confirmation.

Next step: M06 still needs simultaneous-adapter measurements. Hardware-
independent M07 fixtures can be implemented in parallel with that hardware
dependency, but they do not replace the two-/three-adapter validation matrix.
