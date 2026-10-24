package io.github.ms912x.desktop;

import java.util.List;

/** EDID timing matching kept separate from the adapter mode value object. */
final class EdidModeMatcher {
    private final DisplayModeOption mode;

    private EdidModeMatcher(DisplayModeOption mode) {
        this.mode = mode;
    }

    static boolean advertisedBy(DisplayModeOption mode, byte[] edid) {
        EdidModeMatcher matcher = new EdidModeMatcher(mode);
        if (edid == null || edid.length < 128 || edid.length % 128 != 0) {
            return false;
        }
        if (matcher.supportsEstablished(edid) || matcher.supportsStandard(edid)
                || matcher.supportsDetailed(edid, 0, 54, 126)) {
            return true;
        }
        for (int block = 1; block < edid.length / 128; ++block) {
            int base = block * 128;
            if ((edid[base] & 0xff) == 0x02 && matcher.supportsCta(edid, base)) {
                return true;
            }
        }
        return false;
    }

    /** Select only supported preferred/native timings; otherwise leave fallback to the caller. */
    static DisplayModeOption preferredFrom(byte[] edid, List<DisplayModeOption> supported) {
        if (edid == null || edid.length < 128 || edid.length % 128 != 0) return null;
        // Linux DRM treats the first base DTD as preferred for EDID 1.4+, or when flagged.
        if (edid[18] == 1 && ((edid[19] & 0xff) >= 4 || (edid[24] & 2) != 0)) {
            for (DisplayModeOption mode : supported) {
                if (new EdidModeMatcher(mode).supportsDetailed(edid, 0, 54, 72)) return mode;
            }
        }
        for (int base = 128; base < edid.length; base += 128) {
            if ((edid[base] & 0xff) != 2) continue;
            int dtdStart = edid[base + 2] & 0xff;
            if (dtdStart != 0 && (dtdStart < 4 || dtdStart > 127)) continue;
            int end = dtdStart == 0 ? base + 127 : base + dtdStart;
            for (int offset = base + 4; offset < end;) {
                int header = edid[offset++] & 0xff;
                int length = header & 0x1f;
                if (offset + length > end) break;
                if ((header >>> 5) == 2) {
                    for (int i = 0; i < length; ++i) {
                        int svd = edid[offset + i] & 0xff;
                        if ((svd & 0x80) == 0) continue;
                        for (DisplayModeOption mode : supported) {
                            if (new EdidModeMatcher(mode).matchesCtaVic(svd & 0x7f)) return mode;
                        }
                    }
                }
                offset += length;
            }
            if (dtdStart == 0) continue;
            int nativeCount = edid[base + 3] & 0x0f;
            for (int i = 0, offset = dtdStart; i < nativeCount && offset + 18 <= 127;
                    ++i, offset += 18) {
                for (DisplayModeOption mode : supported) {
                    if (new EdidModeMatcher(mode).supportsDetailed(edid, base, offset, offset + 18))
                        return mode;
                }
            }
        }
        return null;
    }

    private boolean supportsDetailed(byte[] edid, int base, int start, int end) {
        for (int offset = start; offset + 18 <= end; offset += 18) {
            int pixelClock = u16(edid, base + offset);
            if (pixelClock == 0) continue;
            int hActive = (edid[base + offset + 2] & 0xff)
                    | ((edid[base + offset + 4] & 0xf0) << 4);
            int hBlank = (edid[base + offset + 3] & 0xff)
                    | ((edid[base + offset + 4] & 0x0f) << 8);
            int vActive = (edid[base + offset + 5] & 0xff)
                    | ((edid[base + offset + 7] & 0xf0) << 4);
            int vBlank = (edid[base + offset + 6] & 0xff)
                    | ((edid[base + offset + 7] & 0x0f) << 8);
            int hFront = (edid[base + offset + 8] & 0xff)
                    | ((edid[base + offset + 11] & 0xc0) << 2);
            int hSync = (edid[base + offset + 9] & 0xff)
                    | ((edid[base + offset + 11] & 0x30) << 4);
            int vFront = ((edid[base + offset + 10] & 0xf0) >>> 4)
                    | ((edid[base + offset + 11] & 0x0c) << 2);
            int vSync = (edid[base + offset + 10] & 0x0f)
                    | ((edid[base + offset + 11] & 0x03) << 4);
            int flags = edid[base + offset + 17] & 0xff;
            boolean separateSync = (flags & 0x18) == 0x18;
            if (pixelClock * 10 == mode.clockKHz && hActive == mode.width
                    && vActive == mode.height && hActive + hBlank == mode.htotal
                    && vActive + vBlank == mode.vtotal && separateSync
                    && (flags & 0x80) == 0 && hSync == mode.hsyncWidth
                    && vSync == mode.vsyncWidth && hBlank - hFront == mode.hoffset
                    && vBlank - vFront == mode.voffset
                    && ((flags >>> 1) & 1) == ((mode.polarity >>> 1) & 1)
                    && ((flags >>> 2) & 1) == ((mode.polarity >>> 2) & 1)) return true;
        }
        return false;
    }

    private boolean supportsEstablished(byte[] edid) {
        int[][] entries = {
                {0, 0, 720, 400, 70}, {0, 1, 720, 400, 88},
                {0, 2, 640, 480, 60}, {0, 3, 640, 480, 67},
                {0, 4, 640, 480, 72}, {0, 5, 640, 480, 75},
                {0, 6, 800, 600, 56}, {0, 7, 800, 600, 60},
                {1, 0, 800, 600, 72}, {1, 1, 800, 600, 75},
                {1, 2, 832, 624, 75}, {1, 3, 1024, 768, 87},
                {1, 4, 1024, 768, 60}, {1, 5, 1024, 768, 70},
                {1, 6, 1024, 768, 75}, {1, 7, 1280, 1024, 75},
                {2, 7, 1152, 870, 75}
        };
        for (int[] entry : entries) {
            if ((edid[35 + entry[0]] & (1 << (7 - entry[1]))) != 0
                    && entry[2] == mode.width && entry[3] == mode.height
                    && entry[4] == mode.refreshHz) return true;
        }
        return false;
    }

    private boolean supportsStandard(byte[] edid) {
        for (int offset = 38; offset + 1 < 54; offset += 2) {
            int first = edid[offset] & 0xff;
            int second = edid[offset + 1] & 0xff;
            if (first == 1 && second == 1) continue;
            int h = (first + 31) * 8;
            int aspect = second >>> 6;
            int v = aspect == 0 ? h * 10 / 16
                    : aspect == 1 ? h * 3 / 4
                    : aspect == 2 ? h * 4 / 5 : h * 9 / 16;
            int refresh = (second & 0x3f) + 60;
            if (h == mode.width && v == mode.height && refresh == mode.refreshHz) return true;
        }
        return false;
    }

    private boolean supportsCta(byte[] edid, int base) {
        int dtdStart = edid[base + 2] & 0xff;
        if (dtdStart != 0 && (dtdStart < 4 || dtdStart > 127)) return false;
        int end = dtdStart == 0 ? base + 127 : base + dtdStart;
        for (int offset = base + 4; offset < end;) {
            int header = edid[offset++] & 0xff;
            int tag = header >>> 5;
            int length = header & 0x1f;
            if (offset + length > end) return false;
            if (tag == 2) {
                for (int i = 0; i < length; ++i) {
                    if (matchesCtaVic(edid[offset + i] & 0x7f)) return true;
                }
            }
            offset += length;
        }
        return dtdStart != 0 && supportsDetailed(edid, base, dtdStart, 127);
    }

    private boolean matchesCtaVic(int vic) {
        switch (vic) {
            case 1: return mode.width == 640 && mode.height == 480 && mode.refreshHz == 60;
            case 2: case 3: return mode.width == 720 && mode.height == 480 && mode.refreshHz == 60;
            case 4: return mode.width == 1280 && mode.height == 720 && mode.refreshHz == 60;
            case 16: return mode.width == 1920 && mode.height == 1080 && mode.refreshHz == 60;
            case 17: case 18: return mode.width == 720 && mode.height == 576 && mode.refreshHz == 50;
            case 19: return mode.width == 1280 && mode.height == 720 && mode.refreshHz == 50;
            case 31: return mode.width == 1920 && mode.height == 1080 && mode.refreshHz == 50;
            case 34: return mode.width == 1920 && mode.height == 1080 && mode.refreshHz == 30;
            default: return false;
        }
    }

    private static int u16(byte[] bytes, int offset) {
        return (bytes[offset] & 0xff) | ((bytes[offset + 1] & 0xff) << 8);
    }
}
