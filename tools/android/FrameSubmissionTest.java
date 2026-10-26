package io.github.taglia_ti.ms912x_ng.desktop;

import java.io.IOException;
import java.nio.ByteBuffer;

public final class FrameSubmissionTest {
    public static void main(String[] args) throws Exception {
        int[] calls = {0};
        boolean[] transferred = {false};
        FrameSubmission.Completion completion = (token, sent) -> {
            require(token == 19, "Wrong completion token");
            ++calls[0];
            transferred[0] = sent;
            return 0;
        };
        try (FrameSubmission batch = new FrameSubmission(ByteBuffer.allocateDirect(32), 19, completion)) {
            require(batch.packet().isReadOnly() && batch.packet().isDirect(), "Packet ownership");
            require(calls[0] == 0, "Committed before transport completion");
            batch.commit();
        }
        require(calls[0] == 1 && transferred[0], "Success must commit exactly once");
        calls[0] = 0;
        try (FrameSubmission batch = new FrameSubmission(ByteBuffer.allocateDirect(32), 19, completion)) {
            batch.packet();
            throw new IOException("Injected USB failure");
        } catch (IOException expected) {
            require(calls[0] == 1 && !transferred[0], "Failure must abort exactly once");
        }
        calls[0] = 0;
        FrameSubmission abandoned = new FrameSubmission(ByteBuffer.allocateDirect(32), 19, completion);
        abandoned.close();
        abandoned.close();
        require(calls[0] == 1 && !transferred[0], "Close must be idempotent");
        try (FrameSubmission stale = new FrameSubmission(ByteBuffer.allocateDirect(32), 19,
                (token, sent) -> -116)) {
            stale.commit();
            throw new AssertionError("Stale completion was accepted");
        } catch (IOException expected) {
            // Completion errors must reach the stream owner.
        }
        System.out.println("Java frame-submission ownership tests passed");
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
