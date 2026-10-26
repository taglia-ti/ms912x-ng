package io.github.taglia_ti.ms912x_ng.desktop;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.ActivityOptions;
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
    private ImageSettingsController imageSettings;
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
        imageSettings=new ImageSettingsController(this,screen,options->{if(screencast!=null)screencast.setImageOptions(options);});
        setContentView(screen.root);
        selection = new DisplaySelectionController(this, screen, () -> {
            if (screencast != null) screencast.updateControls();
        }, id -> screencast.onDisplayRemoved(id));
        screencast = new ScreencastController(this, screen, selection);
        screencast.restore(state);
        adbToolsController = new AdbToolsController(this, adbCredentialStore, screen.status,
                screen.endpoints, screen.discoverButton, screen.pairButton);
        screencast.setAdbToolsController(adbToolsController);
        adbToolsController.setSetupListener(new AdbToolsController.SetupListener(){
            @Override public void onDiscoveryChanged(){screencast.onAdbSetupChanged();}
            @Override public void onPairingFinished(String host,boolean success){screencast.onPairingFinished(host,success);}
        });
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
    @Override public void openAppOnNewScreen(){screencast.openAppOnNewScreen();}
    @Override public void openTouchpad(){screencast.openTouchpad();}
    @Override public void sendTouchpadInput(int type,float x,float y,int button){screencast.sendTouchpadInput(type,x,y,button);}
    @Override public void openTouchpadApps(){screencast.openTouchpadApps();}
    @Override public void startPreview() { screencast.startSession(false); }
    @Override public void setShowDraws(boolean enabled) { screencast.setShowDraws(enabled); }
    @Override public void clearSavedOptions() {
        boolean reset=!screencast.isActive();selection.clearSavedOptions(reset);imageSettings.clearSavedOptions();
        screencast.clearSavedMouseOptions();
    }
    @Override public void startDiscovery() { adbToolsController.startDiscovery(); }
    @Override public void choosePairingEndpoint() { adbToolsController.choosePairingEndpoint(); }
    @Override public void runLocalTcpProbe() { adbToolsController.runLocalTcpProbe(); }
    @Override public void probeAppLaunchPermission() { screencast.probeAppLaunchPermission(); }
    @Override public void toggleTestScene() { screencast.toggleTestScene(); }
    @Override public void startTestScreen(){screencast.startTestScreen();}
    @Override public void chooseHelperEndpoint(){adbToolsController.chooseHelperEndpoint(screencast::connectHelper);}
    @Override public void disconnectHelper(){screencast.disconnectHelper();}
    @Override public void prepareHelper() { screencast.prepareHelper(); }
    @Override public void previewHelper() { screencast.previewHelper(); }
    @Override public void stopHelper() { screencast.stopHelper(); }

    @Override protected void onSaveInstanceState(Bundle state) {
        if(screencast!=null)screencast.save(state);
        super.onSaveInstanceState(state);
    }

    @SuppressLint("GestureBackNavigation")
    @Override public void onBackPressed() { handleBack(); }

    private void handleBack() {
        if (screen != null && screen.navigateBack()) return;
        // Show Home while the service keeps the running USB session.
        if (screencast != null && screencast.isActive()) {
            Intent home=new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            if(Build.VERSION.SDK_INT>=26){
                ActivityOptions options=ActivityOptions.makeBasic();
                options.setLaunchDisplayId(getWindowManager().getDefaultDisplay().getDisplayId());
                startActivity(home,options.toBundle());
            }else startActivity(home);
        }else finish();
    }

    @Override public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        if (screen != null) screen.configurationChanged(configuration);
    }

    @Override public void onRequestPermissionsResult(int code,String[] permissions,int[] results) {
        super.onRequestPermissionsResult(code,permissions,results);
        if(screencast!=null)screencast.onRequestPermissionsResult(code);
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
        if(screen!=null)screen.cancelTouchpadGesture();
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
