package io.github.ms912x.desktop;

import android.hardware.usb.UsbDevice;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** A snapshot of the selected adapter and its monitor's validated EDID modes. */
final class AdapterCapabilities {
    final UsbDevice device;
    final String monitorName;
    final List<DisplayModeOption> modes;
    final DisplayModeOption preferredMode;
    final DisplayModeOption initialMode;

    AdapterCapabilities(UsbDevice device, String monitorName, List<DisplayModeOption> modes) {
        this(device, monitorName, modes, null);
    }

    AdapterCapabilities(UsbDevice device, String monitorName, List<DisplayModeOption> modes, byte[] edid) {
        this.device = device;
        this.monitorName = monitorName;
        List<DisplayModeOption> ordered = new ArrayList<>(modes);
        ordered.sort(DisplayModeOption.HIGHEST_FIRST);
        this.modes = Collections.unmodifiableList(ordered);
        preferredMode = DisplayModeOption.preferredFrom(edid, ordered);
        initialMode = preferredMode != null ? preferredMode : ordered.isEmpty() ? null : ordered.get(0);
    }
}
