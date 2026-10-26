package io.github.taglia_ti.ms912x_ng.desktop;

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
        List<DisplayModeOption> ordered = new ArrayList<>();
        for(DisplayModeOption mode:modes){
            // Never retain a mode the shell helper will reject before creating
            // a VirtualDisplay. The selector and preferred timing use this
            // filtered snapshot, not the raw adapter/EDID intersection.
            if(mode.width>0&&mode.height>0&&mode.width<=8192&&mode.height<=8192
                    &&(long)mode.width*mode.height<=16_777_216L)ordered.add(mode);
        }
        ordered.sort(DisplayModeOption.HIGHEST_FIRST);
        this.modes = Collections.unmodifiableList(ordered);
        preferredMode = DisplayModeOption.preferredFrom(edid, ordered);
        initialMode = preferredMode != null ? preferredMode : ordered.isEmpty() ? null : ordered.get(0);
    }
}
