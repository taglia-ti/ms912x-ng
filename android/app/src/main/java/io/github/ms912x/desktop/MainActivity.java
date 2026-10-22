package io.github.ms912x.desktop;

import android.app.Activity;
import android.app.ActivityOptions;
import android.content.ComponentName;
import android.content.Context;
import android.content.ServiceConnection;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.projection.MediaProjectionManager;
import android.content.Intent;
import android.os.IBinder;
import android.os.Handler;
import android.os.Bundle;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.ViewGroup;
import android.view.Display;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.LinkedHashSet;
import java.util.Set;
import java.util.concurrent.atomic.AtomicLong;

public final class MainActivity extends Activity {
    private static final int REQUEST_SCREEN_CAPTURE = 47;
    private static final int SOURCE_CREATE_NEW = 1;
    private static final int SOURCE_CONSENTED_MIRROR = 2;
    private static final long DISCOVERY_TIMEOUT_MS = 15_000L;
    private final Set<String> endpointRows = new LinkedHashSet<>();
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private LinearLayout page;
    private TextView status;
    private LinearLayout endpoints;
    private Button discoverButton;
    private TextView captureStatus;
    private TextView displayInventory;
    private RadioGroup sourceSelector;
    private TextView launchStatus;
    private CaptureController captureController;
    private VirtualDisplay captureDisplay;
    private final AtomicLong acceptedFrames = new AtomicLong();
    private final AtomicLong rejectedFrames = new AtomicLong();
    private AdbEndpointDiscovery discovery;
    private ProjectionCaptureService projectionService;
    private boolean projectionServiceBound;
    private boolean projectionServiceStarted;
    private final ServiceConnection projectionConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            ProjectionCaptureService.LocalBinder localBinder =
                    (ProjectionCaptureService.LocalBinder) binder;
            projectionService = localBinder.service();
            projectionService.setListener(message -> runOnUiThread(() -> {
                if (message.contains("stopped") || message.contains("ended")
                        || message.contains("failed")) {
                    projectionServiceStarted = false;
                }
                if (captureStatus != null) {
                    captureStatus.setText(message);
                }
            }));
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            projectionService = null;
            projectionServiceBound = false;
        }
    };
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

        TextView sourceTitle = new TextView(this);
        sourceTitle.setText("Screen source");
        sourceTitle.setTextSize(18);
        sourceTitle.setPadding(0, pad / 2, 0, 0);
        page.addView(sourceTitle);

        sourceSelector = new RadioGroup(this);
        RadioButton createSource = new RadioButton(this);
        createSource.setId(SOURCE_CREATE_NEW);
        createSource.setText("Create a new independent display (prototype)");
        sourceSelector.addView(createSource);
        RadioButton mirrorSource = new RadioButton(this);
        mirrorSource.setId(SOURCE_CONSENTED_MIRROR);
        mirrorSource.setText("Phone screen/app window (Android consent picker)");
        sourceSelector.addView(mirrorSource);
        sourceSelector.check(SOURCE_CREATE_NEW);
        page.addView(sourceSelector);

        displayInventory = new TextView(this);
        displayInventory.setTextSize(13);
        page.addView(displayInventory);
        refreshDisplayInventory();

        Button captureButton = new Button(this);
        captureButton.setText("Start selected screen source");
        captureButton.setOnClickListener(view -> startSelectedCapture());
        page.addView(captureButton);

        Button launchProbeButton = new Button(this);
        launchProbeButton.setText("Probe app-UID launch permission");
        launchProbeButton.setOnClickListener(view -> probeAppLaunchPermission());
        page.addView(launchProbeButton);

        launchStatus = new TextView(this);
        launchStatus.setText("App-UID virtual-display launch probe not run.");
        page.addView(launchStatus);

        Button stopCaptureButton = new Button(this);
        stopCaptureButton.setText("Stop screen capture");
        stopCaptureButton.setOnClickListener(view -> {
            stopProjectionCapture();
            stopCaptureTest();
        });
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

    private void refreshDisplayInventory() {
        Display[] displays = getSystemService(DisplayManager.class).getDisplays();
        StringBuilder inventory = new StringBuilder("Android display inventory:\n");
        for (Display display : displays) {
            DisplayMetrics metrics = new DisplayMetrics();
            display.getRealMetrics(metrics);
            inventory.append("• ").append(display.getName()).append(" (ID ")
                    .append(display.getDisplayId()).append("): ")
                    .append(metrics.widthPixels).append('×').append(metrics.heightPixels)
                    .append("; capture eligibility not verified\n");
        }
        inventory.append("Existing displays are listed for information. Android consent ")
                .append("selects the screen/window to capture; arbitrary display attachment ")
                .append("is not enabled.");
        displayInventory.setText(inventory.toString());
    }

    private void startSelectedCapture() {
        if (sourceSelector.getCheckedRadioButtonId() == SOURCE_CONSENTED_MIRROR) {
            requestScreenCaptureConsent();
        } else {
            startCaptureTest();
        }
    }

    private void requestScreenCaptureConsent() {
        stopProjectionCapture();
        MediaProjectionManager manager = getSystemService(MediaProjectionManager.class);
        startActivityForResult(manager.createScreenCaptureIntent(), REQUEST_SCREEN_CAPTURE);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_SCREEN_CAPTURE) {
            return;
        }
        if (resultCode != RESULT_OK || data == null) {
            captureStatus.setText("Screen capture consent was declined.");
            return;
        }

        DisplayMetrics metrics = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(metrics);
        Intent serviceIntent = new Intent(this, ProjectionCaptureService.class)
                .putExtra(ProjectionCaptureService.EXTRA_RESULT_CODE, resultCode)
                .putExtra(ProjectionCaptureService.EXTRA_RESULT_DATA, data)
                .putExtra(ProjectionCaptureService.EXTRA_WIDTH, metrics.widthPixels)
                .putExtra(ProjectionCaptureService.EXTRA_HEIGHT, metrics.heightPixels)
                .putExtra(ProjectionCaptureService.EXTRA_DENSITY_DPI, metrics.densityDpi);
        startForegroundService(serviceIntent);
        projectionServiceStarted = true;
        bindProjectionService();
        captureStatus.setText("Consent granted; starting screen/window capture.");
    }

    private void bindProjectionService() {
        if (!projectionServiceBound) {
            projectionServiceBound = bindService(
                    new Intent(this, ProjectionCaptureService.class), projectionConnection,
                    Context.BIND_AUTO_CREATE);
        }
    }

    private void stopProjectionCapture() {
        if (projectionService != null) {
            projectionService.setListener(null);
            projectionService.stopCapture();
            projectionService = null;
        } else if (projectionServiceStarted) {
            stopService(new Intent(this, ProjectionCaptureService.class));
        }
        projectionServiceStarted = false;
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
        stopProjectionCapture();
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
            captureStatus.setText("Screen capture stopped.");
        }
    }

    private void probeAppLaunchPermission() {
        if (captureDisplay == null) {
            launchStatus.setText("Create the capture display before running this probe.");
            return;
        }

        int displayId = captureDisplay.getDisplay().getDisplayId();
        ActivityOptions options = ActivityOptions.makeBasic();
        options.setLaunchDisplayId(displayId);
        Intent settings = new Intent("android.settings.SETTINGS");
        try {
            startActivity(settings, options.toBundle());
            launchStatus.setText("App-UID launch accepted for display " + displayId + ".");
        } catch (SecurityException denied) {
            launchStatus.setText("App-UID launch denied for display " + displayId + ": "
                    + denied.getMessage());
        }
    }

    @Override
    protected void onStop() {
        stopDiscovery();
        if (projectionService != null) {
            projectionService.setListener(null);
        }
        if (projectionServiceBound) {
            unbindService(projectionConnection);
            projectionServiceBound = false;
            projectionService = null;
        }
        super.onStop();
    }

    @Override
    protected void onStart() {
        super.onStart();
        if (projectionServiceStarted) {
            bindProjectionService();
        }
    }

    @Override
    protected void onDestroy() {
        stopProjectionCapture();
        stopCaptureTest();
        super.onDestroy();
    }
}
