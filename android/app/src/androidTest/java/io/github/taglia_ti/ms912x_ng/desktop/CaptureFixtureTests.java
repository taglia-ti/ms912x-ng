package io.github.taglia_ti.ms912x_ng.desktop;

import android.app.Instrumentation;
import android.graphics.Canvas;
import android.graphics.Color;
import android.hardware.display.*;
import android.os.Build;
import android.view.*;
import java.nio.ByteBuffer;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import static io.github.taglia_ti.ms912x_ng.desktop.AndroidFixtures.*;

final class CaptureFixtureTests {
    static void mirror() throws Exception {
        AtomicInteger accepted = new AtomicInteger();
        try (CaptureController capture = new CaptureController(4, 2, 1,
                result -> { if (result == 0) accepted.incrementAndGet(); })) {
            Canvas canvas = capture.surface().lockCanvas(null);
            canvas.drawColor(Color.RED);
            capture.surface().unlockCanvasAndPost(canvas);
            waitFor(() -> accepted.get() > 0, "Legacy ImageReader to native capture");
            FrameSubmission frame = capture.prepareLatestFrame(ByteBuffer.allocateDirect(capture.packetCapacity()));
            require(frame != null, "Legacy captured frame encodes");
            frame.commit();
            frame.close();
        }
        testIdleFrameWait();
        testSettingsWake();
        testConsecutiveFrames();
    }

    static void run(Instrumentation test) throws Exception {
        DisplayManager manager = test.getTargetContext().getSystemService(DisplayManager.class);
        ScreenSource phone = ScreenSource.fromDisplay(manager.getDisplay(Display.DEFAULT_DISPLAY));
        require(phone.canStart() && phone.toString().contains("Phone screen") && phone.refreshHz > 0, "Default screen inventory");
        require(new ScreenSource(ScreenSource.TEST_SCREEN, "new", 60, 0).canStart(), "New source");
        require(new ScreenSource(ScreenSource.APP_PICKER, "app", 60, 0).canStart(), "App source");
        require(!new ScreenSource(99, "secondary", 60, 0).canStart(), "Secondary source requires helper");
        expect(IllegalArgumentException.class, () -> new CaptureController(640, 480, 0, null));
        AtomicInteger accepted = new AtomicInteger();
        CaptureController capture = new CaptureController(640, 480, 1, s -> { if (s == 0) accepted.incrementAndGet(); });
        VirtualDisplay display = null;
        TestScreenPresentation[] presentation = {null};
        try {
            display = VirtualScreenOptions.createOwnScreen(manager, 640, 480, 160, capture.surface(), 50);
            require(display != null, "Public own-screen creation");
            ScreenSource secondary = ScreenSource.fromDisplay(display.getDisplay());
            require(!secondary.canStart() && !secondary.label.contains("Phone screen"), "Secondary display inventory");
            VirtualDisplay active = display;
            expect(IllegalArgumentException.class, () -> VirtualScreenOptions.applyRotation(active, -1));
            expect(IllegalArgumentException.class, () -> VirtualScreenOptions.applyRotation(active, 4));
            if (Build.VERSION.SDK_INT >= 36) {
                for (int rotation = 0; rotation < 4; ++rotation) VirtualScreenOptions.applyRotation(display, rotation);
            } else {
                VirtualScreenOptions.applyRotation(display, 0);
                expect(IllegalArgumentException.class, () -> VirtualScreenOptions.applyRotation(active, 1));
            }
            test.runOnMainSync(() -> {
                presentation[0] = new TestScreenPresentation(test.getTargetContext(), active.getDisplay());
                presentation[0].show();
            });
            waitFor(() -> accepted.get() > 0, "ImageReader -> native accepted frame");
            require(capture.width() == 640 && capture.height() == 480 && capture.isOpen(), "Capture geometry/lifetime");
            require(capture.prepareLatestFrame(null) == null
                    && capture.prepareLatestFrame(ByteBuffer.allocate(4)) == null, "Capture destination validation");
            ByteBuffer packet = ByteBuffer.allocateDirect(capture.packetCapacity());
            FrameSubmission batch = capture.prepareLatestFrame(packet);
            require(batch != null && batch.packet().remaining() == 614416, "ImageReader -> full encoded packet");
            expect(java.io.IOException.class, () -> capture.prepareLatestFrame(packet));
            batch.close(); capture.invalidateBaseline();
            int previous = accepted.get();
            waitFor(() -> accepted.get() > previous, "Animation supplies next captured frame");
            try (FrameSubmission repair = capture.prepareLatestFrame(packet)) {
                require(repair != null && repair.packet().remaining() == 614416, "Aborted submission forces repair");
                repair.commit();
            }
        } finally {
            test.runOnMainSync(() -> { if (presentation[0] != null) presentation[0].dismiss(); });
            if (display != null) display.release();
            capture.close(); capture.close();
        }
        require(!capture.isOpen() && capture.packetCapacity() == 0
                && capture.prepareLatestFrame(ByteBuffer.allocateDirect(4)) == null, "Closed capture methods");
        capture.invalidateBaseline();
        // Close from the image callback: cleanup must be deferred until the borrowed Image is released.
        AtomicReference<CaptureController> self = new AtomicReference<>();
        CountDownLatch callbackClosed = new CountDownLatch(1);
        CaptureController fromCallback = new CaptureController(4, 2, 1, status -> {
            self.get().close(); callbackClosed.countDown();
        });
        self.set(fromCallback);
        Canvas canvas = fromCallback.surface().lockCanvas(null);
        canvas.drawColor(Color.RED); fromCallback.surface().unlockCanvasAndPost(canvas);
        require(callbackClosed.await(10, TimeUnit.SECONDS), "Image-thread close completed");
        fromCallback.close();
        require(!fromCallback.isOpen(), "Image-thread close state");
        testIdleFrameWait();
        testSettingsWake();
        testConsecutiveFrames();
    }

    private static void testSettingsWake() throws Exception {
        CaptureController capture = new CaptureController(4, 2, 1, null);
        CountDownLatch waiting = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread waiter = new Thread(() -> {
            long version = capture.changeVersion();
            waiting.countDown();
            try { capture.awaitFrame(version); }
            catch (Throwable problem) { failure.set(problem); }
        }, "capture-settings-wait-fixture");
        try {
            waiter.start();
            require(waiting.await(2, TimeUnit.SECONDS), "Settings waiter starts");
            capture.setImageOptions(new ImageOptions(2, 1, 0));
            waiter.join(2000);
            require(!waiter.isAlive(), "Live settings wake an idle USB worker without a new image");
            require(capture.frameVersion() == 0, "A setting change does not count as a captured frame");
            if (failure.get() != null) throw new AssertionError("Settings wake", failure.get());
        } finally {
            capture.close();
            waiter.interrupt();
            waiter.join(2000);
        }
    }

    private static void testIdleFrameWait() throws Exception {
        AtomicInteger accepted = new AtomicInteger();
        CaptureController capture = new CaptureController(4, 2, 1,
                status -> { if (status == 0) accepted.incrementAndGet(); });
        AtomicReference<Throwable> failure = new AtomicReference<>();
        CountDownLatch waiting = new CountDownLatch(1);
        Thread waiter = new Thread(() -> {
            long version = capture.changeVersion();
            waiting.countDown();
            try { capture.awaitFrame(version); }
            catch (Throwable problem) { failure.set(problem); }
        }, "capture-idle-wait-fixture");
        try {
            waiter.start();
            require(waiting.await(2, TimeUnit.SECONDS), "Idle waiter starts");
            Thread.sleep(80);
            require(waiter.isAlive(), "Idle capture waits for a frame instead of polling");
            Canvas canvas = capture.surface().lockCanvas(null);
            canvas.drawColor(Color.GREEN);
            capture.surface().unlockCanvasAndPost(canvas);
            require(acceptedWait(accepted), "A newly offered frame wakes the USB worker");
            waiter.join(2000);
            require(!waiter.isAlive(), "Frame signal releases the waiter");
            if (failure.get() != null) throw new AssertionError("Idle frame wait", failure.get());
        } finally {
            capture.close();
            waiter.interrupt();
            waiter.join(2000);
        }
    }

    private static boolean acceptedWait(AtomicInteger accepted) throws Exception {
        waitFor(() -> accepted.get() > 0, "ImageReader accepts frame during idle wait");
        return accepted.get() > 0;
    }

    private static void testConsecutiveFrames() throws Exception {
        AtomicInteger accepted = new AtomicInteger();
        AtomicReference<Throwable> failure = new AtomicReference<>();
        AtomicReference<CaptureController> owner = new AtomicReference<>();
        CountDownLatch completed = new CountDownLatch(3);
        CaptureController capture = new CaptureController(4, 2, 1, status -> {
            try {
                require(status == 0, "Consecutive source frame accepted");
                int count = accepted.incrementAndGet();
                if (count < 3) {
                    // Queue the next frame immediately; no animation or pacing timer.
                    Canvas frame = owner.get().surface().lockCanvas(null);
                    frame.drawColor(count == 1 ? Color.GREEN : Color.BLUE);
                    owner.get().surface().unlockCanvasAndPost(frame);
                }
            } catch (Throwable problem) {
                failure.set(problem);
                while (completed.getCount() > 0) completed.countDown();
            } finally {
                completed.countDown();
            }
        });
        owner.set(capture);
        try {
            Canvas first = capture.surface().lockCanvas(null);
            first.drawColor(Color.RED);
            capture.surface().unlockCanvasAndPost(first);
            require(completed.await(10, TimeUnit.SECONDS),
                    "Consecutive queued frames must reach native capture without timed drops");
            if (failure.get() != null) throw new AssertionError("Consecutive capture", failure.get());
            require(accepted.get() == 3, "All three consecutive frames were offered");
        } finally {
            capture.close();
        }
    }

    interface Condition { boolean ready(); }
    static void waitFor(Condition condition, String message) throws Exception {
        long deadline = android.os.SystemClock.elapsedRealtime() + 10_000;
        while (!condition.ready()) {
            require(android.os.SystemClock.elapsedRealtime() < deadline, message);
            Thread.sleep(20);
        }
    }
}
