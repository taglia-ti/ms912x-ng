package io.github.taglia_ti.ms912x_ng.desktop;

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
import android.os.Handler;
import android.os.Looper;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;

/** Serializes USB permission, read-only EDID probing, and captured-frame output. */
final class UsbOutputController implements AutoCloseable {
    interface Listener {
        void onStatus(String status, boolean userFacing);
        void onCapabilities(AdapterCapabilities capabilities);
        void onBusyChanged(boolean busy);
        void onStreamFailed(String message);
        default void onStreamStarted(CaptureController capture) { }
    }

    interface DeviceChooser { void choose(List<UsbDevice> devices, Consumer<UsbDevice> result); }

    private static final String ACTION_USB_PERMISSION =
            "io.github.taglia_ti.ms912x_ng.desktop.USB_PERMISSION";
    private static final String PERMISSION_SCHEME="ms912x-usb-permission";

    private static final class Request {
        final UsbDevice device;
        final CaptureController capture;
        final DisplayModeOption mode;
        final android.net.Uri permissionUri=android.net.Uri.parse(PERMISSION_SCHEME+":"+java.util.UUID.randomUUID());

        Request(UsbDevice device, CaptureController capture, DisplayModeOption mode) {
            this.device = device;
            this.capture = capture;
            this.mode = mode;
        }
    }

    private final Context context;
    private DeviceChooser chooser;
    private final Listener listener;
    private final UsbHost usbHost;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final List<Runnable> drainCallbacks = new ArrayList<>();
    private Request pending;
    private UsbDevice attachedProbe;
    private AdapterCapabilities capabilities;
    private volatile UsbDevice activeDevice;
    private volatile Thread worker;
    private volatile boolean cancelled;
    private volatile boolean closed;
    private long choiceGeneration;
    private boolean choosing;

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (closed) return;
            if (UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(intent.getAction())) {
                probeAttachedDevice(intent.getParcelableExtra(UsbManager.EXTRA_DEVICE));
                return;
            }
            if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals(intent.getAction())) {
                UsbDevice device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
                if (device != null && device.equals(attachedProbe)) attachedProbe = null;
                if (device != null && ((capabilities != null
                        && capabilities.device.equals(device))
                        || (pending != null && pending.device.equals(device))
                        || device.equals(activeDevice))) {
                    stopStreaming();
                    capabilities = null;
                    listener.onCapabilities(null);
                    report("Adapter disconnected. Reconnect it to read monitor modes automatically.");
                }
                return;
            }
            if (!ACTION_USB_PERMISSION.equals(intent.getAction()) || pending == null) return;
            Request request = pending;
            UsbDevice device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
            if (device == null || !request.device.equals(device)) return;
            if(!request.permissionUri.equals(intent.getData()))return;
            pending = null;
            if (!usbHost.hasPermission(device)) {
                capabilities = null;
                listener.onCapabilities(null);
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
        this(activity.getApplicationContext(), listener, usbHost, (devices, result) -> {
            String[] labels = labels(devices);
            new AlertDialog.Builder(activity).setTitle("Choose USB adapter")
                    .setItems(labels, (dialog, which) -> result.accept(devices.get(which)))
                    .setNegativeButton("Cancel", (dialog, which) -> result.accept(null))
                    .setOnCancelListener(dialog -> result.accept(null)).show();
        });
    }

    UsbOutputController(Context context, Listener listener, UsbHost usbHost, DeviceChooser chooser) {
        this.context = context.getApplicationContext();
        this.chooser = chooser;
        this.listener = listener;
        this.usbHost = usbHost;
        IntentFilter filter = new IntentFilter();
        filter.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        IntentFilter permissions=new IntentFilter(ACTION_USB_PERMISSION);
        permissions.addDataScheme(PERMISSION_SCHEME);
        registerCallbacks(filter);
        try{registerCallbacks(permissions);}
        catch(RuntimeException failure){this.context.unregisterReceiver(receiver);throw failure;}
    }

    private void registerCallbacks(IntentFilter filter){
        if (Build.VERSION.SDK_INT >= 33) {
            this.context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerLegacyReceiver(filter);
        }
    }

    @SuppressLint("UnspecifiedRegisterReceiverFlag")
    private void registerLegacyReceiver(IntentFilter filter) {
        // API 24-32 has no exported-state flag; a signature permission restricts callbacks.
        context.registerReceiver(receiver, filter,
                context.getPackageName() + ".permission.USB_CALLBACK", null);
    }

    boolean isBusy() {
        return choosing || pending != null || worker != null;
    }

    AdapterCapabilities capabilities() { return capabilities; }
    boolean hasGrantedAdapter() {
        return !closed && capabilities != null && usbHost != null
                && usbHost.devices().contains(capabilities.device) && usbHost.hasPermission(capabilities.device);
    }
    void setDeviceChooser(DeviceChooser chooser) {
        this.chooser = chooser;
        if (choosing) { choosing=false; ++choiceGeneration; notifyBusy(); }
    }

    static String[] labels(List<UsbDevice> devices) {
        String[] labels = new String[devices.size()];
        for (int i=0;i<labels.length;++i) {
            UsbDevice device=devices.get(i);
            labels[i]=String.format(java.util.Locale.ROOT,"MS912x %04x:%04x · %s",device.getVendorId(),device.getProductId(),device.getDeviceName());
        }
        return labels;
    }

    private void onMain(Runnable action) {
        if (Looper.myLooper() == Looper.getMainLooper()) action.run(); else mainHandler.post(action);
    }

    private void probeAttachedDevice(UsbDevice device) {
        if (closed || usbHost == null || device == null
                || !UsbAdapterSession.isSupportedDevice(device)
                || !usbHost.devices().contains(device)) return;
        if (isBusy()) {
            // A reconnect can arrive before the detached device's worker has drained.
            // Leave active probes, permission prompts, and screencasts undisturbed.
            if (cancelled && worker != null) attachedProbe = device;
            return;
        }
        capabilities = null;
        listener.onCapabilities(null);
        requestOrOpen(new Request(device, null, null));
    }

    void probeModes() {
        if (closed || isBusy()) return;
        if (usbHost == null) {
            capabilities = null;
            listener.onCapabilities(null);
            report("This Android build has no USB host service.");
            return;
        }
        List<UsbDevice> devices = new ArrayList<>();
        for (UsbDevice device : usbHost.devices()) {
            if (UsbAdapterSession.isSupportedDevice(device)) devices.add(device);
        }
        if (devices.isEmpty()) {
            capabilities = null;
            listener.onCapabilities(null);
            report("No MS912x adapter found. Connect it through USB host/OTG, then retry.");
            return;
        }
        if (capabilities != null && !devices.contains(capabilities.device)) {
            capabilities = null;
            listener.onCapabilities(null);
        }
        if (devices.size() == 1) {
            requestOrOpen(new Request(devices.get(0), null, null));
            return;
        }
        if (chooser == null) { report("Open the app to choose a USB adapter."); return; }
        long generation = ++choiceGeneration;
        choosing = true; notifyBusy();
        chooser.choose(java.util.Collections.unmodifiableList(devices), selected -> onMain(() -> {
            if (closed || generation != choiceGeneration) return;
            choosing = false; notifyBusy();
            if (selected != null && usbHost.devices().contains(selected)) requestOrOpen(new Request(selected,null,null));
        }));
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
        Intent intent = new Intent(ACTION_USB_PERMISSION).setPackage(context.getPackageName())
                .setData(request.permissionUri);
        int mutable = Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0;
        PendingIntent permission = PendingIntent.getBroadcast(context, 912, intent,
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
            String[] streamFailure = {null};
            try (UsbAdapterSession session = UsbAdapterSession.open(usbHost, request.device)) {
                if (request.capture == null) {
                    reportRoutine("Reading monitor EDID…");
                    AdapterCapabilities result = session.probeCapabilities(request.device);
                    onMain(() -> {
                        if (!closed && !cancelled) {
                            capabilities = result;
                            listener.onCapabilities(result);
                            listener.onStatus("Monitor ready.", false);
                        }
                    });
                } else {
                    stream(session, request);
                }
            } catch (IOException | RuntimeException failure) {
                if (request.capture != null) request.capture.invalidateBaseline();
                if (!cancelled) {
                    String message = "Adapter operation failed: " + failure.getMessage();
                    report(message);
                    if (request.capture != null) streamFailure[0] = message;
                }
            } finally {
                onMain(() -> {
                    // Publish completion after the worker's result callbacks. A new probe
                    // must not reset cancellation before an old result is discarded.
                    activeDevice = null;
                    worker = null;
                    // Session close and any submitted batch completion/abort
                    // precede this barrier. Callers may now release capture.
                    List<Runnable> drained = new ArrayList<>(drainCallbacks);
                    drainCallbacks.clear();
                    UsbDevice device = attachedProbe;
                    attachedProbe = null;
                    if (device != null) probeAttachedDevice(device);
                    notifyBusy();
                    if (streamFailure[0] != null) listener.onStreamFailed(streamFailure[0]);
                    // Publish the old operation's final result before a completion
                    // callback can start a replacement session.
                    for (Runnable callback : drained) callback.run();
                });
            }
        }, request.capture == null ? "ms912x-edid" : "ms912x-usb-stream");
        notifyBusy();
        worker.start();
    }

    private void stream(UsbAdapterSession session, Request request) throws IOException {
        CaptureController capture = request.capture;
        DisplayModeOption mode = request.mode;
        reportRoutine("Configuring " + mode + "…");
        capture.invalidateBaseline();
        session.configureMode(mode);
        ByteBuffer packet = ByteBuffer.allocateDirect(capture.packetCapacity());
        long batches = 0;
        long lastReport = SystemClock.elapsedRealtime();
        reportRoutine("Screencast at " + mode + ". Waiting for captured frames…");
        while (!cancelled && !closed && capture.isOpen()) {
            long observedVersion = capture.changeVersion();
            FrameSubmission submission = capture.preparePacedFrame(packet, mode.refreshHz);
            if (submission != null) {
                try (FrameSubmission batch = submission) {
                    if (cancelled || closed) break;
                    long started = SystemClock.elapsedRealtimeNanos();
                    int bytes = session.sendFrame(batch.packet());
                    long duration = SystemClock.elapsedRealtimeNanos() - started;
                    capture.recordUsb(bytes, duration);
                    batch.commit();
                }
                ++batches;
                if (batches == 1) onMain(() -> {
                    if (!closed && !cancelled && request.capture.isOpen()) listener.onStreamStarted(request.capture);
                });
                long now = SystemClock.elapsedRealtime();
                if (batches == 1 || now - lastReport >= 2000) {
                    reportRoutine("Screencast · " + mode + " · " + batches + " USB updates sent");
                    lastReport = now;
                }
            } else {
                try {
                    capture.awaitFrame(observedVersion);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }
        reportRoutine("Screencast stopped.");
    }

    private void report(String message) {
        report(message, true);
    }

    private void reportRoutine(String message) {
        report(message, false);
    }

    private void report(String message, boolean userFacing) {
        onMain(() -> {
            if (!closed) listener.onStatus(message, userFacing);
        });
    }

    private void notifyBusy() {
        onMain(() -> {
            if (!closed) listener.onBusyChanged(isBusy());
        });
    }

    void stopStreaming() {
        stopStreaming(null);
    }

    /** Main-thread cancellation; completion runs on main after the USB owner has drained, even after close(). */
    void stopStreaming(Runnable drained) {
        pending = null;
        choosing = false;
        ++choiceGeneration;
        attachedProbe = null;
        cancelled = true;
        Thread running = worker;
        if (drained != null) {
            if (running == null) mainHandler.post(drained);
            else drainCallbacks.add(drained);
        }
        if (running != null) running.interrupt();
        notifyBusy();
    }

    @Override
    public void close() {
        if (closed) return;
        closed = true;
        stopStreaming();
        context.unregisterReceiver(receiver);
    }
}
