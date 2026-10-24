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
        return resolutionLabel() + " @ " + refreshHz + " Hz";
    }

    String resolutionLabel() {
        return width + "×" + height + " (" + aspectRatio(width, height) + ")";
    }

    static String aspectRatio(int width, int height) {
        if (width <= 0 || height <= 0) return "—";
        int divisor = gcd(width, height);
        int ratioWidth = width / divisor;
        int ratioHeight = height / divisor;

        int[][] common = {
                {1, 1}, {4, 3}, {5, 4}, {3, 2}, {16, 10},
                {16, 9}, {18, 9}, {20, 9}, {21, 9}
        };
        for (int[] ratio : common) {
            long difference = Math.abs((long) width * ratio[1] - (long) height * ratio[0]);
            if (difference * 100 <= (long) height * ratio[0] * 2) {
                return ratio[0] + ":" + ratio[1];
            }
        }
        return ratioWidth + ":" + ratioHeight;
    }

    private static int gcd(int left, int right) {
        while (right != 0) {
            int remainder = left % right;
            left = right;
            right = remainder;
        }
        return left;
    }

    boolean advertisedBy(byte[] edid) {
        return EdidModeMatcher.advertisedBy(this, edid);
    }

    static DisplayModeOption preferredFrom(byte[] edid, List<DisplayModeOption> supported) {
        return EdidModeMatcher.preferredFrom(edid, supported);
    }

}
