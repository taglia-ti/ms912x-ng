package io.github.ms912x.desktop;

import android.app.Activity;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.os.Handler;
import android.os.Bundle;
import android.os.Looper;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.LinkedHashSet;
import java.util.Set;
import java.util.concurrent.atomic.AtomicLong;

public final class MainActivity extends Activity {
    private static final long DISCOVERY_TIMEOUT_MS = 15_000L;
    private final Set<String> endpointRows = new LinkedHashSet<>();
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private LinearLayout page;
    private TextView status;
    private LinearLayout endpoints;
    private Button discoverButton;
    private TextView captureStatus;
    private CaptureController captureController;
    private VirtualDisplay captureDisplay;
    private final AtomicLong acceptedFrames = new AtomicLong();
    private final AtomicLong rejectedFrames = new AtomicLong();
    private AdbEndpointDiscovery discovery;
    private final Runnable discoveryTimeout = () -> {
        stopDiscovery();
        if (status != null) {
            status.setText("Discovery timed out. Enable Wireless debugging and try again.\n"
                    + "Pairing and connecting are not implemented yet.");
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);

        page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setGravity(Gravity.CENTER_VERTICAL);
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        page.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("MS912x Desktop");
        title.setTextSize(24);
        page.addView(title);

        TextView status = new TextView(this);
        this.status = status;
        status.setText("Android 11+ virtual desktop prototype\n\n"
                + "Pairing protocol and shell display helper are not implemented yet.\n"
                + "No phone-screen mirroring fallback is used.");
        status.setTextSize(16);
        status.setPadding(0, pad / 2, 0, 0);
        page.addView(status);

        discoverButton = new Button(this);
        discoverButton.setText("Find wireless debugging endpoints");
        discoverButton.setOnClickListener(view -> startDiscovery());
        page.addView(discoverButton);

        Button captureButton = new Button(this);
        captureButton.setText("Create virtual display for capture test");
        captureButton.setOnClickListener(view -> startCaptureTest());
        page.addView(captureButton);

        Button stopCaptureButton = new Button(this);
        stopCaptureButton.setText("Stop capture test");
        stopCaptureButton.setOnClickListener(view -> stopCaptureTest());
        page.addView(stopCaptureButton);

        captureStatus = new TextView(this);
        captureStatus.setText("Local virtual display capture test not running.");
        page.addView(captureStatus);

        ScrollView scroll = new ScrollView(this);
        endpoints = new LinearLayout(this);
        endpoints.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(endpoints, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        LinearLayout.LayoutParams scrollParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1);
        page.addView(scroll, scrollParams);
        setContentView(page);
    }

    private void startDiscovery() {
        if (discovery != null) {
            discovery.close();
            discovery = null;
        }
        endpointRows.clear();
        endpoints.removeAllViews();
        status.setText("Searching local DNS-SD for pairing and TLS connection services.\n"
                + "This does not pair or connect yet.");
        discovery = new AdbEndpointDiscovery(this, new AdbEndpointDiscovery.Listener() {
            @Override
            public void onEndpoint(AdbEndpointDiscovery.Endpoint endpoint) {
                runOnUiThread(() -> {
                    if (discovery != null && endpointRows.add(endpoint.toString())) {
                        TextView row = new TextView(MainActivity.this);
                        row.setText(endpoint.toString());
                        row.setTextSize(15);
                        endpoints.addView(row);
                    }
                });
            }

            @Override
            public void onFailure(String serviceType, int errorCode) {
                runOnUiThread(() -> status.setText(
                        "Discovery error for " + serviceType + ": " + errorCode
                                + "\nPairing and connecting are not implemented yet."));
            }
        });
        try {
            discovery.start();
            discoverButton.setEnabled(false);
            mainHandler.postDelayed(discoveryTimeout, DISCOVERY_TIMEOUT_MS);
        } catch (RuntimeException failure) {
            stopDiscovery();
            status.setText("Could not start local service discovery: "
                    + failure.getClass().getSimpleName());
        }
    }

    private void stopDiscovery() {
        mainHandler.removeCallbacks(discoveryTimeout);
        if (discovery != null) {
            discovery.close();
            discovery = null;
        }
        if (discoverButton != null) {
            discoverButton.setEnabled(true);
        }
    }

    private void startCaptureTest() {
        stopCaptureTest();
        acceptedFrames.set(0);
        rejectedFrames.set(0);
        int width = 720;
        int height = 1280;
        int densityDpi = 240;
        try {
            captureController = new CaptureController(width, height, 1, result -> {
                if (result == 0) {
                    acceptedFrames.incrementAndGet();
                } else {
                    rejectedFrames.incrementAndGet();
                }
                runOnUiThread(() -> {
                    if (captureStatus != null && captureController != null) {
                        captureStatus.setText("Local capture running; accepted native copies: "
                                + acceptedFrames.get() + ", rejected: "
                                + rejectedFrames.get());
                    }
                });
            });
            DisplayManager displayManager = getSystemService(DisplayManager.class);
            captureDisplay = displayManager.createVirtualDisplay(
                    "ms912x-local-capture-test", width, height, densityDpi,
                    captureController.surface(),
                    DisplayManager.VIRTUAL_DISPLAY_FLAG_OWN_CONTENT_ONLY
                            | DisplayManager.VIRTUAL_DISPLAY_FLAG_PRESENTATION);
            if (captureDisplay == null) {
                throw new IllegalStateException("Virtual display creation returned null");
            }
            captureStatus.setText("Created app-owned display "
                    + captureDisplay.getDisplay().getDisplayId()
                    + ". Capture callbacks are active. A shell helper is required to launch "
                    + "apps on the display; the normal app UID is denied that permission.");
        } catch (RuntimeException failure) {
            stopCaptureTest();
            captureStatus.setText("Capture test failed: "
                    + failure.getClass().getSimpleName() + ": " + failure.getMessage());
        }
    }

    private void stopCaptureTest() {
        if (captureDisplay != null) {
            captureDisplay.release();
            captureDisplay = null;
        }
        if (captureController != null) {
            CaptureController oldController = captureController;
            captureController = null;
            oldController.close();
        }
        if (captureStatus != null) {
            captureStatus.setText("Local virtual display capture test stopped.");
        }
    }

    @Override
    protected void onStop() {
        stopDiscovery();
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        stopCaptureTest();
        super.onDestroy();
    }
}
