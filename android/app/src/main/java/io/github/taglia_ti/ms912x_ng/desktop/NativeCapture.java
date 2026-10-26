package io.github.taglia_ti.ms912x_ng.desktop;

import java.nio.ByteBuffer;

final class NativeCapture implements AutoCloseable {
    static {
        System.loadLibrary("ms912x_capture");
    }

    private volatile long handle;

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

    int encodePaced(ByteBuffer destination, long[] sequenceAndWait, long nowNs, int refresh) {
        if (handle == 0 || destination == null || !destination.isDirect()) return -1;
        return nativeEncodePaced(handle, destination, sequenceAndWait, nowNs, refresh);
    }

    void recordUsb(int bytes, long durationNs) {
        if (handle != 0) nativeRecordUsb(handle, bytes, durationNs);
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

    void configure(ImageOptions options) {
        int result=nativeSetOptions(handle,options.profile,options.stride,options.tolerance,options.toleranceMax,options.scanEnabled);
        if(result!=0)throw new IllegalStateException("Cannot configure image matching: "+result);
    }
    static int[] profileDefaults(int profile){return nativeProfileDefaults(profile);}

    @Override
    public void close() {
        long oldHandle = handle;
        handle = 0;
        if (oldHandle != 0) {
            nativeDestroy(oldHandle);
        }
    }

    private static native long nativeCreate(int width, int height, long generation);
    private static native int nativeSetOptions(long handle,int profile,int stride,int tolerance,int toleranceMax,boolean scanEnabled);
    private static native int[] nativeProfileDefaults(int profile);
    private static native int nativeReconfigure(long handle, int width, int height,
                                                long generation);
    private static native int nativeOfferRgba(long handle, ByteBuffer buffer,
            int position, int capacity, int rowStride, int pixelStride,
            int cropX, int cropY, int width, int height, long generation);
    private static native int nativeEncodePaced(long handle, ByteBuffer destination,
            long[] sequenceAndWait, long nowNs, int refresh);
    private static native void nativeRecordUsb(long handle, int bytes, long durationNs);
    private static native int nativeEncodeLatest(long handle, ByteBuffer destination, long[] sequence);
    private static native int nativeCompleteBatch(long handle, long sequence, boolean transferred);
    private static native String nativeConversionBackend(long handle);
    private static native int nativePacketCapacity(long handle);
    private static native void nativeInvalidateBaseline(long handle);
    private static native void nativeSetShowDraws(long handle, boolean enabled);
    private static native void nativeDestroy(long handle);
}
