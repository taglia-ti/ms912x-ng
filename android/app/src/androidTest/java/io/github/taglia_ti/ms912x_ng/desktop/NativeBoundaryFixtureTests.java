package io.github.taglia_ti.ms912x_ng.desktop;

import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.util.List;
import static io.github.taglia_ti.ms912x_ng.desktop.AndroidFixtures.*;

final class NativeBoundaryFixtureTests {
    static Object nativeCall(Class<?> owner, String name, Object... args) throws Exception {
        for (Method method : owner.getDeclaredMethods()) {
            if (method.getName().equals(name)) {
                method.setAccessible(true);
                return method.invoke(null, args);
            }
        }
        throw new AssertionError("Missing native method " + name);
    }

    static int offer(long handle, ByteBuffer bytes, int position, int capacity, int row,
                     int pixel, int x, int y, int width, int height, long generation) throws Exception {
        return (Integer)nativeCall(NativeCapture.class, "nativeOfferRgba", handle,
                bytes, position, capacity, row, pixel, x, y, width, height, generation);
    }

    static void run() throws Exception {
        expect(IllegalArgumentException.class, () -> new NativeCapture(3, 2, 1));
        expect(IllegalArgumentException.class, () -> new NativeCapture(0, 2, 1));
        expect(IllegalArgumentException.class, () -> new NativeCapture(1922, 2, 1));
        expect(IllegalArgumentException.class, () -> new NativeCapture(2, 1202, 1));
        expect(IllegalArgumentException.class, () -> new NativeCapture(2, 2, 0));
        long handle = (Long)nativeCall(NativeCapture.class, "nativeCreate", 4, 2, 1L);
        require(handle > 0, "Native handle");
        ByteBuffer pixels = ByteBuffer.allocateDirect(32), packet = ByteBuffer.allocateDirect(48);
        try {
            Object[][] invalid = {
                {null, 0, 32, 16, 4, 0, 0, 4, 2, 1L},
                {ByteBuffer.allocate(32), 0, 32, 16, 4, 0, 0, 4, 2, 1L},
                {pixels, -1, 32, 16, 4, 0, 0, 4, 2, 1L},
                {pixels, 1, 0, 16, 4, 0, 0, 4, 2, 1L},
                {pixels, 0, 33, 16, 4, 0, 0, 4, 2, 1L},
                {pixels, 0, 32, 0, 4, 0, 0, 4, 2, 1L},
                {pixels, 0, 32, 16, 0, 0, 0, 4, 2, 1L},
                {pixels, 0, 32, 16, 4, -1, 0, 4, 2, 1L},
                {pixels, 0, 32, 16, 4, 0, -1, 4, 2, 1L},
                {pixels, 0, 32, 16, 4, 0, 0, 0, 2, 1L},
                {pixels, 0, 32, 16, 4, 0, 0, 4, 0, 1L},
                {pixels, 0, 32, 16, 4, 0, 0, 4, 2, 0L},
                {pixels, 0, 31, 16, 4, 0, 0, 4, 2, 1L},
                {pixels, 0, 32, 16, 4, 1, 0, 4, 2, 1L},
                {pixels, 0, 32, 16, 4, 0, 0, 3, 2, 1L}};
            for (Object[] args : invalid) {
                Object[] all = new Object[args.length + 1]; all[0] = handle;
                System.arraycopy(args, 0, all, 1, args.length);
                require((Integer)nativeCall(NativeCapture.class, "nativeOfferRgba", all) < 0, "JNI rejects malformed view");
            }
            require(offer(handle, pixels, 0, 32, 16, 4, 0, 0, 4, 2, 2) == -2, "Stale generation");
            require(offer(handle, pixels, 0, 32, 16, 4, 0, 0, 4, 2, 1) == 0, "Valid view after failures");
            require((Integer)nativeCall(NativeCapture.class, "nativeEncodeLatest", handle, null, new long[1]) < 0, "Null packet");
            require((Integer)nativeCall(NativeCapture.class, "nativeEncodeLatest", handle, packet, null) < 0, "Null token");
            require((Integer)nativeCall(NativeCapture.class, "nativeEncodeLatest", handle, packet, new long[0]) < 0, "Empty token");
            require((Integer)nativeCall(NativeCapture.class, "nativeEncodeLatest", handle, ByteBuffer.allocate(48), new long[1]) < 0, "Heap packet");
            long[] token = new long[1];
            require((Integer)nativeCall(NativeCapture.class, "nativeEncodeLatest", handle, packet, token) == 32, "Valid JNI encode");
            require((Integer)nativeCall(NativeCapture.class, "nativeReconfigure", handle, 4, 2, 2L) == -2, "Pinned batch cannot reconfigure");
            require((Integer)nativeCall(NativeCapture.class, "nativeCompleteBatch", handle, token[0], true) == 0, "Complete token");
            require((Integer)nativeCall(NativeCapture.class, "nativeReconfigure", handle, 4, 2, 1L) < 0, "Generation must advance");
            require((Integer)nativeCall(NativeCapture.class, "nativeReconfigure", handle, 4, 2, 2L) == 0, "Successful reconfigure");
        } finally { nativeCall(NativeCapture.class, "nativeDestroy", handle); }
        require(offer(handle, pixels, 0, 32, 16, 4, 0, 0, 4, 2, 1) == -4, "Destroyed handle rejected");
        nativeCall(NativeCapture.class, "nativeDestroy", handle);
        require(nativeCall(NativeCapture.class, "nativeConversionBackend", handle).equals("closed"), "Closed backend");
        require((Integer)nativeCall(NativeCapture.class, "nativePacketCapacity", handle) == 0, "Closed capacity");
        NativeCapture closed = new NativeCapture(4, 2, 1); closed.close(); closed.close();
        require(closed.packetCapacity() == 0 && closed.conversionBackend().equals("closed"), "Closed Java wrapper");
        require(closed.offerRgba(pixels, 16, 4, 0, 0, 4, 2, 1) < 0, "Closed offer wrapper");
        require(closed.encodeLatest(packet, new long[1]) < 0 && closed.completeBatch(1, true) < 0, "Closed submission wrapper");
        closed.invalidateBaseline(); require(closed.reconfigure(4, 2, 2) < 0, "Closed configure wrapper");
        List<DisplayModeOption> modes = NativeAdapter.modeCatalog();
        ByteBuffer allSource = ByteBuffer.allocateDirect(1920 * 1200 * 4);
        ByteBuffer allPacket = ByteBuffer.allocateDirect(1200 * (1920 * 2 + 16));
        for (DisplayModeOption mode : modes) {
            try (NativeCapture capture = new NativeCapture(mode.width, mode.height, 1)) {
                require(capture.packetCapacity() == mode.height * (mode.width * 2 + 16), "Catalog capacity " + mode);
                ByteBuffer source = allSource;
                source.clear(); source.limit(mode.width * mode.height * 4);
                require(capture.offerRgba(source, mode.width * 4, 4, 0, 0, mode.width, mode.height, 1) == 0,
                        "All catalog geometries accepted");
                long[] token = new long[1];
                require(capture.encodeLatest(allPacket, token)
                        == mode.width * mode.height * 2 + 16, "All modes full-frame packet");
                require(capture.completeBatch(token[0], true) == 0, "All modes complete");
            }
        }
        require(NativeTestPattern.create640x480Packet().remaining() == 614416, "Test pattern size");
        require((Integer)nativeCall(NativeTestPattern.class, "nativeEncode640x480", (Object)null) < 0, "Null pattern destination");
        require((Integer)nativeCall(NativeTestPattern.class, "nativeEncode640x480", ByteBuffer.allocateDirect(4)) < 0, "Short pattern destination");
        require((Integer)nativeCall(NativeTestPattern.class, "nativeEncode640x480", ByteBuffer.allocate(614416)) < 0, "Heap pattern destination");
    }
}
