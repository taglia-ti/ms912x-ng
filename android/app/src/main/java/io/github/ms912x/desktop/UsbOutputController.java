package io.github.ms912x.desktop;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.os.SystemClock;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

/** Serializes USB permission, read-only EDID probing, and captured-frame output. */
final class UsbOutputController implements AutoCloseable {
    interface Listener {
        void onStatus(String status);
        void onCapabilities(AdapterCapabilities capabilities);
        void onBusyChanged(boolean busy);
    }

    private static final String ACTION_USB_PERMISSION =
            "io.github.ms912x.desktop.USB_PERMISSION";

    private static final class Request {
        final UsbDevice device;
        final CaptureController capture;
        final DisplayModeOption mode;

        Request(UsbDevice device, CaptureController capture, DisplayModeOption mode) {
            this.device = device;
            this.capture = capture;
            this.mode = mode;
        }
    }

    private final Activity activity;
    private final Listener listener;
    private final UsbHost usbHost;
    private Request pending;
    private AdapterCapabilities capabilities;
    private volatile UsbDevice activeDevice;
    private volatile Thread worker;
    private volatile boolean cancelled;
    private volatile boolean closed;

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals(intent.getAction())) {
                UsbDevice device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
                if (device != null && ((capabilities != null
                        && capabilities.device.equals(device))
                        || (pending != null && pending.device.equals(device))
                        || device.equals(activeDevice))) {
                    stopStreaming();
                    capabilities = null;
                    listener.onCapabilities(null);
                    report("Adapter disconnected. Reconnect and read monitor modes again.");
                }
                return;
            }
            if (!ACTION_USB_PERMISSION.equals(intent.getAction()) || pending == null) return;
            Request request = pending;
            UsbDevice device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
            if (device == null || !request.device.equals(device)) return;
            pending = null;
            if (!usbHost.hasPermission(device)) {
                report("USB permission was denied. Tap Read monitor modes to try again.");
                notifyBusy();
                return;
            }
            startWorker(request);
        }
    };

    UsbOutputController(Activity activity, Listener listener) {
        this(activity, listener, UsbHost.fromContext(activity));
    }

    UsbOutputController(Activity activity, Listener listener, UsbHost usbHost) {
        this.activity = activity;
        this.listener = listener;
        this.usbHost = usbHost;
        IntentFilter filter = new IntentFilter(ACTION_USB_PERMISSION);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        if (Build.VERSION.SDK_INT >= 33) {
            activity.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerLegacyReceiver(filter);
        }
    }

    @SuppressLint("UnspecifiedRegisterReceiverFlag")
    private void registerLegacyReceiver(IntentFilter filter) {
        // API 30-32 has no exported-state flag; a signature permission restricts callbacks.
        activity.registerReceiver(receiver, filter,
                activity.getPackageName() + ".permission.USB_CALLBACK", null);
    }

    boolean isBusy() {
        return pending != null || worker != null;
    }

    void probeModes() {
        if (closed || isBusy()) return;
        capabilities = null;
        listener.onCapabilities(null);
        if (usbHost == null) {
            report("This Android build has no USB host service.");
            return;
        }
        List<UsbDevice> devices = new ArrayList<>();
        for (UsbDevice device : usbHost.devices()) {
            if (UsbAdapterSession.isSupportedDevice(device)) devices.add(device);
        }
        if (devices.isEmpty()) {
            report("No MS912x adapter found. Connect it through USB host/OTG, then retry.");
            return;
        }
        if (devices.size() == 1) {
            requestOrOpen(new Request(devices.get(0), null, null));
            return;
        }
        String[] labels = new String[devices.size()];
        for (int i = 0; i < labels.length; ++i) {
            UsbDevice device = devices.get(i);
            labels[i] = String.format("MS912x %04x:%04x · %s", device.getVendorId(),
                    device.getProductId(), device.getDeviceName());
        }
        new AlertDialog.Builder(activity).setTitle("Choose USB adapter")
                .setItems(labels, (dialog, which) ->
                        requestOrOpen(new Request(devices.get(which), null, null)))
                .setNegativeButton("Cancel", null).show();
    }

    void startStreaming(CaptureController capture, DisplayModeOption mode) {
        if (closed || isBusy()) return;
        if (capabilities == null) {
            report("Read the connected monitor's modes before starting.");
            return;
        }
        if (capture == null || !capture.isOpen() || mode == null
                || capture.width() != mode.width || capture.height() != mode.height) {
            report("Start a screen source matching the selected output mode.");
            return;
        }
        boolean advertised = false;
        for (DisplayModeOption option : capabilities.modes) {
            if (option.id == mode.id) advertised = true;
        }
        if (!advertised) {
            report("The selected mode is no longer available. Read monitor modes again.");
            return;
        }
        requestOrOpen(new Request(capabilities.device, capture, mode));
    }

    private void requestOrOpen(Request request) {
        if (closed || isBusy()) return;
        if (usbHost.hasPermission(request.device)) {
            startWorker(request);
            return;
        }
        pending = request;
        notifyBusy();
        Intent intent = new Intent(ACTION_USB_PERMISSION).setPackage(activity.getPackageName());
        int mutable = Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0;
        PendingIntent permission = PendingIntent.getBroadcast(activity, 912, intent,
                PendingIntent.FLAG_CANCEL_CURRENT | mutable);
        try {
            report("Allow USB access to read the connected monitor.");
            usbHost.requestPermission(request.device, permission);
        } catch (RuntimeException failure) {
            pending = null;
            notifyBusy();
            report("USB permission request failed: " + failure.getMessage());
        }
    }

    private void startWorker(Request request) {
        if (closed || worker != null) return;
        cancelled = false;
        activeDevice = request.device;
        worker = new Thread(() -> {
            try (UsbAdapterSession session = UsbAdapterSession.open(usbHost, request.device)) {
                if (request.capture == null) {
                    report("Reading monitor EDID…");
                    AdapterCapabilities result = session.probeCapabilities(request.device);
                    activity.runOnUiThread(() -> {
                        if (!closed && !cancelled) {
                            capabilities = result;
                            listener.onCapabilities(result);
                            listener.onStatus(result.monitorName + " · "
                                    + result.modes.size() + " supported output modes");
                        }
                    });
                } else {
                    stream(session, request);
                }
            } catch (IOException | RuntimeException failure) {
                if (request.capture != null) request.capture.invalidateBaseline();
                if (!cancelled) report("Adapter operation failed: " + failure.getMessage());
            } finally {
                activeDevice = null;
                worker = null;
                notifyBusy();
            }
        }, request.capture == null ? "ms912x-edid" : "ms912x-usb-stream");
        notifyBusy();
        worker.start();
    }

    private void stream(UsbAdapterSession session, Request request) throws IOException {
        CaptureController capture = request.capture;
        DisplayModeOption mode = request.mode;
        report("Configuring " + mode + "…");
        capture.invalidateBaseline();
        session.configureMode(mode);
        ByteBuffer packet = ByteBuffer.allocateDirect(capture.packetCapacity());
        long batches = 0;
        long lastReport = SystemClock.elapsedRealtime();
        report("Sharing at " + mode + ". Waiting for captured frames…");
        while (!cancelled && !closed && capture.isOpen()) {
            FrameSubmission submission = capture.prepareLatestFrame(packet);
            if (submission != null) {
                try (FrameSubmission batch = submission) {
                    if (cancelled || closed) break;
                    session.sendFrame(batch.packet());
                    batch.commit();
                }
                ++batches;
                long now = SystemClock.elapsedRealtime();
                if (batches == 1 || now - lastReport >= 2000) {
                    report("Sharing · " + mode + " · " + batches + " USB updates sent");
                    lastReport = now;
                }
            } else {
                try {
                    Thread.sleep(20);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }
        report("Sharing stopped.");
    }

    private void report(String message) {
        activity.runOnUiThread(() -> {
            if (!closed) listener.onStatus(message);
        });
    }

    private void notifyBusy() {
        activity.runOnUiThread(() -> {
            if (!closed) listener.onBusyChanged(isBusy());
        });
    }

    void stopStreaming() {
        pending = null;
        cancelled = true;
        Thread running = worker;
        if (running != null) running.interrupt();
        notifyBusy();
    }

    @Override
    public void close() {
        if (closed) return;
        closed = true;
        stopStreaming();
        activity.unregisterReceiver(receiver);
    }
}
