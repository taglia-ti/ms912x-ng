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
import android.provider.Settings;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.function.Consumer;

/** UI commands and consent only; the bound service owns all session resources. */
final class ScreencastController implements AutoCloseable {
    private static final int REQUEST_SCREEN_CAPTURE=47;
    private final Activity activity;
    private final MainScreenView screen;
    private final DisplaySelectionController selection;
    private SessionCoordinator coordinator;
    private AdbToolsController adbTools;
    private AlertDialog chooserDialog;
    private AlertDialog createScreenDialog;
    private TextView createScreenProgress,createScreenSteps;
    private Button createScreenSettings,createScreenPair,createScreenScan,createScreenAdapter;
    private SessionCoordinator.Snapshot state;
    private AdapterCapabilities renderedCapabilities;
    private boolean bound,closed,pendingProbe=true;
    private long consentGeneration;
    private boolean restoring,notificationAsked,notificationResult;
    private boolean createScreenSetup,adapterProbeRequested,autoConnectAttempted,setupDiscoveryStarted;
    private CaptureSource.Request notificationRequest;
    private static final int REQUEST_NOTIFICATIONS=48;
    private static final int REQUEST_WIRELESS_SETTINGS=49;
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
    void setAdbToolsController(AdbToolsController tools){adbTools=tools;}
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
    void onStop(){if(screen!=null)screen.cancelTouchpadGesture();if(coordinator!=null)coordinator.uiBackgrounded();}
    @Override public void close(){
        closed=true;
        if(chooserDialog!=null){chooserDialog.dismiss();chooserDialog=null;}
        cancelCreateScreenSetup();
        if(createScreenDialog!=null){createScreenDialog.dismiss();createScreenDialog=null;}
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
    void startTestScreen(){
        if(coordinator==null||coordinator.active())return;
        java.util.List<DisplayModeOption> modes=NativeAdapter.modeCatalog();
        if(modes.isEmpty())return;
        coordinator.start(ScreenSource.TEST_SCREEN,"",modes.get(0),0,
                activity.getResources().getDisplayMetrics().densityDpi,false,false);
    }
    void toggleScreencast(){if(isActive())coordinator.stop("Screencast stopped.");else startSession(true);}
    void startSession(boolean output){
        ScreenSource source=selection.selectedSource();
        if(coordinator==null||source==null||notificationRequest!=null)return;
        if(output&&source.id==ScreenSource.CREATE_NEW
                &&(state==null||!state.helperConnected||!adapterReady())){beginCreateScreenSetup();return;}
        if(!source.canStart()||(output&&!selection.hasMonitor()))return;
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

    private void beginCreateScreenSetup(){
        if(createScreenSetup){updateCreateScreenDialog();return;}
        createScreenSetup=true;adapterProbeRequested=false;autoConnectAttempted=false;setupDiscoveryStarted=false;
        showCreateScreenDialog();
        if(coordinator==null){updateCreateScreenDialog();return;}
        if(coordinator.usb().isBusy())adapterProbeRequested=true;else requestAdapterProbe();
        if(state==null||!state.helperConnected&&!state.helperBusy)startWirelessDiscovery();
        tryAutoConnect();updateCreateScreenDialog();maybeAdvanceCreateScreenSetup();
    }

    private void showCreateScreenDialog(){
        if(createScreenDialog!=null&&createScreenDialog.isShowing())return;
        LinearLayout content=new LinearLayout(activity);content.setOrientation(LinearLayout.VERTICAL);
        int pad=Math.round(20*activity.getResources().getDisplayMetrics().density);content.setPadding(pad,pad/2,pad,0);
        createScreenProgress=new TextView(activity);createScreenProgress.setTextSize(16);content.addView(createScreenProgress);
        ScrollView scroll=new ScrollView(activity);createScreenSteps=new TextView(activity);createScreenSteps.setTextSize(14);
        createScreenSteps.setPadding(0,pad/2,0,pad/2);scroll.addView(createScreenSteps);content.addView(scroll,new LinearLayout.LayoutParams(-1,Math.round(170*activity.getResources().getDisplayMetrics().density)));
        createScreenSettings=new Button(activity);createScreenSettings.setText("Open Wireless debugging settings");createScreenSettings.setAllCaps(false);
        createScreenSettings.setOnClickListener(view->openWirelessDebuggingSettings());content.addView(createScreenSettings);
        createScreenPair=new Button(activity);createScreenPair.setText("Pair this phone");createScreenPair.setAllCaps(false);
        createScreenPair.setOnClickListener(view->{if(adbTools!=null)adbTools.choosePairingEndpoint();});content.addView(createScreenPair);
        createScreenScan=new Button(activity);createScreenScan.setText("Refresh Wireless debugging discovery");createScreenScan.setAllCaps(false);
        createScreenScan.setOnClickListener(view->{autoConnectAttempted=false;setupDiscoveryStarted=false;startWirelessDiscovery();tryAutoConnect();updateCreateScreenDialog();});content.addView(createScreenScan);
        createScreenAdapter=new Button(activity);createScreenAdapter.setText("Retry adapter and monitor check");createScreenAdapter.setAllCaps(false);
        createScreenAdapter.setOnClickListener(view->{adapterProbeRequested=false;requestAdapterProbe();updateCreateScreenDialog();});content.addView(createScreenAdapter);
        createScreenDialog=new AlertDialog.Builder(activity).setTitle("Set up your HDMI screen").setView(content)
                .setNegativeButton("Close",(dialog,which)->cancelCreateScreenSetup()).create();
        createScreenDialog.setOnCancelListener(dialog->cancelCreateScreenSetup());
        createScreenDialog.setOnDismissListener(dialog->{
            if(createScreenSetup){createScreenSetup=false;if(adbTools!=null)adbTools.stopDiscovery();}
        });
        createScreenDialog.show();updateCreateScreenDialog();
    }

    private void requestAdapterProbe(){
        if(coordinator==null)return;
        if(selection.hasMonitor()&&coordinator.usb().hasGrantedAdapter())return;
        if(adapterProbeRequested||coordinator.usb().isBusy())return;
        adapterProbeRequested=true;selection.prepareProbe();coordinator.probe();
    }

    private void startWirelessDiscovery(){
        if(adbTools==null||state!=null&&(state.helperConnected||state.helperBusy))return;
        if(!adbTools.discoveryRunning())adbTools.startDiscovery();
        setupDiscoveryStarted=true;
    }

    private boolean adapterReady(){return coordinator!=null&&selection.hasMonitor()&&coordinator.usb().hasGrantedAdapter();}

    private void tryAutoConnect(){
        if(!createScreenSetup||coordinator==null||adbTools==null||state==null||state.helperConnected
                ||state.helperBusy||autoConnectAttempted)return;
        for(AdbEndpointDiscovery.Endpoint endpoint:adbTools.connectEndpoints){
            String host=endpoint.address.getHostAddress();
            if(adbTools.hasReusableCredentialFor(host)){
                autoConnectAttempted=true;coordinator.connectHelper(host,endpoint.port);updateCreateScreenDialog();return;
            }
        }
    }

    private void maybeAdvanceCreateScreenSetup(){
        if(!createScreenSetup||coordinator==null||state==null||state.phase!=SessionCoordinator.Phase.IDLE)return;
        if(!adapterReady()&&!coordinator.usb().isBusy()&&!adapterProbeRequested)requestAdapterProbe();
        if(!state.helperConnected&&!state.helperBusy&&!setupDiscoveryStarted)startWirelessDiscovery();
        tryAutoConnect();
        if(state.helperConnected&&adapterReady()&&!state.usbBusy){
            createScreenSetup=false;if(createScreenDialog!=null&&createScreenDialog.isShowing())createScreenDialog.dismiss();
            startSession(true);return;
        }
        updateCreateScreenDialog();
    }

    void onAdbSetupChanged(){if(!createScreenSetup)return;tryAutoConnect();if(coordinator!=null)render(coordinator.snapshot());else updateCreateScreenDialog();}
    void onPairingFinished(String host,boolean success){
        if(!createScreenSetup)return;
        if(success){autoConnectAttempted=false;
            if(adbTools!=null)for(AdbEndpointDiscovery.Endpoint endpoint:adbTools.connectEndpoints)
                if(endpoint.address.getHostAddress().equals(host)&&adbTools.hasReusableCredentialFor(host)&&coordinator!=null){
                    autoConnectAttempted=true;coordinator.connectHelper(host,endpoint.port);break;
                }
        }
        if(coordinator!=null)render(coordinator.snapshot());else updateCreateScreenDialog();
    }

    private void updateCreateScreenDialog(){
        if(!createScreenSetup||createScreenProgress==null)return;
        boolean connected=state!=null&&state.helperConnected;
        boolean connecting=state!=null&&state.helperBusy;
        boolean ready=adapterReady();
        String adapter=ready?"Ready · adapter permission granted and monitor modes read"
                :state!=null&&state.usbBusy?"Checking adapter or waiting for USB permission…"
                :coordinator==null?"Waiting for capture service…":state==null?"Checking adapter…":state.outputStatus;
        String wireless=connected?"Helper connected on this phone."
                :connecting?(state==null?"Starting helper…":state.helperStatus)
                :adbTools!=null&&adbTools.discoveryRunning()?"Searching this phone's Wireless debugging services…"
                :"Enable Wireless debugging in Android settings, then refresh discovery.";
        boolean pairedEndpoint=false,storedCredential=false;
        if(adbTools!=null)for(AdbEndpointDiscovery.Endpoint endpoint:adbTools.connectEndpoints){
            String host=endpoint.address.getHostAddress();
            if(adbTools.hasReusableCredentialFor(host))storedCredential=true;
            if(adbTools.hasMatchingPairingEndpoint(host))pairedEndpoint=true;
        }
        String pairing=storedCredential?"Saved pairing found; helper connection will start automatically."
                :pairedEndpoint?"On first use, open Wireless debugging and choose Pair device with pairing code. Then tap Pair this phone here and enter Android's six-digit code."
                :"First use needs one manual pairing. In Wireless debugging choose Pair device with pairing code; after discovery finds it, tap Pair this phone and enter the six-digit code.";
        String helper=connected?"Ready.":connecting?(state==null?"Connecting…":state.helperStatus)
                :autoConnectAttempted?(state==null?"Connection started.":state.helperStatus)
                :"Will connect automatically after pairing credentials are available.";
        String next=connected&&ready?"Starting the screencast…":!ready?"Connect or authorize the USB adapter.":!connected&&storedCredential?"Connecting the ADB helper…":!connected&&pairedEndpoint?"Pair this phone once to enable automatic helper connection.":"Enable Wireless debugging, then return here.";
        createScreenProgress.setText(next);
        createScreenSteps.setText("1. USB adapter\n"+adapter+"\n\n2. Wireless debugging\n"+wireless
                +"\n\n3. Pairing\n"+pairing+"\n\n4. ADB helper\n"+helper
                +(state!=null&&state.outputStatus!=null&&!ready?"\n\nAdapter detail: "+state.outputStatus:""));
        createScreenPair.setVisibility(pairedEndpoint&&!storedCredential&&!connected?View.VISIBLE:View.GONE);
        createScreenPair.setEnabled(pairedEndpoint&&(state==null||!state.helperBusy));
        createScreenSettings.setVisibility(connected?View.GONE:View.VISIBLE);
        createScreenScan.setVisibility(connected?View.GONE:View.VISIBLE);
        createScreenAdapter.setVisibility(ready?View.GONE:View.VISIBLE);
        createScreenAdapter.setEnabled(coordinator!=null&&!coordinator.usb().isBusy());
    }

    private void cancelCreateScreenSetup(){
        if(!createScreenSetup)return;createScreenSetup=false;
        if(adbTools!=null)adbTools.stopDiscovery();
    }

    private void openWirelessDebuggingSettings(){
        try{activity.startActivityForResult(new Intent(Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS),REQUEST_WIRELESS_SETTINGS);}
        catch(RuntimeException unavailable){
            try{activity.startActivityForResult(new Intent(Settings.ACTION_SETTINGS),REQUEST_WIRELESS_SETTINGS);}
            catch(RuntimeException failed){if(createScreenProgress!=null)createScreenProgress.setText("Open Android Settings and enable Developer options and Wireless debugging.");}
        }
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
    void openTouchpad(){
        if(coordinator==null||state==null||!state.inputSupported||state.createdDisplayId<0||state.mode==null)return;
        screen.showTouchpad(state.createdDisplayId,state.mode.width,state.mode.height);
        coordinator.sendInput(TouchpadInput.MOVE,0,0,0);
    }
    void sendTouchpadInput(int type,float x,float y,int button){if(coordinator!=null)coordinator.sendInput(type,x,y,button);}
    void openTouchpadApps(){if(coordinator!=null&&state!=null&&state.createdDisplayId>0)openAppOnNewScreen();}
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
        maybeAdvanceCreateScreenSetup();
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
        if(requestCode==REQUEST_WIRELESS_SETTINGS&&createScreenSetup){
            setupDiscoveryStarted=false;autoConnectAttempted=false;startWirelessDiscovery();tryAutoConnect();updateCreateScreenDialog();return;
        }
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
        boolean createNew=source!=null&&source.id==ScreenSource.CREATE_NEW;
        screen.sourceSelector.setEnabled(!active);
        screen.modeSelector.setEnabled(!active&&!busy&&selection.hasMonitor());
        screen.rateSelector.setEnabled(!active&&!busy&&selection.hasMonitor());
        screen.rotationSelector.setEnabled(!active&&(Build.VERSION.SDK_INT>=36||(source!=null&&source.helper)));
        screen.probeButton.setEnabled(!active&&!busy&&coordinator!=null);
        screen.screencastButton.setText(stopping?"Stopping…":active?"Stop screencast":"Start screencast");
        screen.screencastButton.setEnabled(!stopping&&coordinator!=null&&(active||createNew||(!busy&&usable&&selection.hasMonitor())));
        screen.screencastStatus.setText(active?state.status:createNew?"Start will check the adapter and guide Wireless debugging setup.":busy?"Reading monitor…":!selection.hasMonitor()
                ?"Choose a screen to detect your monitor.":!usable?(source==null?"Choose a screen.":source.reason)
                :state==null?"Connecting to capture service…":state.status);
        if(screen.previewButton!=null)screen.previewButton.setEnabled(coordinator!=null&&!active&&!busy&&usable);
        screen.openAppButton.setVisibility(state!=null&&state.createdDisplayId>=0?android.view.View.VISIBLE:android.view.View.GONE);
        screen.openAppButton.setEnabled(state!=null&&state.phase==SessionCoordinator.Phase.STREAMING);
        boolean ownDesktop=state!=null&&state.phase==SessionCoordinator.Phase.STREAMING
                &&state.sourceId==ScreenSource.CREATE_NEW&&state.createdDisplayId>0&&state.helperConnected;
        screen.setTouchpadEntry(ownDesktop,ownDesktop&&state.inputSupported,ownDesktop?state.createdDisplayId:-1);
        screen.touchpadStatus.setText(state==null?"Touchpad requires a running screen created by the ADB helper.":state.inputStatus);
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
    void openAppOnNewScreen(){
        if(coordinator==null||state==null||state.createdDisplayId<0)return;
        android.content.pm.PackageManager packages=activity.getPackageManager();
        java.util.List<android.content.pm.ResolveInfo> found=packages.queryIntentActivities(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER),0);
        java.util.List<android.content.pm.ResolveInfo> options=new java.util.ArrayList<>();
        for(android.content.pm.ResolveInfo app:found)if(app.activityInfo!=null&&app.activityInfo.enabled&&app.activityInfo.exported
                &&!app.activityInfo.packageName.equals(activity.getPackageName()))options.add(app);
        options.sort((a,b)->a.loadLabel(packages).toString().compareToIgnoreCase(b.loadLabel(packages).toString()));
        String[] labels=new String[options.size()];
        for(int i=0;i<labels.length;++i)labels[i]=options.get(i).loadLabel(packages)+" · "+options.get(i).activityInfo.packageName;
        long session=state.generation;
        new AlertDialog.Builder(activity).setTitle("Open on independent screen #"+state.createdDisplayId)
                .setItems(labels,(dialog,index)->{
                    if(coordinator==null||state==null||state.generation!=session)return;
                    android.content.pm.ActivityInfo app=options.get(index).activityInfo;
                    coordinator.launchApp(new ComponentName(app.packageName,app.name).flattenToString());
                }).setNegativeButton("Cancel",null).show();
    }

    void probeAppLaunchPermission(){
        VirtualDisplay display=coordinator==null||coordinator.source()==null?null:coordinator.source().ownedDisplay();
        if(Build.VERSION.SDK_INT<26||display==null){screen.launchStatus.setText("Create a new test screen before running this probe.");return;}
        int id=display.getDisplay().getDisplayId();ActivityOptions options=ActivityOptions.makeBasic();options.setLaunchDisplayId(id);
        try{activity.startActivity(new Intent("android.settings.SETTINGS"),options.toBundle());screen.launchStatus.setText("App launch accepted for screen "+id+".");}
        catch(RuntimeException failure){screen.launchStatus.setText("App launch denied: "+failure.getMessage());}
    }
}
