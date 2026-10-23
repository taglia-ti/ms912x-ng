package io.github.ms912x.desktop;

/** A timing from the shared Linux/Android adapter catalog. */
final class DisplayModeOption {
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
        if (edid == null || edid.length < 128 || edid.length % 128 != 0) {
            return false;
        }
        if (supportsEstablished(edid) || supportsStandard(edid)
                || supportsDetailed(edid, 0, 54, 126)) {
            return true;
        }
        for (int block = 1; block < edid.length / 128; ++block) {
            int base = block * 128;
            if ((edid[base] & 0xff) == 0x02 && supportsCta(edid, base)) {
                return true;
            }
        }
        return false;
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
            if (pixelClock * 10 == clockKHz && hActive == width
                    && vActive == height && hActive + hBlank == htotal
                    && vActive + vBlank == vtotal && separateSync
                    && (flags & 0x80) == 0 && hSync == hsyncWidth
                    && vSync == vsyncWidth && hBlank - hFront == hoffset
                    && vBlank - vFront == voffset
                    && ((flags >>> 1) & 1) == ((polarity >>> 1) & 1)
                    && ((flags >>> 2) & 1) == ((polarity >>> 2) & 1)) return true;
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
                    && entry[2] == width && entry[3] == height
                    && entry[4] == refreshHz) return true;
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
            if (h == width && v == height && refresh == refreshHz) return true;
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
            case 1: return width == 640 && height == 480 && refreshHz == 60;
            case 2: case 3: return width == 720 && height == 480 && refreshHz == 60;
            case 4: return width == 1280 && height == 720 && refreshHz == 60;
            case 16: return width == 1920 && height == 1080 && refreshHz == 60;
            case 17: case 18: return width == 720 && height == 576 && refreshHz == 50;
            case 19: return width == 1280 && height == 720 && refreshHz == 50;
            case 31: return width == 1920 && height == 1080 && refreshHz == 50;
            case 34: return width == 1920 && height == 1080 && refreshHz == 30;
            default: return false;
        }
    }

    private static int u16(byte[] bytes, int offset) {
        return (bytes[offset] & 0xff) | ((bytes[offset + 1] & 0xff) << 8);
    }
}
