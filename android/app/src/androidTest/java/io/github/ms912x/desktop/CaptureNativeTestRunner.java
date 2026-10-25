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
    private Bundle arguments;
    @Override
    public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        this.arguments = arguments == null ? new Bundle() : arguments;
        start();
    }

    @Override
    public void onStart() {
        if (arguments.getString("createdScreen", "false").equals("true") && !BuildConfig.MIRROR_ONLY) {
            runCase("Helper-owned independent screen", () -> Class.forName(
                    "io.github.ms912x.desktop.CreatedScreenFixtureTests").getMethod(
                            "run", Instrumentation.class, Bundle.class).invoke(null, this, arguments));
            finishReport();return;
        }
        if (arguments.getString("embeddedAdb", "false").equals("true") && !BuildConfig.MIRROR_ONLY) {
            runCase("Embedded Wireless ADB bootstrap", () -> Class.forName(
                    "io.github.ms912x.desktop.EmbeddedAdbFixtureTests").getMethod(
                            "run", Instrumentation.class, Bundle.class).invoke(null, this, arguments));
            finishReport();return;
        }
        if (arguments.getString("sessionAcceptance", "false").equals("true") && !BuildConfig.MIRROR_ONLY) {
            runCase("Session hardware acceptance", () -> Class.forName(
                    "io.github.ms912x.desktop.SessionAcceptanceFixtureTests").getMethod(
                            "run", Instrumentation.class, Bundle.class).invoke(null, this, arguments));
            finishReport();return;
        }
        if (arguments.getString("helperLifecycle", "false").equals("true") && !BuildConfig.MIRROR_ONLY) {
            runCase("Helper lifecycle acceptance", () -> Class.forName(
                    "io.github.ms912x.desktop.HelperLifecycleFixtureTests").getMethod(
                            "run", Instrumentation.class, Bundle.class).invoke(null, this, arguments));
            finishReport();
            return;
        }
        if (arguments.getString("helperGeometry", "false").equals("true") && !BuildConfig.MIRROR_ONLY) {
            runCase("Helper geometry pixel acceptance", () -> Class.forName(
                    "io.github.ms912x.desktop.HelperGeometryFixtureTests").getMethod(
                            "run", Instrumentation.class, Bundle.class).invoke(null, this, arguments));
            finishReport();
            return;
        }
        runCase("native submission", CaptureNativeTestRunner::testSubmission);
        runCase("native concurrent close", CaptureNativeTestRunner::testConcurrentClose);
        runCase("JNI boundaries and all modes", NativeBoundaryFixtureTests::run);
        runCase("Display selection persistence and reset", () -> SelectionFixtureTests.run(this));
        runCase("Touchpad tap and held-drag gestures", () -> TouchpadFixtureTests.run(this));
        if (BuildConfig.MIRROR_ONLY) {
            runCase("USB bulk transfer fixtures", UsbFixtureTests::bulk);
            runCase("USB permission and output lifecycle", () -> OutputFixtureTests.run(this));
            runCase("Capture and ImageReader lifecycle", CaptureFixtureTests::mirror);
            runCase("Mirror-only activity and source inventory", () -> ActivityFixtureTests.mirror(this));
            finishReport();
            return;
        }
        runCase("USB bulk transfer fixtures", UsbFixtureTests::bulk);
        runCase("Helper bootstrap and Binder contracts", () -> Class.forName(
                "io.github.ms912x.desktop.HelperFixtureTests").getMethod("run", Instrumentation.class).invoke(null, this));
        runCase("Embedded ADB runtime and JNI", () -> Class.forName(
                "io.github.ms912x.desktop.EmbeddedBridgeFixtureTests").getMethod("run", Instrumentation.class).invoke(null, this));
        runCase("Helper client cancellation and cleanup", () -> Class.forName(
                "io.github.ms912x.desktop.HelperClientFixtureTests").getMethod("run", Instrumentation.class).invoke(null, this));
        runCase("USB descriptor fixtures", UsbFixtureTests::descriptors);
        runCase("USB setup and output for all modes", UsbFixtureTests::protocol);
        runCase("USB control and EDID failures", UsbFixtureTests::failures);
        runCase("NSD callbacks and lifecycle", DiscoveryFixtureTests::run);
        runCase("Android Keystore and storage", () -> CredentialFixtureTests.storage(getTargetContext()));
        runCase("Java Rust JNI validation", CredentialFixtureTests::bridge);
        runCase("Capture, presentation and API controls", () -> CaptureFixtureTests.run(this));
        runCase("USB permission and output lifecycle", () -> OutputFixtureTests.run(this));
        runCase("Activity selectors and end-to-end fixture output", () -> ActivityFixtureTests.run(this));
        runCase("Session ownership and stop barriers", () -> SessionFixtureTests.run(this));
        runCase("Projection service invalid-consent lifecycle", () -> ActivityFixtureTests.service(this));
        finishReport();
    }

    private void finishReport() {
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
        testPacing();
        try (NativeCapture capture = new NativeCapture(4, 2, 1)) {
            ByteBuffer source = rgba(4, 2);
            require(capture.packetCapacity() == 48, "JNI packet capacity");
            ByteBuffer packet = ByteBuffer.allocateDirect(capture.packetCapacity());
            long[] token = new long[1];
            require(offer(capture, source, 4, 2) == 0, "JNI offer");
            String backend = capture.conversionBackend();
            require(Build.SUPPORTED_ABIS[0].equals("arm64-v8a") ? backend.equals("neon")
                    : backend.equals("sse2") || backend.equals("ssse3") || backend.equals("avx2"),
                    "Backend selection: " + backend);
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
            require(capture.encodeLatest(packet, token) == 24, "Linux damage block clipped to the four-pixel row");
            capture.invalidateBaseline();
            require(capture.completeBatch(token[0], true) == 0, "Complete after invalidation");
            require(offer(capture, source, 4, 2) == 0, "Offer after invalidation");
            require(capture.encodeLatest(packet, token) == 32, "Invalidation survives old acknowledgement");
            require(capture.completeBatch(token[0], true) == 0, "Complete full repair");
        }
    }

    private static void testPacing() {
        try (NativeCapture capture = new NativeCapture(64, 4, 1)) {
            ByteBuffer source = rgba(64, 4), packet = ByteBuffer.allocateDirect(capture.packetCapacity());
            long[] token = new long[2];
            long now = 1_000_000_000L;
            require(offer(capture, source, 64, 4) == 0, "Paced offer");
            for (int i = 0; i < 2; ++i) {
                require(capture.encodePaced(packet, token, now, 60) == 528 && token[1] == 0,
                        "Both initial full batches bypass coalescing");
                require(capture.completeBatch(token[0], true) == 0, "Paced full completion");
            }
            source.put(8, (byte)255);
            require(offer(capture, source, 64, 4) == 0, "Paced partial offer");
            require(capture.encodePaced(packet, token, now, 60) == 0 && token[1] == 8_333_333L,
                    "Linux half-frame coalescing deadline");
            long deadline = now + token[1];
            source.put(80, (byte)255);
            require(offer(capture, source, 64, 4) == 0, "Newer pending snapshot");
            require(capture.encodePaced(packet, token, now + 1, 60) == 0 && now + 1 + token[1] == deadline,
                    "New publications never renew the deadline");
            require(capture.encodePaced(packet, token, deadline, 60) == 80 && token[0] == 3,
                    "Newest snapshot keeps all changes without advancing unsent baseline");
            require(capture.completeBatch(token[0], true) == 0, "Paced damage completion");
            require(capture.encodePaced(packet, token, deadline, 60) == 80 && token[1] == 0,
                    "Final replay bypasses coalescing");
            require(capture.completeBatch(token[0], true) == 0, "Paced replay completion");
            require(capture.encodePaced(packet, new long[1], now, 60) < 0
                    && capture.encodePaced(packet, token, now, 0) < 0, "Pacing JNI bounds");
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
