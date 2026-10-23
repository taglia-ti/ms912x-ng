package io.github.ms912x.desktop;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

final class NativeCapture implements AutoCloseable {
    static {
        System.loadLibrary("ms912x_capture");
    }

    private volatile long handle;

    static List<DisplayModeOption> modeCatalog() {
        int[] values = nativeModeCatalog();
        List<DisplayModeOption> result = new ArrayList<>();
        if (values == null || values.length % 12 != 0) {
            return result;
        }
        for (int i = 0; i < values.length; i += 12) {
            result.add(new DisplayModeOption(values[i], values[i + 1],
                    values[i + 2], values[i + 3], values[i + 4], values[i + 5],
                    values[i + 6], values[i + 7], values[i + 8], values[i + 9],
                    values[i + 10], values[i + 11]));
        }
        return result;
    }

    NativeCapture(int width, int height, long generation) {
        handle = nativeCreate(width, height, generation);
        if (handle == 0) {
            throw new IllegalArgumentException("Frame dimensions exceed the native limit");
        }
    }

    int reconfigure(int width, int height, long generation) {
        return handle == 0 ? -4 : nativeReconfigure(handle, width, height, generation);
    }

    int offerRgba(ByteBuffer buffer, int rowStride, int pixelStride,
                  int cropX, int cropY, int width, int height, long generation) {
        if (handle == 0 || buffer == null || !buffer.isDirect()) {
            return -1;
        }
        return nativeOfferRgba(handle, buffer, buffer.position(), buffer.limit(),
                rowStride, pixelStride, cropX, cropY, width, height, generation);
    }

    int encodeLatest(ByteBuffer destination, long[] sequence) {
        if (handle == 0 || destination == null || !destination.isDirect()) {
            return -1;
        }
        return nativeEncodeLatest(handle, destination, sequence);
    }

    int completeBatch(long sequence, boolean transferred) {
        return handle == 0 ? -4 : nativeCompleteBatch(handle, sequence, transferred);
    }

    String conversionBackend() {
        return handle == 0 ? "closed" : nativeConversionBackend(handle);
    }

    int packetCapacity() {
        return handle == 0 ? 0 : nativePacketCapacity(handle);
    }

    void invalidateBaseline() {
        if (handle != 0) {
            nativeInvalidateBaseline(handle);
        }
    }

    void setShowDraws(boolean enabled) {
        if (handle != 0) nativeSetShowDraws(handle, enabled);
    }

    @Override
    public void close() {
        long oldHandle = handle;
        handle = 0;
        if (oldHandle != 0) {
            nativeDestroy(oldHandle);
        }
    }

    static native byte[] modePlan(int vic, int width, int height);
    static native byte[] controlWrite(int command, byte[] data);
    static native byte[] registerRead(int address);
    static native boolean supportsSdram(int value);
    static native boolean isSupportedDevice(int vendor, int product);
    static native boolean repairEdidBaseHeader(byte[] block);
    private static native long nativeCreate(int width, int height, long generation);
    private static native int nativeReconfigure(long handle, int width, int height,
                                                long generation);
    private static native int nativeOfferRgba(long handle, ByteBuffer buffer,
            int position, int capacity, int rowStride, int pixelStride,
            int cropX, int cropY, int width, int height, long generation);
    private static native int nativeEncodeLatest(long handle, ByteBuffer destination, long[] sequence);
    private static native int nativeCompleteBatch(long handle, long sequence, boolean transferred);
    private static native String nativeConversionBackend(long handle);
    private static native int nativePacketCapacity(long handle);
    private static native void nativeInvalidateBaseline(long handle);
    private static native void nativeSetShowDraws(long handle, boolean enabled);
    private static native void nativeDestroy(long handle);
    private static native int[] nativeModeCatalog();
}
