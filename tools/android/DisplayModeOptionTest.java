package io.github.ms912x.desktop;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/** Checks Android's EDID filtering against EDID generated from the Linux catalog. */
public final class DisplayModeOptionTest {
    public static void main(String[] args) throws Exception {
        byte[] edid = Files.readAllBytes(Path.of(args[0]));
        List<DisplayModeOption> modes = new ArrayList<>();
        for (String line : Files.readAllLines(Path.of(args[1]))) {
            String[] fields = line.split(",");
            int[] v = new int[12];
            for (int i = 0; i < v.length; ++i) v[i] = Integer.parseInt(fields[i]);
            modes.add(new DisplayModeOption(v[0], v[1], v[2], v[3],
                    v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11]));
        }
        require(!modes.isEmpty(), "No timing records");
        byte[] wrongSync = edid.clone();
        byte[] interlaced = edid.clone();
        for (int block = 0; block < edid.length / 128; ++block) {
            int start = block == 0 ? 54 : 4;
            int end = block == 0 ? 126 : 127;
            for (int offset = start; offset + 18 <= end; offset += 18) {
                int dtd = block * 128 + offset;
                if (edid[dtd] == 0 && edid[dtd + 1] == 0) continue;
                wrongSync[dtd + 8] ^= 1;
                interlaced[dtd + 17] |= (byte) 0x80;
            }
        }
        for (DisplayModeOption mode : modes) {
            require(mode.advertisedBy(edid), "Missing catalog timing: " + mode);
            require(!mode.advertisedBy(wrongSync), "Accepted wrong sync: " + mode);
            require(!mode.advertisedBy(interlaced), "Accepted interlaced timing: " + mode);
            require(!mode.advertisedBy(new byte[127]), "Accepted truncated EDID");
        }
        byte[] cta = new byte[256];
        cta[128] = 2;
        cta[129] = 3;
        cta[130] = 6;
        cta[132] = 0x41;
        cta[133] = 4; // CTA 720p60; adapter protocol ID is 0x4f.
        for (DisplayModeOption mode : modes) {
            require(mode.advertisedBy(cta) == (mode.id == 0x4f),
                    "Confused CTA code with adapter timing: " + mode);
        }
        testFormats();
        System.out.println("Android EDID filtering passed for " + modes.size()
                + " modes, established/standard formats, all supported CTA codes, and malformed blocks");
    }

    private static DisplayModeOption timing(int width, int height, int hz) {
        return new DisplayModeOption(0, width, height, hz, 1, 1, 1, 1, 1, 1, 1, 0);
    }

    private static void testFormats() {
        int[][] established = {{0,0,720,400,70}, {0,1,720,400,88}, {0,2,640,480,60},
            {0,3,640,480,67}, {0,4,640,480,72}, {0,5,640,480,75}, {0,6,800,600,56},
            {0,7,800,600,60}, {1,0,800,600,72}, {1,1,800,600,75}, {1,2,832,624,75},
            {1,3,1024,768,87}, {1,4,1024,768,60}, {1,5,1024,768,70}, {1,6,1024,768,75},
            {1,7,1280,1024,75}, {2,7,1152,870,75}};
        for (int[] entry : established) {
            byte[] edid = new byte[128]; edid[35 + entry[0]] = (byte)(1 << (7-entry[1]));
            require(timing(entry[2], entry[3], entry[4]).advertisedBy(edid), "Established bit");
            require(!timing(entry[2], entry[3], entry[4] + 1).advertisedBy(edid), "Established refresh mismatch");
        }
        int[] heights = {800,960,1024,720};
        for (int aspect = 0; aspect < 4; ++aspect) {
            byte[] edid = new byte[128];
            for (int i = 38; i < 54; ++i) edid[i] = 1;
            edid[38] = (byte)129; edid[39] = (byte)((aspect << 6) | 15);
            require(timing(1280, heights[aspect], 75).advertisedBy(edid), "Standard aspect");
            require(!timing(1280, heights[aspect], 60).advertisedBy(edid), "Standard refresh mismatch");
        }
        int[][] ctas = {{1,640,480,60}, {2,720,480,60}, {3,720,480,60}, {4,1280,720,60},
            {16,1920,1080,60}, {17,720,576,50}, {18,720,576,50}, {19,1280,720,50},
            {31,1920,1080,50}, {34,1920,1080,30}};
        for (int[] vic : ctas) {
            byte[] edid = new byte[256]; edid[128] = 2; edid[130] = 6;
            edid[132] = 0x41; edid[133] = (byte)(vic[0] | 0x80);
            require(timing(vic[1], vic[2], vic[3]).advertisedBy(edid), "CTA native flag/code");
            edid[130] = 0;
            require(timing(vic[1], vic[2], vic[3]).advertisedBy(edid), "CTA no detailed section");
            edid[130] = 5;
            require(!timing(vic[1], vic[2], vic[3]).advertisedBy(edid), "Truncated CTA data block");
            edid[130] = 3;
            require(!timing(vic[1], vic[2], vic[3]).advertisedBy(edid), "Invalid CTA offset");
            edid[130] = (byte)128;
            require(!timing(vic[1], vic[2], vic[3]).advertisedBy(edid), "Oversized CTA offset");
            edid[130] = 6; edid[133] = 127;
            require(!timing(vic[1], vic[2], vic[3]).advertisedBy(edid), "Unknown CTA code");
            edid[132] = 0x21;
            require(!timing(vic[1], vic[2], vic[3]).advertisedBy(edid), "Audio block is not a mode");
        }
        require(!timing(640,480,60).advertisedBy(null), "Null EDID");
        require(!timing(640,480,60).advertisedBy(new byte[129]), "Incomplete extension");
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
