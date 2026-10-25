package io.github.ms912x.desktop;

import android.app.Activity;
import android.app.ActivityOptions;
import android.app.AlertDialog;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.hardware.display.VirtualDisplay;
import android.media.projection.MediaProjectionConfig;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Bundle;
import android.os.IBinder;
import java.util.function.Consumer;

/** UI commands and consent only; the bound service owns all session resources. */
final class ScreencastController implements AutoCloseable {
    private static final int REQUEST_SCREEN_CAPTURE=47;
    private final Activity activity;
    private final MainScreenView screen;
    private final DisplaySelectionController selection;
    private SessionCoordinator coordinator;
    private AlertDialog chooserDialog;
    private SessionCoordinator.Snapshot state;
    private AdapterCapabilities renderedCapabilities;
    private boolean bound,closed,pendingProbe=true;
    private long consentGeneration;
    private boolean restoring,notificationAsked,notificationResult;
    private CaptureSource.Request notificationRequest;
    private static final int REQUEST_NOTIFICATIONS=48;
    private final Consumer<SessionCoordinator.Snapshot> observer=this::render;

    ScreencastController(Activity activity,MainScreenView screen,DisplaySelectionController selection){
        this.activity=activity;this.screen=screen;this.selection=selection;
        bound=activity.bindService(new Intent(activity,CaptureSessionService.class),connection,Context.BIND_AUTO_CREATE);
    }
    private final ServiceConnection connection=new ServiceConnection(){
        public void onServiceConnected(ComponentName name,IBinder binder){
            if(closed)return;
            attach(((CaptureSessionService.LocalBinder)binder).coordinator());
        }
        public void onServiceDisconnected(ComponentName name){coordinator=null;state=null;screen.screencastStatus.setText("Capture session service disconnected.");updateControls();}
    };
    private void attach(SessionCoordinator owner){
        coordinator=owner;
        coordinator.chooser((devices,result)->{
            chooserDialog=new AlertDialog.Builder(activity).setTitle("Choose USB adapter")
                .setItems(UsbOutputController.labels(devices),(dialog,index)->result.accept(devices.get(index)))
                .setNegativeButton("Cancel",(dialog,index)->result.accept(null)).setOnCancelListener(dialog->result.accept(null)).create();
            chooserDialog.show();
        });
        coordinator.observe(observer);
        completeNotificationRequest();
        if(pendingProbe&&!coordinator.active()){pendingProbe=false;probeMonitorModes();}
    }
    SessionCoordinator coordinator(){return coordinator;}
    void restore(Bundle saved){
        if(saved==null)return;
        consentGeneration=saved.getLong("capture.consent_generation",0);
        notificationAsked=saved.getBoolean("capture.notification_asked");
        notificationResult=saved.getBoolean("capture.notification_result");
        if(saved.containsKey("capture.notification_source"))for(DisplayModeOption mode:NativeAdapter.modeCatalog()){
            if(mode.id==saved.getInt("capture.notification_mode"))notificationRequest=new CaptureSource.Request(1,
                    saved.getInt("capture.notification_source"),saved.getString("capture.notification_identity",""),mode,
                    saved.getInt("capture.notification_rotation"),activity.getResources().getDisplayMetrics().densityDpi,true,
                    saved.getBoolean("capture.notification_helper"));
        }
    }
    void save(Bundle out){
        out.putLong("capture.consent_generation",consentGeneration);
        out.putBoolean("capture.notification_asked",notificationAsked);
        out.putBoolean("capture.notification_result",notificationResult);
        if(notificationRequest!=null){
            out.putInt("capture.notification_source",notificationRequest.sourceId);
            out.putString("capture.notification_identity",notificationRequest.identity);
            out.putInt("capture.notification_mode",notificationRequest.mode.id);
            out.putInt("capture.notification_rotation",notificationRequest.rotation);
            out.putBoolean("capture.notification_helper",notificationRequest.helper);
        }
    }
    boolean isActive(){return state!=null&&state.active();}
    void onStart(){ }
    void onStop(){if(coordinator!=null)coordinator.uiBackgrounded();}
    @Override public void close(){
        closed=true;
        if(chooserDialog!=null){chooserDialog.dismiss();chooserDialog=null;}
        if(coordinator!=null)coordinator.unobserve(observer);
        coordinator=null;
        if(bound){activity.unbindService(connection);bound=false;}
    }
    void probeMonitorModes(){
        if(coordinator==null){pendingProbe=true;return;}
        if(isActive()||coordinator.usb().isBusy())return;
        selection.prepareProbe();coordinator.refreshSources();coordinator.probe();
    }
    void onDisplayRemoved(int id){if(coordinator!=null)coordinator.displayRemoved(id);}
    void setShowDraws(boolean enabled){if(coordinator!=null)coordinator.setShowDraws(enabled);}
    void toggleScreencast(){if(isActive())coordinator.stop("Screencast stopped.");else startSession(true);}
    void startSession(boolean output){
        ScreenSource source=selection.selectedSource();
        if(coordinator==null||source==null||!source.canStart()||notificationRequest!=null||(output&&!selection.hasMonitor()))return;
        if(output&&Build.VERSION.SDK_INT>=33&&!notificationAsked&&activity.checkSelfPermission(
                android.Manifest.permission.POST_NOTIFICATIONS)!=android.content.pm.PackageManager.PERMISSION_GRANTED){
            notificationAsked=true;
            notificationRequest=new CaptureSource.Request(1,source.id,source.identity,selection.selectedMode(),selection.selectedRotation(),
                    activity.getResources().getDisplayMetrics().densityDpi,true,source.helper);
            activity.requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS},REQUEST_NOTIFICATIONS);
            return;
        }
        coordinator.start(source.id,source.identity,selection.selectedMode(),selection.selectedRotation(),
                activity.getResources().getDisplayMetrics().densityDpi,output,source.helper);
    }
    void onRequestPermissionsResult(int code){
        if(code==REQUEST_NOTIFICATIONS){notificationResult=true;completeNotificationRequest();}
    }
    private void completeNotificationRequest(){
        if(coordinator==null||notificationRequest==null||!notificationResult)return;
        CaptureSource.Request pending=notificationRequest;notificationRequest=null;notificationResult=false;
        coordinator.start(pending.sourceId,pending.identity,pending.mode,pending.rotation,pending.densityDpi,true,pending.helper);
    }
    void connectHelper(String host,int port){if(coordinator!=null&&!isActive())coordinator.connectHelper(host,port);}
    void disconnectHelper(){if(coordinator!=null&&!isActive())coordinator.disconnectHelper();}
    void prepareHelper(){if(coordinator!=null&&!isActive())coordinator.prepareHelper();}
    void previewHelper(){
        ScreenSource source=selection.selectedSource();
        if(coordinator==null||source==null||isActive())return;
        SourceCapability capability=selection.helperSource(source.id);
        if(capability==null){screen.helperStatus.setText("Prepare and connect the helper, then select a current Android display.");return;}
        coordinator.start(source.id,capability.identity,selection.selectedMode(),screen.helperRotationSelector.getSelectedItemPosition(),
                activity.getResources().getDisplayMetrics().densityDpi,false,true);
    }
    void stopHelper(){if(coordinator!=null&&isActive())coordinator.stop("Helper preview stopped.");}
    void toggleTestScene(){if(coordinator!=null)coordinator.toggleScene();}
    private void render(SessionCoordinator.Snapshot next){
        if(closed)return;
        boolean rebind=state==null;
        state=next;
        if(renderedCapabilities!=next.capabilities){renderedCapabilities=next.capabilities;selection.applyProbedCapabilities(next.capabilities);}
        selection.helperCapabilities(next.helperConnected,next.sources);
        if(rebind&&next.active()&&!restoring){
            restoring=true;selection.restoreSession(next.sourceId,next.mode,next.rotation);restoring=false;
        }
        screen.captureStatus.setText(next.captureStatus);screen.outputStatus.setText(next.outputStatus);
        screen.launchStatus.setText(next.launchStatus);screen.helperStatus.setText(next.helperStatus);
        updateControls();
        if(next.phase==SessionCoordinator.Phase.WAITING_CONSENT&&consentGeneration==0){
            consentGeneration=next.generation;
            MediaProjectionManager manager=activity.getSystemService(MediaProjectionManager.class);
            Intent intent=Build.VERSION.SDK_INT>=34&&next.sourceId==0
                    ?manager.createScreenCaptureIntent(MediaProjectionConfig.createConfigForDefaultDisplay()):manager.createScreenCaptureIntent();
            try{activity.startActivityForResult(intent,REQUEST_SCREEN_CAPTURE);}
            catch(RuntimeException failure){coordinator.stop("Cannot request screen capture: "+failure.getMessage());}
        }
    }
    void onActivityResult(int requestCode,int resultCode,Intent data){
        if(requestCode==REQUEST_SCREEN_CAPTURE&&coordinator!=null){
            long pending=consentGeneration;consentGeneration=0;
            coordinator.consent(pending,resultCode,data);
            render(coordinator.snapshot());
        }
    }
    void updateControls(){
        if(screen.screencastButton==null)return;
        boolean active=isActive(),stopping=state!=null&&state.phase==SessionCoordinator.Phase.STOPPING;
        boolean busy=state!=null&&state.usbBusy;
        ScreenSource source=selection.selectedSource();boolean usable=source!=null&&source.canStart();
        screen.sourceSelector.setEnabled(!active);
        screen.modeSelector.setEnabled(!active&&!busy&&selection.hasMonitor());
        screen.rateSelector.setEnabled(!active&&!busy&&selection.hasMonitor());
        screen.rotationSelector.setEnabled(!active&&(Build.VERSION.SDK_INT>=36||(source!=null&&source.helper)));
        screen.probeButton.setEnabled(!active&&!busy&&coordinator!=null);
        screen.screencastButton.setText(stopping?"Stopping…":active?"Stop screencast":"Start screencast");
        screen.screencastButton.setEnabled(!stopping&&coordinator!=null&&(active||(!busy&&usable&&selection.hasMonitor())));
        screen.screencastStatus.setText(active?state.status:busy?"Reading monitor…":!selection.hasMonitor()
                ?"Choose a screen to detect your monitor.":!usable?(source==null?"Choose a screen.":source.reason)
                :state==null?"Connecting to capture service…":state.status);
        if(screen.previewButton!=null)screen.previewButton.setEnabled(coordinator!=null&&!active&&!busy&&usable);
        if(screen.sceneButton!=null){screen.sceneButton.setEnabled(state!=null&&state.ownScreen&&!stopping);screen.sceneButton.setText(state!=null&&state.sceneVisible?"Hide test scene":"Show test scene");}
        if(screen.prepareHelperButton!=null){
            screen.connectHelperButton.setEnabled(coordinator!=null&&!active&&(state==null||!state.helperBusy&&!state.helperConnected));
            screen.disconnectHelperButton.setEnabled(coordinator!=null&&!active&&state!=null&&(state.helperBusy||state.helperConnected));
            screen.prepareHelperButton.setEnabled(coordinator!=null&&!active&&(state==null||!state.helperBusy));
            screen.helperPreviewButton.setEnabled(state!=null&&state.helperConnected&&!active);
            screen.stopHelperButton.setEnabled(active&&!stopping);
            screen.helperRotationSelector.setEnabled(!active);
        }
    }
    void probeAppLaunchPermission(){
        VirtualDisplay display=coordinator==null||coordinator.source()==null?null:coordinator.source().ownedDisplay();
        if(Build.VERSION.SDK_INT<26||display==null){screen.launchStatus.setText("Create a new test screen before running this probe.");return;}
        int id=display.getDisplay().getDisplayId();ActivityOptions options=ActivityOptions.makeBasic();options.setLaunchDisplayId(id);
        try{activity.startActivity(new Intent("android.settings.SETTINGS"),options.toBundle());screen.launchStatus.setText("App launch accepted for screen "+id+".");}
        catch(RuntimeException failure){screen.launchStatus.setText("App launch denied: "+failure.getMessage());}
    }
}
