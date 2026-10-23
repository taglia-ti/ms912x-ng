package io.github.ms912x.desktop;

import java.nio.ByteBuffer;

final class NativeTestPattern {
    private static final int WIDTH = 640;
    private static final int HEIGHT = 480;
    private static final int PACKET_BYTES = WIDTH * HEIGHT * 2 + 16;

    static {
        System.loadLibrary("ms912x_capture");
    }

    private NativeTestPattern() { }

    static ByteBuffer create640x480Packet() {
        ByteBuffer packet = ByteBuffer.allocateDirect(PACKET_BYTES);
        int length = nativeEncode640x480(packet);
        if (length != PACKET_BYTES) {
            throw new IllegalStateException("Native test-pattern encoding failed: "
                    + length);
        }
        packet.position(0);
        packet.limit(length);
        return packet;
    }

    private static native int nativeEncode640x480(ByteBuffer destination);
}
