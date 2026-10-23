package io.github.ms912x.desktop;

import android.hardware.usb.UsbEndpoint;
import android.os.SystemClock;

import java.io.IOException;
import java.io.InterruptedIOException;
import java.util.function.LongSupplier;
import java.nio.ByteBuffer;

/** Writes one immutable mode-3 logical batch over the adapter's bulk endpoint. */
final class UsbBulkWriter {
    private static final int MAX_CHUNK_BYTES = 16 * 1024;
    private static final int CHUNK_TIMEOUT_MS = 250;
    private static final long BATCH_TIMEOUT_MS = 10_000;

    interface Transfer {
        int write(byte[] data, int offset, int length, int timeout);
    }

    private final Transfer transfer;
    private final LongSupplier clock;

    UsbBulkWriter(UsbHost.Connection connection, UsbEndpoint endpoint) {
        if (connection == null || endpoint == null) {
            throw new IllegalArgumentException("USB connection and endpoint are required");
        }
        transfer = (data, offset, length, timeout) ->
                connection.bulkTransfer(endpoint, data, offset, length, timeout);
        clock = SystemClock::elapsedRealtime;
    }

    UsbBulkWriter(Transfer transfer, LongSupplier clock) {
        if (transfer == null || clock == null) throw new IllegalArgumentException("Transfer and clock required");
        this.transfer = transfer;
        this.clock = clock;
    }

    int writePacket(ByteBuffer packet) throws IOException {
        if (packet == null || !packet.hasRemaining() || !packet.isDirect()) {
            throw new IllegalArgumentException("A non-empty direct packet is required");
        }

        ByteBuffer bytes = packet.asReadOnlyBuffer();
        byte[] chunk = new byte[Math.min(MAX_CHUNK_BYTES, bytes.remaining())];
        long deadline = clock.getAsLong() + BATCH_TIMEOUT_MS;
        int sent = 0;
        while (bytes.hasRemaining()) {
            int requested = Math.min(chunk.length, bytes.remaining());
            bytes.get(chunk, 0, requested);
            int offset = 0;
            while (offset < requested) {
                if (Thread.currentThread().isInterrupted()) {
                    throw new InterruptedIOException("USB mode-3 batch cancelled after " + sent + " bytes");
                }
                long remaining = deadline - clock.getAsLong();
                if (remaining <= 0) {
                    throw new IOException("USB mode-3 batch exceeded its deadline after " + sent + " bytes");
                }
                int remainingMs = (int) Math.min(CHUNK_TIMEOUT_MS, remaining);
                int written = transfer.write(chunk, offset, requested - offset, remainingMs);
                if (written > requested - offset) {
                    throw new IOException("USB bulk transfer reported more bytes than requested");
                }
                if (written < 0) {
                    throw new IOException("USB bulk transfer failed after " + sent
                            + " bytes");
                }
                if (written == 0) {
                    throw new IOException("USB bulk transfer made no progress after "
                            + sent + " bytes");
                }
                offset += written;
                sent += written;
            }
        }
        return sent;
    }
}
