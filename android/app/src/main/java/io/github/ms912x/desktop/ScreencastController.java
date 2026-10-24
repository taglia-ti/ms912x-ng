package io.github.ms912x.desktop;

import android.app.Activity;
import android.app.ActivityOptions;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.projection.MediaProjectionConfig;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.IBinder;
import android.util.DisplayMetrics;
import android.view.Display;

import java.util.Locale;
import java.util.concurrent.atomic.AtomicLong;

/** Owns capture consent, projection binding, presentation screens and their USB-output session. */
final class ScreencastController implements AutoCloseable {
    private static final int REQUEST_SCREEN_CAPTURE = 47;
    private final Activity activity;
    private final MainScreenView screen;
    private final DisplaySelectionController selection;
    private final AtomicLong acceptedFrames = new AtomicLong();
    private final AtomicLong rejectedFrames = new AtomicLong();
    private String userMessage;
    private boolean stoppingScreencast;
    private DisplayModeOption sessionMode;
    private int sessionSource;
    private int sessionRotation;
    private float sessionRefreshHz;
    private boolean settingsLocked;
    private boolean pendingStartOutput;
    private CaptureController captureController;
    private VirtualDisplay captureDisplay;
    private TestScreenPresentation presentation;
    private ProjectionCaptureService projectionService;
    private boolean projectionServiceBound;
    private boolean projectionServiceStarted;
    private UsbOutputController usbOutputController;

    private final ServiceConnection projectionConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            projectionService = ((ProjectionCaptureService.LocalBinder) binder).service();
            projectionService.setListener((state, message) -> {
                showCaptureStatus(message, state == ProjectionCaptureService.State.FAILED
                        || state == ProjectionCaptureService.State.STOPPED);
                if (state == ProjectionCaptureService.State.FAILED
                        || state == ProjectionCaptureService.State.STOPPED) {
                    projectionServiceStarted = false;
                    pendingStartOutput = false;
                    settingsLocked = false;
                    usbOutputController.stopStreaming();
                    updateControls();
                } else if (state == ProjectionCaptureService.State.ACTIVE
                        && projectionService != null) {
                    startPendingOutput(projectionService.captureController());
                }
            });
            startPendingOutput(projectionService.captureController());
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            projectionService = null;
            projectionServiceBound = false;
            projectionServiceStarted = false;
            pendingStartOutput = false;
            settingsLocked = false;
            if (usbOutputController != null) usbOutputController.stopStreaming();
            updateControls();
        }
    };

    ScreencastController(Activity activity, MainScreenView screen, DisplaySelectionController selection) {
        this.activity = activity;
        this.screen = screen;
        this.selection = selection;
        usbOutputController = new UsbOutputController(activity, new UsbOutputController.Listener() {
            @Override public void onStatus(String message, boolean userFacing) {
                showOutputStatus(message, userFacing);
            }
            @Override public void onCapabilities(AdapterCapabilities next) {
                if (next == null && settingsLocked) stopSharing();
                selection.applyProbedCapabilities(next);
            }
            @Override public void onBusyChanged(boolean busy) { updateControls(); }
            @Override public void onStreamFailed(String message) { finishFailedOutput(message); }
        });
    }

    boolean isActive() { return settingsLocked; }

    void probeMonitorModes() {
        if (settingsLocked || usbOutputController.isBusy()) return;
        userMessage = null;
        selection.prepareProbe();
        usbOutputController.probeModes();
    }

    void setShowDraws(boolean enabled) {
        CaptureController capture = captureController != null ? captureController
                : projectionService == null ? null : projectionService.captureController();
        if (capture != null) capture.setShowDraws(enabled);
    }

    void onDisplayRemoved(int id) {
        if (settingsLocked && sessionSource == id) {
            stopSharing();
            showCaptureStatus("The selected screen was disconnected.", true);
        }
    }

    void onStart() {
        if (projectionServiceStarted) bindProjectionService();
    }

    void onStop() {
        // Keep startup and system-stop callbacks connected while casting in the background.
        if (!settingsLocked) {
            if (projectionService != null) projectionService.setListener(null);
            if (projectionServiceBound) {
                activity.unbindService(projectionConnection);
                projectionServiceBound = false;
                projectionService = null;
            }
        }
    }

    @Override public void close() {
        stopSharing();
        usbOutputController.close();
    }

    private void showOutputStatus(String message, boolean userFacing) {
        screen.outputStatus.setText(message);
        userMessage = userFacing ? message : null;
        updateControls();
    }

    private void finishFailedOutput(String message) {
        if (!settingsLocked) return;
        pendingStartOutput = false;
        settingsLocked = false;
        stoppingScreencast = false;
        sessionMode = null;
        stopProjectionCapture();
        stopCaptureTest();
        userMessage = message;
        updateControls();
    }

    private void showCaptureStatus(String message, boolean userFacing) {
        screen.captureStatus.setText(message);
        if (userFacing) {
            userMessage = message;
            updateControls();
        }
    }

    void toggleScreencast() {
        if (settingsLocked) stopSharing();
        else startSession(true);
    }

    void updateControls() {
        if (screen.screencastButton == null) return;
        boolean busy = usbOutputController != null && usbOutputController.isBusy();
        ScreenSource source = selection.selectedSource();
        boolean usable = source != null && source.canStart();
        screen.sourceSelector.setEnabled(!settingsLocked);
        screen.modeSelector.setEnabled(!settingsLocked && !busy && selection.hasMonitor());
        screen.rateSelector.setEnabled(!settingsLocked && !busy && selection.hasMonitor());
        screen.rotationSelector.setEnabled(!settingsLocked && Build.VERSION.SDK_INT >= 36);
        screen.probeButton.setEnabled(!settingsLocked && !busy);
        if (!busy) stoppingScreencast = false;
        screen.screencastButton.setText(settingsLocked
                ? "Stop screencast"
                : stoppingScreencast ? "Stopping…" : "Start screencast");
        screen.screencastButton.setEnabled(settingsLocked || (!busy && usable && selection.hasMonitor()));
        screen.screencastStatus.setText(userMessage != null ? userMessage : settingsLocked
                ? (pendingStartOutput ? "Starting screencast…" : "Screencast active")
                : stoppingScreencast ? "Stopping screencast…"
                : busy ? "Reading monitor…"
                : !selection.hasMonitor() ? "Choose a screen to detect your monitor."
                : !usable ? "This screen cannot be captured by this build."
                : "Ready to screencast");
        if (screen.previewButton != null) screen.previewButton.setEnabled(!settingsLocked && !busy && usable);
        if (screen.sceneButton != null) {
            screen.sceneButton.setEnabled(captureDisplay != null);
            screen.sceneButton.setText(presentation == null ? "Show test scene" : "Hide test scene");
        }
    }

    void startSession(boolean sendOutput) {
        if (settingsLocked || usbOutputController.isBusy()) return;
        ScreenSource source = selection.selectedSource();
        if (source == null || !source.canStart() || (sendOutput && !selection.hasMonitor())) return;
        userMessage = null;
        sessionMode = selection.selectedMode();
        sessionSource = source.id;
        sessionRotation = selection.selectedRotation();
        settingsLocked = true;
        pendingStartOutput = sendOutput;
        updateControls();
        if (source.id == ScreenSource.CREATE_NEW) {
            startCaptureTest();
        } else {
            requestScreenCaptureConsent();
        }
    }

    private void requestScreenCaptureConsent() {
        stopProjectionCapture();
        stopCaptureTest();
        MediaProjectionManager manager = activity.getSystemService(MediaProjectionManager.class);
        Intent consent = Build.VERSION.SDK_INT >= 34 && sessionSource == Display.DEFAULT_DISPLAY
                ? manager.createScreenCaptureIntent(MediaProjectionConfig.createConfigForDefaultDisplay())
                : manager.createScreenCaptureIntent();
        try {
            activity.startActivityForResult(consent, REQUEST_SCREEN_CAPTURE);
        } catch (RuntimeException failure) {
            settingsLocked = false;
            pendingStartOutput = false;
            showCaptureStatus("Cannot request screen capture: " + failure.getMessage(), true);
            updateControls();
        }
    }

    void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQUEST_SCREEN_CAPTURE || !settingsLocked || sessionMode == null) return;
        if (resultCode != Activity.RESULT_OK || data == null) {
            settingsLocked = false;
            pendingStartOutput = false;
            showCaptureStatus("Screen capture consent was declined.", true);
            updateControls();
            return;
        }
        DisplayMetrics metrics = activity.getResources().getDisplayMetrics();
        Intent service = new Intent(activity, ProjectionCaptureService.class)
                .putExtra(ProjectionCaptureService.EXTRA_RESULT_CODE, resultCode)
                .putExtra(ProjectionCaptureService.EXTRA_RESULT_DATA, data)
                .putExtra(ProjectionCaptureService.EXTRA_WIDTH, sessionMode.width)
                .putExtra(ProjectionCaptureService.EXTRA_HEIGHT, sessionMode.height)
                .putExtra(ProjectionCaptureService.EXTRA_ROTATION, sessionRotation)
                .putExtra(ProjectionCaptureService.EXTRA_DENSITY_DPI, metrics.densityDpi);
        try {
            if (Build.VERSION.SDK_INT >= 26) activity.startForegroundService(service);
            else activity.startService(service);
            projectionServiceStarted = true;
            showCaptureStatus("Consent granted. Starting screen capture…", false);
            bindProjectionService();
        } catch (RuntimeException failure) {
            stopSharing();
            showCaptureStatus("Screen capture could not start: " + failure.getMessage(), true);
        }
    }

    private void bindProjectionService() {
        if (!projectionServiceBound) {
            projectionServiceBound = activity.bindService(new Intent(activity, ProjectionCaptureService.class),
                    projectionConnection, Context.BIND_AUTO_CREATE);
        }
    }

    private void stopProjectionCapture() {
        if (projectionService != null) {
            projectionService.setListener(null);
            projectionService.stopCapture();
            projectionService = null;
        }
        if (projectionServiceBound) {
            activity.unbindService(projectionConnection);
            projectionServiceBound = false;
        }
        if (projectionServiceStarted) activity.stopService(new Intent(activity, ProjectionCaptureService.class));
        projectionServiceStarted = false;
    }

    private void startCaptureTest() {
        stopProjectionCapture();
        stopCaptureTest();
        acceptedFrames.set(0);
        rejectedFrames.set(0);
        try {
            captureController = new CaptureController(sessionMode.width, sessionMode.height, 1,
                    result -> {
                        if (result == 0) acceptedFrames.incrementAndGet();
                        else rejectedFrames.incrementAndGet();
                        activity.runOnUiThread(() -> {
                            if (captureController != null) {
                                showCaptureStatus(String.format(Locale.US,
                                        "New test screen · %.1f Hz · %d°\nCaptured %d frames · rejected %d",
                                        sessionRefreshHz, sessionRotation * 90,
                                        acceptedFrames.get(), rejectedFrames.get()), false);
                            }
                        });
                    });
            captureDisplay = VirtualScreenOptions.createOwnScreen(
                    activity.getSystemService(DisplayManager.class), sessionMode.width, sessionMode.height,
                    160, captureController.surface(), sessionMode.refreshHz);
            if (captureDisplay == null) throw new IllegalStateException("Screen creation returned null");
            VirtualScreenOptions.applyRotation(captureDisplay, sessionRotation);
            sessionRefreshHz = captureDisplay.getDisplay().getRefreshRate();
            showTestScene();
            screen.launchStatus.setText("App-owned screen " + captureDisplay.getDisplay().getDisplayId()
                    + " ready. App launch probe not run.");
            showCaptureStatus(String.format(Locale.US, "New test screen · Android %.1f Hz · %d°",
                    captureDisplay.getDisplay().getRefreshRate(), sessionRotation * 90), false);
            startPendingOutput(captureController);
            updateControls();
        } catch (RuntimeException failure) {
            stopSharing();
            showCaptureStatus("New screen failed: " + failure.getMessage(), true);
        }
    }

    private void startPendingOutput(CaptureController capture) {
        if (capture != null) capture.setShowDraws(screen.showDrawsToggle.isChecked());
        if (pendingStartOutput && capture != null && capture.isOpen()) {
            pendingStartOutput = false;
            usbOutputController.startStreaming(capture, sessionMode);
        }
    }

    private void showTestScene() {
        presentation = new TestScreenPresentation(activity, captureDisplay.getDisplay());
        presentation.show();
    }

    void toggleTestScene() {
        if (captureDisplay == null) return;
        try {
            if (presentation == null) {
                showTestScene();
            } else {
                presentation.dismiss();
                presentation = null;
            }
            screen.launchStatus.setText("Test scene " + (presentation == null ? "hidden" : "shown")
                    + " on screen " + captureDisplay.getDisplay().getDisplayId() + ".");
            updateControls();
        } catch (RuntimeException failure) {
            stopSharing();
            showCaptureStatus("Test scene failed: " + failure.getMessage(), true);
        }
    }

    private void stopCaptureTest() {
        if (presentation != null) {
            presentation.dismiss();
            presentation = null;
        }
        if (captureDisplay != null) {
            captureDisplay.release();
            captureDisplay = null;
            screen.launchStatus.setText("App-owned screen closed.");
        }
        if (captureController != null) {
            CaptureController old = captureController;
            captureController = null;
            old.close();
        }
    }

    private void stopSharing() {
        stoppingScreencast = settingsLocked && usbOutputController != null && usbOutputController.isBusy();
        userMessage = null;
        pendingStartOutput = false;
        if (usbOutputController != null) usbOutputController.stopStreaming();
        stopProjectionCapture();
        stopCaptureTest();
        settingsLocked = false;
        sessionMode = null;
        if (screen.captureStatus != null) showCaptureStatus("Screencast stopped.", false);
        updateControls();
    }

    void probeAppLaunchPermission() {
        if (Build.VERSION.SDK_INT < 26 || captureDisplay == null) {
            screen.launchStatus.setText("Create a new test screen before running this probe.");
            return;
        }
        int id = captureDisplay.getDisplay().getDisplayId();
        ActivityOptions options = ActivityOptions.makeBasic();
        options.setLaunchDisplayId(id);
        try {
            activity.startActivity(new Intent("android.settings.SETTINGS"), options.toBundle());
            screen.launchStatus.setText("App launch accepted for screen " + id + ".");
        } catch (RuntimeException failure) {
            screen.launchStatus.setText("App launch denied: " + failure.getMessage());
        }
    }
}
