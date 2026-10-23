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

    AdapterCapabilities(UsbDevice device, String monitorName, List<DisplayModeOption> modes) {
        this.device = device;
        this.monitorName = monitorName;
        this.modes = Collections.unmodifiableList(new ArrayList<>(modes));
    }
}
