package io.github.ms912x.desktop;

import android.app.Activity;
import android.app.Instrumentation;
import android.os.Build;
import android.os.Bundle;

import java.nio.ByteBuffer;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

/** Exercises the production JNI bridge without USB hardware or external test libraries. */
public final class CaptureNativeTestRunner extends Instrumentation {
    @Override
    public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        start();
    }

    @Override
    public void onStart() {
        runCase("native submission", CaptureNativeTestRunner::testSubmission);
        runCase("native concurrent close", CaptureNativeTestRunner::testConcurrentClose);
        runCase("JNI boundaries and all modes", NativeBoundaryFixtureTests::run);
        runCase("USB bulk transfer fixtures", UsbFixtureTests::bulk);
        runCase("USB descriptor fixtures", UsbFixtureTests::descriptors);
        runCase("USB setup and output for all modes", UsbFixtureTests::protocol);
        runCase("USB control and EDID failures", UsbFixtureTests::failures);
        runCase("NSD callbacks and lifecycle", DiscoveryFixtureTests::run);
        runCase("Android Keystore and storage", () -> CredentialFixtureTests.storage(getTargetContext()));
        runCase("Java Rust JNI validation", CredentialFixtureTests::bridge);
        runCase("Capture, presentation and API controls", () -> CaptureFixtureTests.run(this));
        runCase("USB permission and output lifecycle", () -> OutputFixtureTests.run(this));
        runCase("Activity selectors and end-to-end fixture output", () -> ActivityFixtureTests.run(this));
        runCase("Projection service invalid-consent lifecycle", () -> ActivityFixtureTests.service(this));
        Bundle result = new Bundle();
        result.putString("stream", report.toString() + "\n" + passed + " groups passed; " + failed + " failed\n");
        finish(failed == 0 ? Activity.RESULT_OK : Activity.RESULT_CANCELED, result);
    }

    private final StringBuilder report = new StringBuilder();
    private int passed, failed;

    private void runCase(String name, AndroidFixtures.Action action) {
        String message;
        try {
            action.run(); ++passed; message = "PASS: " + name;
        } catch (Throwable failure) {
            ++failed; message = "FAIL: " + name + "\n" + android.util.Log.getStackTraceString(failure);
        }
        report.append(message).append('\n');
        Bundle progress = new Bundle(); progress.putString("stream", message + "\n"); sendStatus(0, progress);
    }

    private static ByteBuffer rgba(int width, int height) {
        ByteBuffer source = ByteBuffer.allocateDirect(width * height * 4);
        for (int i = 0; i < width * height; ++i) {
            source.put((byte) 5).put((byte) 72).put((byte) 193).put((byte) 255);
        }
        source.clear();
        return source;
    }

    private static int offer(NativeCapture capture, ByteBuffer rgba, int width, int height) {
        return capture.offerRgba(rgba, width * 4, 4, 0, 0, width, height, 1);
    }

    private static void testSubmission() {
        try (NativeCapture capture = new NativeCapture(4, 2, 1)) {
            ByteBuffer source = rgba(4, 2);
            require(capture.packetCapacity() == 48, "JNI packet capacity");
            ByteBuffer packet = ByteBuffer.allocateDirect(capture.packetCapacity());
            long[] token = new long[1];
            require(offer(capture, source, 4, 2) == 0, "JNI offer");
            String expected = Build.SUPPORTED_ABIS[0].equals("arm64-v8a") ? "neon" : "sse2";
            require(capture.conversionBackend().equals(expected), "Backend selection");
            require(capture.encodeLatest(packet, token) == 32, "Initial full frame");
            long first = token[0];
            require(capture.encodeLatest(packet, token) == -16, "Outstanding batch must stay pinned");
            require(capture.completeBatch(first, false) == 0, "Abort");
            require(offer(capture, source, 4, 2) == 0, "Offer repair");
            require(capture.encodeLatest(packet, token) == 32, "Abort must force full repair");
            require(token[0] > first, "Monotonic submission sequence");
            require(capture.completeBatch(first, true) < 0, "Stale acknowledgement");
            require(capture.completeBatch(token[0], true) == 0, "Successful acknowledgement");
            require(capture.encodeLatest(packet, token) == 32, "Idle initial full-frame replay");
            require(capture.completeBatch(token[0], true) == 0, "Complete idle replay");
            require(offer(capture, source, 4, 2) == 0, "Offer unchanged");
            require(capture.encodeLatest(packet, token) == 0, "No unchanged-frame packet");
            source.put(8, (byte) 255);
            require(offer(capture, source, 4, 2) == 0, "Offer changed pair");
            require(capture.encodeLatest(packet, token) == 20, "Pair-aligned damage rectangle");
            capture.invalidateBaseline();
            require(capture.completeBatch(token[0], true) == 0, "Complete after invalidation");
            require(offer(capture, source, 4, 2) == 0, "Offer after invalidation");
            require(capture.encodeLatest(packet, token) == 32, "Invalidation survives old acknowledgement");
            require(capture.completeBatch(token[0], true) == 0, "Complete full repair");
        }
    }

    private static void testConcurrentClose() throws Exception {
        NativeCapture closing = new NativeCapture(640, 480, 1);
        NativeCapture other = new NativeCapture(640, 480, 1);
        ByteBuffer source = rgba(640, 480);
        CountDownLatch started = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread producer = new Thread(() -> {
            try {
                started.countDown();
                for (int i = 0; i < 500; ++i) {
                    int status = offer(closing, source, 640, 480);
                    require(status == 0 || status == 1 || status == -1 || status == -4,
                            "Unexpected concurrent-close status: " + status);
                }
            } catch (Throwable problem) {
                failure.set(problem);
            }
        });
        try {
            producer.start();
            started.await();
            for (int i = 0; i < 30; ++i) require(offer(other, source, 640, 480) == 0, "Independent handle");
            closing.close();
            producer.join(15_000);
            require(!producer.isAlive(), "Concurrent close deadlocked");
            if (failure.get() != null) throw new AssertionError("Concurrent producer", failure.get());
            require(offer(other, source, 640, 480) == 0, "Other handle survived close");
        } finally {
            closing.close();
            other.close();
            producer.join();
        }
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
