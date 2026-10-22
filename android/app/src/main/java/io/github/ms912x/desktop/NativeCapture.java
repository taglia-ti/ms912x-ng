package io.github.ms912x.desktop;

import java.nio.ByteBuffer;

final class NativeCapture implements AutoCloseable {
    static {
        System.loadLibrary("ms912x_capture");
    }

    private long handle;

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

    @Override
    public void close() {
        long oldHandle = handle;
        handle = 0;
        if (oldHandle != 0) {
            nativeDestroy(oldHandle);
        }
    }

    private static native long nativeCreate(int width, int height, long generation);
    private static native int nativeReconfigure(long handle, int width, int height,
                                                long generation);
    private static native int nativeOfferRgba(long handle, ByteBuffer buffer,
            int position, int capacity, int rowStride, int pixelStride,
            int cropX, int cropY, int width, int height, long generation);
    private static native void nativeDestroy(long handle);
}
