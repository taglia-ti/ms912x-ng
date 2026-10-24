package io.github.ms912x.desktop;

import android.app.*;
import android.content.*;
import android.graphics.*;
import android.hardware.usb.*;
import android.view.accessibility.AccessibilityNodeInfo;
import java.lang.reflect.Field;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicReference;
import static io.github.ms912x.desktop.AndroidFixtures.*;

final class OutputFixtureTests {
    static <T> T main(Instrumentation test, java.util.concurrent.Callable<T> call) throws Exception {
        AtomicReference<T> result = new AtomicReference<>(); AtomicReference<Throwable> failure = new AtomicReference<>();
        test.runOnMainSync(() -> { try { result.set(call.call()); } catch (Throwable e) { failure.set(e); } });
        if (failure.get() != null) throw new AssertionError("Main-thread fixture", failure.get());
        return result.get();
    }

    static void click(Instrumentation test, String text) throws Exception {
        CaptureFixtureTests.waitFor(() -> {
            AccessibilityNodeInfo root = test.getUiAutomation().getRootInActiveWindow();
            if (root == null) return false;
            for (AccessibilityNodeInfo node : root.findAccessibilityNodeInfosByText(text)) {
                while (node != null && !node.isClickable()) node = node.getParent();
                if (node != null && node.isEnabled() && node.performAction(AccessibilityNodeInfo.ACTION_CLICK)) return true;
            }
            return false;
        }, "UI click: " + text);
        test.waitForIdleSync();
    }

    static final class State implements UsbOutputController.Listener {
        final CopyOnWriteArrayList<String> messages = new CopyOnWriteArrayList<>();
        volatile AdapterCapabilities caps;
        volatile String streamFailure;
        public void onStatus(String status) { messages.add(status); }
        public void onCapabilities(AdapterCapabilities value) { caps = value; }
        public void onBusyChanged(boolean busy) {}
        public void onStreamFailed(String message) { streamFailure = message; }
        void await(String text) throws Exception {
            CaptureFixtureTests.waitFor(() -> messages.stream().anyMatch(s -> s.contains(text)), "Output status: " + text);
        }
    }

    static void run(Instrumentation test) throws Exception {
        Activity activity = test.startActivitySync(new Intent(test.getTargetContext(), MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        // This group owns a fixture controller, including its attach callbacks.
        // Close the activity's startup probe before exercising fake USB devices.
        UsbOutputController startup = (UsbOutputController) ActivityFixtureTests.field(activity, "usbOutputController");
        main(test, () -> { startup.close(); return null; });
        CaptureFixtureTests.waitFor(() -> !startup.isBusy(), "Activity startup probe drains");
        State state = new State(); Host host = new Host(new Connection(edid(mode640())));
        UsbOutputController output = main(test, () -> new UsbOutputController(activity, state, host));
        Field f = UsbOutputController.class.getDeclaredField("receiver"); f.setAccessible(true);
        BroadcastReceiver receiver = (BroadcastReceiver) f.get(output);
        UsbDevice device = adapter();
        CaptureController capture = null;
        try {
            UsbOutputController missing = main(test, () -> new UsbOutputController(activity, state, null));
            main(test, () -> { missing.probeModes(); missing.close(); return null; }); state.await("no USB host service");
            main(test, () -> { output.probeModes(); return null; }); state.await("No MS912x adapter");
            main(test, () -> { output.startStreaming(null, mode640()); return null; }); state.await("Read the connected monitor");
            UsbDevice unrelated = device(0x1234, 0x5678);
            host.devices.add(unrelated);
            main(test, () -> {
                receiver.onReceive(activity, new Intent(UsbManager.ACTION_USB_DEVICE_ATTACHED));
                receiver.onReceive(activity, new Intent(UsbManager.ACTION_USB_DEVICE_ATTACHED)
                        .putExtra(UsbManager.EXTRA_DEVICE, unrelated));
                receiver.onReceive(activity, new Intent(UsbManager.ACTION_USB_DEVICE_ATTACHED)
                        .putExtra(UsbManager.EXTRA_DEVICE, device));
                return null;
            });
            require(host.opens == 0 && host.requests == 0 && !output.isBusy(),
                    "Missing, unsupported, and no-longer-present attachments are ignored");
            host.devices.add(device); host.permission = false;
            main(test, () -> { receiver.onReceive(activity, new Intent(UsbManager.ACTION_USB_DEVICE_ATTACHED)
                    .putExtra(UsbManager.EXTRA_DEVICE, device)); return null; });
            require(output.isBusy() && host.requests == 1, "Permission request blocks duplicate work");
            main(test, () -> {
                output.probeModes();
                receiver.onReceive(activity, new Intent(UsbManager.ACTION_USB_DEVICE_ATTACHED)
                        .putExtra(UsbManager.EXTRA_DEVICE, device));
                return null;
            }); require(host.requests == 1, "No duplicate USB requests on repeated attach");
            host.callback.send(activity, 0, new Intent().putExtra(UsbManager.EXTRA_DEVICE,
                    device(0x345f, 0x9133)));
            test.waitForIdleSync(); require(output.isBusy(), "Unrelated callback ignored");
            host.callback.send(activity, 0, new Intent().putExtra(UsbManager.EXTRA_DEVICE, device));
            state.await("permission was denied"); require(!output.isBusy(), "Denied permission clears busy");
            host.requestFails = true;
            main(test, () -> { output.probeModes(); return null; }); state.await("permission request failed");
            require(!output.isBusy(), "Throwing permission request clears busy"); host.requestFails = false;
            main(test, () -> { output.probeModes(); return null; });
            main(test, () -> { output.stopStreaming(); return null; });
            host.permission = true;
            host.callback.send(activity, 0, new Intent().putExtra(UsbManager.EXTRA_DEVICE, device));
            test.waitForIdleSync(); require(host.opens == 0, "Cancelled permission callback cannot open device");
            host.permission = false;
            main(test, () -> { output.probeModes(); return null; }); host.permission = true;
            host.callback.send(activity, 0, new Intent().putExtra(UsbManager.EXTRA_DEVICE, device));
            CaptureFixtureTests.waitFor(() -> state.caps != null && !output.isBusy(), "Permission grant probes modes");
            main(test, () -> { output.startStreaming(null, mode640()); return null; }); state.await("matching the selected output mode");
            capture = new CaptureController(640, 480, 1, null); CaptureController active = capture;
            DisplayModeOption wrongId = new DisplayModeOption(999, 640, 480, 60, 25170, 800, 525, 144, 35, 96, 2, 1);
            main(test, () -> { output.startStreaming(active, wrongId); return null; }); state.await("no longer available");
            Connection success = new Connection(edid(mode640())); host.connection = success;
            main(test, () -> { output.startStreaming(active, mode640()); return null; });
            Canvas canvas = active.surface().lockCanvas(null); canvas.drawColor(Color.GREEN);
            active.surface().unlockCanvasAndPost(canvas);
            state.await("USB updates sent");
            CaptureFixtureTests.waitFor(() -> success.bulk.size() == 2 * 614416,
                    "Captured frame and idle full repair reach fixture USB");
            int streamingOpens = host.opens;
            main(test, () -> { receiver.onReceive(activity, new Intent(UsbManager.ACTION_USB_DEVICE_ATTACHED)
                    .putExtra(UsbManager.EXTRA_DEVICE, device)); return null; });
            require(output.isBusy() && state.caps != null && host.opens == streamingOpens,
                    "Attach cannot clear modes or interrupt an active screencast");
            main(test, () -> { output.stopStreaming(); return null; });
            CaptureFixtureTests.waitFor(() -> !output.isBusy(), "Stopped USB worker exits");
            require(success.closes == 1 && success.releases == 1, "Stream cancellation closes session");
            Connection failure = new Connection(edid(mode640())); failure.bulkResult = 0; host.connection = failure;
            main(test, () -> { output.startStreaming(active, mode640()); return null; });
            Thread.sleep(120);
            canvas = active.surface().lockCanvas(null); canvas.drawColor(Color.RED); active.surface().unlockCanvasAndPost(canvas);
            state.await("no progress"); CaptureFixtureTests.waitFor(() -> !output.isBusy(), "Failed stream exits");
            require(failure.closes == 1, "Failed bulk session closed");
            CaptureFixtureTests.waitFor(() -> state.streamFailure != null, "Failed stream reports terminal result");
            require(state.streamFailure.contains("no progress"), "Failure callback carries adapter error");
            host.openNull = true;
            main(test, () -> { output.probeModes(); return null; }); state.await("could not open the adapter");
            CaptureFixtureTests.waitFor(() -> !output.isBusy(), "Failed open exits"); host.openNull = false;
            host.connection = new Connection(edid(mode640()));
            main(test, () -> { output.probeModes(); return null; });
            CaptureFixtureTests.waitFor(() -> state.caps != null && !output.isBusy(), "Fresh modes after failure");
            // Simulate the framework detach callback; protected system broadcasts are never spoofed.
            main(test, () -> { receiver.onReceive(activity, new Intent(UsbManager.ACTION_USB_DEVICE_DETACHED)
                    .putExtra(UsbManager.EXTRA_DEVICE, device)); return null; });
            require(state.caps == null, "Detach clears stale EDID modes"); state.await("Adapter disconnected");
            host.devices.add(device(0x345f, 0x9133));
            main(test, () -> { output.probeModes(); return null; }); click(test, "Cancel");
            require(!output.isBusy(), "Multiple-device dialog cancellation");
            main(test, () -> { output.probeModes(); return null; }); click(test, "534d:6021");
            CaptureFixtureTests.waitFor(() -> state.caps != null && !output.isBusy(), "Multiple-device selection probes chosen device");
            host.connection = new Connection(edid(mode640()));
            main(test, () -> {
                output.startStreaming(active, mode640());
                receiver.onReceive(activity, new Intent(UsbManager.ACTION_USB_DEVICE_DETACHED)
                        .putExtra(UsbManager.EXTRA_DEVICE, device));
                require(output.isBusy() && state.caps == null, "Detach waits for the active worker to drain");
                receiver.onReceive(activity, new Intent(UsbManager.ACTION_USB_DEVICE_ATTACHED)
                        .putExtra(UsbManager.EXTRA_DEVICE, device));
                receiver.onReceive(activity, new Intent(UsbManager.ACTION_USB_DEVICE_ATTACHED)
                        .putExtra(UsbManager.EXTRA_DEVICE, device));
                return null;
            });
            CaptureFixtureTests.waitFor(() -> state.caps != null && !output.isBusy(),
                    "Reconnect automatically probes after cancelled worker drains without a chooser");
            require(state.caps.device.equals(device), "Attach targets the connected adapter among multiple devices");
        } finally {
            main(test, () -> { output.close(); output.close(); return null; });
            if (capture != null) capture.close();
            main(test, () -> { activity.finish(); return null; });
        }
        main(test, () -> { output.probeModes(); output.startStreaming(null, null); return null; });
        require(!output.isBusy(), "Closed output ignores requests");
    }
}
