package io.github.taglia_ti.ms912x_ng.desktop;

import java.io.IOException;
import java.nio.ByteBuffer;

/** One immutable logical batch; closing an uncommitted batch aborts its baseline update. */
final class FrameSubmission implements AutoCloseable {
    interface Completion {
        int complete(long sequence, boolean transferred);
    }

    private final ByteBuffer packet;
    private final long sequence;
    private final Completion completion;
    private boolean finished;

    FrameSubmission(ByteBuffer packet, long sequence, Completion completion) {
        this.packet = packet.asReadOnlyBuffer();
        this.sequence = sequence;
        this.completion = completion;
    }

    ByteBuffer packet() {
        if (finished) throw new IllegalStateException("Submission is already completed");
        return packet;
    }

    void commit() throws IOException {
        if (finished) throw new IllegalStateException("Submission is already completed");
        finished = true;
        int result = completion.complete(sequence, true);
        if (result < 0) throw new IOException("Frame completion failed: " + result);
    }

    @Override
    public void close() {
        if (!finished) {
            finished = true;
            completion.complete(sequence, false);
        }
    }
}
