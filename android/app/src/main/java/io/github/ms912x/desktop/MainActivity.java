package io.github.ms912x.desktop;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Intent;
import android.content.res.Configuration;
import android.os.Build;
import android.os.Bundle;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

/** Wires screen actions to their owners and forwards Android lifecycle and capture results. */
public final class MainActivity extends Activity implements MainScreenView.Actions {
    private MainScreenView screen;
    private DisplaySelectionController selection;
    private ScreencastController screencast;
    private AdbCredentialStore adbCredentialStore;
    private AdbToolsController adbToolsController;
    private OnBackInvokedCallback backInvokedCallback;

    @Override
    protected void onCreate(Bundle state) {
        AppAppearance appearance = new AppAppearance(this);
        setTheme(appearance.theme());
        super.onCreate(state);
        // Android's app-sharing picker can relaunch our launcher above the consent owner.
        // Keep that owner alive so its pending result and active capture stay attached.
        if (!isTaskRoot() && Intent.ACTION_MAIN.equals(getIntent().getAction())
                && getIntent().hasCategory(Intent.CATEGORY_LAUNCHER)) {
            finish();
            return;
        }
        if (!BuildConfig.MIRROR_ONLY) adbCredentialStore = new AdbCredentialStore(this);
        screen = new MainScreenView(this, appearance, this);
        setContentView(screen.root);
        selection = new DisplaySelectionController(this, screen, () -> {
            if (screencast != null) screencast.updateControls();
        }, id -> screencast.onDisplayRemoved(id));
        screencast = new ScreencastController(this, screen, selection);
        adbToolsController = new AdbToolsController(this, adbCredentialStore, screen.status,
                screen.endpoints, screen.discoverButton, screen.pairButton);
        if (Build.VERSION.SDK_INT >= 33) {
            backInvokedCallback = this::handleBack;
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, backInvokedCallback);
        }
        selection.applyCapabilities(null);
        selection.refreshDisplayInventory();
        screencast.updateControls();
        screen.applyAppearance();
        screencast.probeMonitorModes();
    }

    @Override public void refreshSourceOptions() {
        if (screencast.isActive()) return;
        selection.refreshDisplayInventory();
        screencast.probeMonitorModes();
    }

    @Override public void probeMonitorModes() { screencast.probeMonitorModes(); }
    @Override public void toggleScreencast() { screencast.toggleScreencast(); }
    @Override public void startPreview() { screencast.startSession(false); }
    @Override public void setShowDraws(boolean enabled) { screencast.setShowDraws(enabled); }
    @Override public void startDiscovery() { adbToolsController.startDiscovery(); }
    @Override public void choosePairingEndpoint() { adbToolsController.choosePairingEndpoint(); }
    @Override public void runLocalTcpProbe() { adbToolsController.runLocalTcpProbe(); }
    @Override public void probeAppLaunchPermission() { screencast.probeAppLaunchPermission(); }
    @Override public void toggleTestScene() { screencast.toggleTestScene(); }

    @SuppressLint("GestureBackNavigation")
    @Override public void onBackPressed() { handleBack(); }

    private void handleBack() {
        if (screen != null && screen.navigateBack()) return;
        // Show Home without finishing the Activity that owns the running session.
        if (screencast != null && screencast.isActive()) startActivity(new Intent(Intent.ACTION_MAIN)
                .addCategory(Intent.CATEGORY_HOME).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        else finish();
    }

    @Override public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        if (screen != null) screen.configurationChanged(configuration);
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (screencast != null) screencast.onActivityResult(requestCode, resultCode, data);
    }

    @Override protected void onStart() {
        super.onStart();
        if (selection != null) selection.start();
        if (screencast != null) screencast.onStart();
    }

    @Override protected void onStop() {
        if (adbToolsController != null) adbToolsController.stopDiscovery();
        if (selection != null) selection.stop();
        if (screencast != null) screencast.onStop();
        super.onStop();
    }

    @Override protected void onDestroy() {
        if (Build.VERSION.SDK_INT >= 33 && backInvokedCallback != null) {
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backInvokedCallback);
            backInvokedCallback = null;
        }
        if (screencast != null) screencast.close();
        if (selection != null) selection.stop();
        if (adbToolsController != null) adbToolsController.close();
        super.onDestroy();
    }
}
