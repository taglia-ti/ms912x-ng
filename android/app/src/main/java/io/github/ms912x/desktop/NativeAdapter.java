package io.github.ms912x.desktop;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** Native helpers for adapter control reports, EDID, and shared mode data. */
final class NativeAdapter {
    static {
        System.loadLibrary("ms912x_capture");
    }

    private NativeAdapter() {}

    static List<DisplayModeOption> modeCatalog() {
        return modesFrom(nativeModeCatalog());
    }

    static List<DisplayModeOption> customModes(UsbAdapterSession session) throws IOException {
        return modesFrom(nativeCustomModes(session));
    }

    private static List<DisplayModeOption> modesFrom(int[] values) {
        List<DisplayModeOption> result = new ArrayList<>();
        if (values == null || values.length % 12 != 0) return result;
        for (int i = 0; i < values.length; i += 12) {
            result.add(new DisplayModeOption(values[i], values[i + 1],
                    values[i + 2], values[i + 3], values[i + 4], values[i + 5],
                    values[i + 6], values[i + 7], values[i + 8], values[i + 9],
                    values[i + 10], values[i + 11]));
        }
        return result;
    }

    static byte[] readEdidBlock(UsbAdapterSession session, int block) {
        return nativeReadEdidBlock(session, block);
    }

    static byte[] flashReadRequest(int address) { return nativeFlashReadRequest(address); }
    static byte[] modePlan(int vic, int width, int height) {
        return nativeModePlan(vic, width, height);
    }
    static byte[] controlWrite(int command, byte[] data) {
        return nativeControlWrite(command, data);
    }
    static byte[] registerRead(int address) { return nativeRegisterRead(address); }
    static boolean supportsSdram(int value) { return nativeSupportsSdram(value); }
    static boolean isSupportedDevice(int vendor, int product) {
        return nativeIsSupportedDevice(vendor, product);
    }
    static boolean repairEdidBaseHeader(byte[] block) {
        return nativeRepairEdidBaseHeader(block);
    }

    private static native byte[] nativeReadEdidBlock(UsbAdapterSession session, int block);
    private static native byte[] nativeFlashReadRequest(int address);
    private static native int[] nativeCustomModes(UsbAdapterSession session) throws IOException;
    private static native byte[] nativeModePlan(int vic, int width, int height);
    private static native byte[] nativeControlWrite(int command, byte[] data);
    private static native byte[] nativeRegisterRead(int address);
    private static native boolean nativeSupportsSdram(int value);
    private static native boolean nativeIsSupportedDevice(int vendor, int product);
    private static native boolean nativeRepairEdidBaseHeader(byte[] block);
    private static native int[] nativeModeCatalog();
}
