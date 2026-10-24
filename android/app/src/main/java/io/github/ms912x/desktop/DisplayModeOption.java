package io.github.ms912x.desktop;

import java.util.Comparator;
import java.util.List;

/** A timing from the shared Linux/Android adapter catalog. */
final class DisplayModeOption {
    static final Comparator<DisplayModeOption> HIGHEST_FIRST = Comparator
            .comparingLong((DisplayModeOption mode) -> (long)mode.width * mode.height)
            .thenComparingInt(mode -> mode.width)
            .thenComparingInt(mode -> mode.height)
            .thenComparingInt(mode -> mode.refreshHz).reversed();
    final int id;
    final int width;
    final int height;
    final int refreshHz;
    final int clockKHz;
    final int htotal;
    final int vtotal;
    final int hoffset;
    final int voffset;
    final int hsyncWidth;
    final int vsyncWidth;
    final int polarity;

    DisplayModeOption(int id, int width, int height, int refreshHz,
                      int clockKHz, int htotal, int vtotal,
                      int hoffset, int voffset, int hsyncWidth,
                      int vsyncWidth, int polarity) {
        this.id = id;
        this.width = width;
        this.height = height;
        this.refreshHz = refreshHz;
        this.clockKHz = clockKHz;
        this.htotal = htotal;
        this.vtotal = vtotal;
        this.hoffset = hoffset;
        this.voffset = voffset;
        this.hsyncWidth = hsyncWidth;
        this.vsyncWidth = vsyncWidth;
        this.polarity = polarity;
    }

    @Override
    public String toString() {
        return width + "×" + height + " @ " + refreshHz + " Hz";
    }

    boolean advertisedBy(byte[] edid) {
        return EdidModeMatcher.advertisedBy(this, edid);
    }

    static DisplayModeOption preferredFrom(byte[] edid, List<DisplayModeOption> supported) {
        return EdidModeMatcher.preferredFrom(edid, supported);
    }

}
