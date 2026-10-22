# Multiple outputs: Windows and macOS evidence

Analysis date: 2026-10-22. Driver baseline: `dd9c1a7`.
Evidence priority: **Windows > macOS**. This investigation inspected existing
static artifacts and USB descriptors exposed by sysfs. No vendor executable
was run, USB configuration was changed, or additional monitor was connected.

## Conclusion

**The recovered three-device limit belongs to the Windows application's USB
adapter enumeration. It does not establish three independent displays per
adapter, or a silicon limit of three adapters.**

The supplied drivers do not establish multiple independent display engines
inside one USB device. The inspected paths configure one image stream per
opened adapter. Whether the HDMI and VGA connectors on a particular product
can display different images remains unverified.

## Windows: the limit counts USB devices

The supplied installer contains two versions of `WinUsbDisplay.exe`:

| Version | Enumeration evidence | Meaning |
|---|---|---|
| 1.0.0.7 | VID/PID comparison at `0x442320`; count check at `0x442367`; `usb_open` call at `0x44237C` | Reject another matching USB device when three are already tracked |
| 1.0.1.6 | VID/PID comparison at `0x441C90`; count check at `0x441CD7`; device-open call at `0x441CEC` | The same three-device policy |

Both paths enumerate `534d:6021`, avoid adding an already tracked device, and
assign available slots 0, 1, or 2 to separately opened USB devices. The counter
is incremented when a device is inserted into that list. This placement makes
the limit a host application policy, rather than a count of connectors or
display engines read from the adapter.

In version 1.0.0.7, the import table resolves `0x4D7530` to `usb_open` and
`0x4D7534` to `usb_get_busses`. The bulk wrapper at `0x44D470` obtains the
USB handle from its object and calls `usb_bulk_write` on endpoint `0x04`.
The `screenNum` slot is host-side device bookkeeping; no corresponding
destination field was found in the inspected rectangle framing.

The installer also contains three indirect display driver sets, numbered
0, 1, and 2. Their presence is consistent with the host-side slot arrangement;
it is not evidence that one adapter has three physical outputs.

Primary artifacts, under the ignored `drivers/` directory:

- [1.0.0.7 enumeration](../drivers/windows-analysis/pe/app/WinUsbDisplay.exe$0/disassembly.asm): `0x4422E8` through `0x442482`.
- [1.0.0.7 imports](../drivers/windows-analysis/pe/app/WinUsbDisplay.exe$0/metadata.json).
- [1.0.1.6 enumeration](../drivers/windows-analysis/pe/app/WinUsbDisplay.exe/disassembly.asm): `0x441C61` through `0x441DD9`.
- [1.0.0.7 bulk wrapper](../drivers/partial-update-analysis/windows-1007-bulk-write-wrapper.asm).

Binary hashes and extraction provenance are recorded in the
[Windows analysis](windows-vs-android-macos-analysis.md).

## macOS: one selected adapter and one stream in the inspected path

The macOS helper's `_test_device` calls
`libusb_open_device_with_vid_pid` at `0x100075BBC`. Its transfer wrapper,
`_ms2160_bulk_transfer` at `0x1000776C0`, uses the global `_g_handle` and
endpoint `0x04`. The control serializers use that same handle.

The normal path programs one input image and one output timing. No per-output
destination selector was located in these paths. Host monitor enumeration in
the capture library does not establish multiple hardware display engines.
This observation describes the supplied application's path, not a universal
macOS or chip-family limit.

Primary artifacts:

- [Device opening](../drivers/macos-analysis/native/functions/test_device.asm).
- [Bulk transfer](../drivers/macos-analysis/native/functions/ms2160_bulk_transfer.asm).
- [VIDEO_IN serializer](../drivers/macos-analysis/native/functions/set_video_in.asm).
- [Symbols](../drivers/macos-analysis/native/symbols.txt).

See the [macOS analysis](macos-vs-android-analysis.md) for binary identity.

## Protocol and connected-device observations

The recovered `VIDEO_IN` and `VIDEO_OUT` commands describe image geometry,
pixel format, and timing. The ordinary mode-3 header describes rectangle
position and size. No monitor ID has been identified in those command paths
or rectangle headers. A separate register-bank or routing command remains
possible, but has not been established by the inspected code.

The connected `534d:6021` device exposed the following sysfs descriptors:

| Interface | Function | Endpoint |
|---|---|---|
| 0 | HID | Interrupt IN `0x81` |
| 1 | USB audio control | No video endpoint |
| 2 | USB audio streaming | No additional video endpoint in the observed setting |
| 3 | Vendor video interface, bound to `ms912x` | Bulk OUT `0x04` |

The owner reports two physical connectors, HDMI and VGA. One USB video
interface does not rule out a multiplexed protocol. Equally, two physical
connectors do not prove independent images, EDIDs, hotplug status, or timings.
Shared-image output or selection between connectors is consistent with the
evidence, but has not been demonstrated with both monitors connected.

## Validation needed before planning multiple heads on one adapter

1. Connect HDMI and VGA simultaneously using the vendor Windows driver.
2. Determine whether Windows exposes two independently configurable external
   monitors, excluding the computer's original display from the count.
3. Display different content and, if supported, different timings on each.
4. Capture USB topology, EDID access, setup commands, and pixel transfers while
   updating or disconnecting only one monitor.
5. Trace any routing mechanism back to the vendor sender and distinguish a
   second USB device from a second output within the same device.

The implementation architecture follows the observed topology:

| Confirmed behavior | Suitable DRM model |
|---|---|
| Separate USB display devices, including devices behind an internal hub | One DRM device per USB display instance |
| One image driving both HDMI and VGA | One CRTC; separate cloned connectors only if individual connector control and detection are established |
| Independent outputs inside one USB device | One DRM device with per-output CRTCs, planes, image state, and a proven transport destination mechanism |

Independent outputs in one device require further protocol evidence. Support
for multiple USB adapters can proceed using the existing per-device model;
see the [multi-adapter plan](multi-adapter-support-plan.md).

## Conditional design if independent outputs are demonstrated

Keep one DRM device for the physical USB adapter and create a CRTC, primary
plane, optional cursor plane, and connector for each proven output. Move
geometry, EDID/hotplug state, image snapshots, damage history, and presentation
queues into per-output state. Callbacks must identify the relevant output
instead of recovering the only display state from `connector->dev`.

Keep shared USB transport, device reset, and any global routing controls in
the adapter state. A transport destination mechanism must be established for
both configuration and image updates. If a device-wide selector exists, its
selection and the operation using it must be serialized together. Preserve
complete batch boundaries and prevent one output's modeset from redirecting
another output's in-flight pixels. Reuse the existing scan/conversion kernels.

Bandwidth also needs joint accounting within such an adapter. Three full
1920x1080 images at 60 updates/s and two bytes/pixel would require
`3 * 1920 * 1080 * 60 * 2 = 746,496,000` payload bytes/s, before framing.
This calculation describes full-image USB traffic, not monitor scanout rate
or the traffic required by damage-only updates.

For shared-image HDMI/VGA output, one CRTC is sufficient. Separate connector
objects require evidence for their detection, EDID, and enable controls, with
compatible timing constraints for the shared raster. Creating extra software
CRTCs alone cannot establish independent hardware images.
