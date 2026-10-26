package io.github.ms912x.desktop;

import android.content.Context;
import android.content.Intent;
import android.os.Handler;
import android.os.Looper;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.function.Consumer;

/** One main-thread authority for the producer, native reader and USB session. */
final class SessionCoordinator implements AutoCloseable {
    enum Phase { IDLE, STARTING_CAPTURE, WAITING_CONSENT, WAITING_FIRST_FRAME, STREAMING, STOPPING }
    interface ForegroundOwner {
        void begin();
        void end();
    }
    interface SourceFactory { CaptureSource create(int sourceId); }

    static final class Snapshot {
        final Phase phase;
        final long generation;
        final boolean output, usbBusy, helperConnected, helperBusy, sceneVisible, ownScreen, inputSupported;
        final int sourceId, rotation, createdDisplayId;
        final DisplayModeOption mode;
        final AdapterCapabilities capabilities;
        final String status, captureStatus, outputStatus, helperStatus, launchStatus, inputStatus;
        final List<SourceCapability> sources;
        final MouseSharingInfo mouseSharing;
        final int mousePlacement;
        Snapshot(SessionCoordinator owner) {
            phase=owner.phase; generation=owner.generation; output=owner.request!=null&&owner.request.output;
            mouseSharing=owner.mousePolicy.describe(owner.helper.mouseSharing());mousePlacement=owner.mousePolicy.side();
            sourceId=owner.request==null?0:owner.request.sourceId;
            rotation=owner.request==null?0:owner.request.rotation;
            createdDisplayId=owner.source==null?-1:owner.source.createdDisplayId();
            inputSupported=owner.source!=null&&owner.source.inputSupported();
            mode=owner.request==null?null:owner.request.mode;
            capabilities=owner.usb.capabilities(); usbBusy=owner.usb.isBusy();
            helperConnected=owner.helper.connected(); helperBusy=owner.helper.bootstrapBusy();
            sources=Collections.unmodifiableList(new ArrayList<>(owner.helper.sources()));
            status=owner.status; outputStatus=owner.outputStatus; helperStatus=owner.helper.status();
            captureStatus=owner.source==null?owner.lastCaptureStatus:owner.source.status();
            launchStatus=owner.source==null?owner.lastLaunchStatus:owner.source.launchStatus();
            inputStatus=owner.source==null?"Touchpad is available only on an active ADB-created screen.":owner.source.inputStatus();
            ownScreen=owner.source!=null&&owner.source.ownedDisplay()!=null;
            sceneVisible=owner.source!=null&&owner.source.presentation()!=null;
        }
        boolean active() { return phase!=Phase.IDLE; }
    }

    private final Context context;
    private final Handler main=new Handler(Looper.getMainLooper());
    private final ForegroundOwner foreground;
    private final SourceFactory factory;
    private final HelperBackend helper;
    private final MouseSharingPolicy mousePolicy;
    private final android.content.SharedPreferences mousePreferences;
    private final Runnable mouseTick=this::publish;
    private final UsbOutputController usb;
    private Consumer<Snapshot> observer;
    private CaptureSource source;
    private CaptureSource.Request request;
    private Phase phase=Phase.IDLE;
    private long generation=android.os.SystemClock.elapsedRealtimeNanos();
    private boolean foregroundStarted, closed, usbDrained, producerStopped, releasing, showDraws;
    private String status="Ready to screencast",outputStatus="Read the connected monitor's modes.",
            lastCaptureStatus="No capture session.",lastLaunchStatus="App launch probe not run.";
    private Runnable startupTimeout;

    SessionCoordinator(Context context,ForegroundOwner foreground) {
        this(context,foreground,UsbHost.fromContext(context),ShellCaptureFactory.createBackend(context),null);
    }
    /** Fixture boundaries replace USB, helper and foreground roles without claiming Android privileges. */
    SessionCoordinator(Context context,ForegroundOwner foreground,UsbHost host,HelperBackend helper,SourceFactory factory) {
        this.context=context.getApplicationContext();this.foreground=foreground;this.helper=helper;
        mousePreferences=this.context.getSharedPreferences("mouse_sharing",Context.MODE_PRIVATE);
        int placement=0;String mouse="";
        try{
            int previous=mousePreferences.contains("side")?mousePreferences.getInt("side",0):0;
            placement=Math.max(0,Math.min(4,mousePreferences.getInt("placement",previous)));mouse=mousePreferences.getString("device","");
        }
        catch(ClassCastException corrupt){placement=0;}
        mousePolicy=new MouseSharingPolicy(placement,mouse);
        this.factory=factory==null?id->id==ScreenSource.TEST_SCREEN?new OwnedCaptureSource(this.context):new ProjectionCaptureSource(this.context):factory;
        usb=new UsbOutputController(this.context,new UsbOutputController.Listener(){
            public void onStatus(String message,boolean userFacing){
                outputStatus=message;
                if(userFacing&&phase==Phase.IDLE)status=message;
                publish();
            }
            public void onCapabilities(AdapterCapabilities capabilities){
                if(capabilities==null&&phase!=Phase.IDLE&&request!=null&&request.output)stop("Adapter disconnected.");
                publish();
            }
            public void onBusyChanged(boolean busy){publish();}
            public void onStreamFailed(String reason){stop(reason);}
            public void onStreamStarted(CaptureController capture){
                if(phase==Phase.WAITING_FIRST_FRAME&&source!=null&&source.capture()==capture&&request.output){
                    cancelStartup();phase=Phase.STREAMING;status="Screencast active";publish();
                }
            }
        },host,null);
        helper.setObserver(this::publish);
    }
    Snapshot snapshot(){return new Snapshot(this);}
    void refreshMouseSharing(){helper.refreshMouseSharing();}
    void setMousePlacement(int side){
        String device="";try{device=mousePreferences.getString("device","");}catch(ClassCastException corrupt){ }
        setMouseSharing(side,device);
    }
    void clearedMousePreferences(){if(phase==Phase.IDLE){mousePolicy.choose(0,"");publish();}}
    void setMouseSharing(int side,String device){
        if(side==0&&device.isEmpty())try{device=mousePreferences.getString("device","");}catch(ClassCastException corrupt){ }
        mousePreferences.edit().putInt("placement",side).putString("device",device).apply();
        if(side>0)mousePreferences.edit().putInt("side",side).apply();
        mousePolicy.choose(side,device);
        if(side==0&&source==helper&&source.createdDisplayId()>0)helper.setMouseSharing(0,"");
        publish();
    }
    void observe(Consumer<Snapshot> observer){this.observer=observer;publish();}
    void unobserve(Consumer<Snapshot> observer){if(this.observer==observer){this.observer=null;usb.setDeviceChooser(null);}}
    void chooser(UsbOutputController.DeviceChooser chooser){usb.setDeviceChooser(chooser);}
    UsbOutputController usb(){return usb;}
    CaptureSource source(){return source;}
    boolean active(){return phase!=Phase.IDLE;}
    private void publish(){
        main.removeCallbacks(mouseTick);
        int display=!closed&&phase==Phase.STREAMING&&source==helper?source.createdDisplayId():-1;
        MouseSharingPolicy.Action action=mousePolicy.update(display,helper.mouseSharing(),android.os.SystemClock.uptimeMillis());
        if(action.type==MouseSharingPolicy.QUERY)helper.refreshMouseSharing();
        else if(action.type==MouseSharingPolicy.START)helper.setMouseSharing(action.side,action.device);
        if(!closed&&mousePolicy.watching())main.postDelayed(mouseTick,1_000);
        if(observer!=null)observer.accept(snapshot());
    }
    void probe(){if(!closed&&phase==Phase.IDLE&&!usb.isBusy())usb.probeModes();}
    void refreshSources(){if(!closed&&phase==Phase.IDLE)helper.refreshInventory();}
    void connectHelper(String host,int port){if(!closed&&phase==Phase.IDLE)helper.connectLocal(host,port);}
    void disconnectHelper(){if(!closed&&phase==Phase.IDLE)helper.shutdown(this::publish);}
    void prepareHelper(){if(!closed&&phase==Phase.IDLE)helper.prepare();}
    void setShowDraws(boolean enabled){showDraws=enabled;if(source!=null&&source.capture()!=null)source.capture().setShowDraws(enabled);}
    void setImageOptions(ImageOptions options){
        if(source!=null&&phase!=Phase.STOPPING&&source.capture()!=null)source.capture().setImageOptions(options);
    }
    void launchApp(String component){if(source!=null&&phase==Phase.STREAMING&&request.sourceId==ScreenSource.CREATE_NEW)source.launchApp(component);}
    void sendInput(int type,float x,float y,int button){
        if(source!=null&&phase==Phase.STREAMING&&request!=null&&request.sourceId==ScreenSource.CREATE_NEW
                &&source.createdDisplayId()>0&&source.inputSupported()){
            mousePolicy.pause();source.sendInput(type,x,y,button);publish();
        }
    }
    void toggleScene(){if(source!=null&&phase!=Phase.STOPPING){source.toggleScene();publish();}}

    boolean start(int sourceId,String identity,DisplayModeOption mode,int rotation,int densityDpi,boolean output,boolean useHelper){
        if(closed||phase!=Phase.IDLE||usb.isBusy()||mode==null)return false;
        if(output){
            AdapterCapabilities caps=usb.capabilities();
            boolean advertised=false;
            if(caps!=null)for(DisplayModeOption option:caps.modes)if(option.id==mode.id)advertised=true;
            if(!advertised||!usb.hasGrantedAdapter()){status="Allow USB access and read monitor modes before starting.";publish();return false;}
        }
        if(useHelper){
            boolean eligible=sourceId==ScreenSource.CREATE_NEW&&helper.connected();
            if(helper.connected())for(SourceCapability option:helper.sources())
                if(option.id==sourceId&&option.capture&&option.identity.equals(identity))eligible=true;
            if(!eligible){status="ADB setup required, or the selected source has changed. Refresh its inventory.";publish();return false;}
        }else if(sourceId!=ScreenSource.TEST_SCREEN&&sourceId!=ScreenSource.APP_PICKER&&sourceId!=0){
            status="ADB setup required for this screen.";publish();return false;
        }
        request=new CaptureSource.Request(++generation,sourceId,identity,mode,rotation,densityDpi,output,useHelper);
        phase=Phase.STARTING_CAPTURE;status="Starting screencast…";
        usbDrained=false;producerStopped=false;releasing=false;
        final long startedGeneration=generation;
        try{
            if(output){foreground.begin();foregroundStarted=true;}
            source=useHelper?helper:factory.create(sourceId);
            CaptureSource current=source;
            publish();
            startupTimeout=()->{if(generation==startedGeneration&&phase!=Phase.STREAMING&&phase!=Phase.IDLE)stop("Capture/output startup timed out.");};
            main.postDelayed(startupTimeout,20_000);
            current.start(request,new CaptureSource.Listener(){
                private boolean current(){return source==current&&generation==startedGeneration&&phase!=Phase.IDLE&&phase!=Phase.STOPPING;}
                public void onReady(CaptureController capture){
                    if(phase==Phase.WAITING_FIRST_FRAME||phase==Phase.STREAMING)return;
                    if(!current()||capture==null||!capture.isOpen()||capture.frameVersion()==0)return;
                    capture.setShowDraws(showDraws);
                    if(request.output){phase=Phase.WAITING_FIRST_FRAME;publish();usb.startStreaming(capture,request.mode);}
                    else{cancelStartup();phase=Phase.STREAMING;status="Capture preview active";publish();}
                }
                public void onChanged(){if(current())publish();}
                public void onEnded(String reason){if(current())stop(reason);}
                public void onConsentRequired(){if(current()){cancelStartup();phase=Phase.WAITING_CONSENT;status="Waiting for Android capture consent.";publish();}}
            });
        }catch(RuntimeException failure){stop("Capture could not start: "+failure.getMessage());}
        return true;
    }
    void consent(long expectedGeneration,int resultCode,Intent data){
        if(phase!=Phase.WAITING_CONSENT||expectedGeneration!=generation||source==null)return;
        phase=Phase.STARTING_CAPTURE;status="Starting consented capture…";
        final long pending=generation;
        startupTimeout=()->{if(generation==pending&&phase!=Phase.STREAMING&&phase!=Phase.IDLE)stop("Capture/output startup timed out.");};
        main.postDelayed(startupTimeout,20_000);
        publish();source.consent(resultCode,data);
    }
    void displayRemoved(int id){if(request!=null&&request.sourceId==id&&phase!=Phase.IDLE)stop("The selected screen was disconnected.");}
    void uiBackgrounded(){
        if(request!=null&&!request.output&&request.helper)stop("Helper preview stopped when the interface left the foreground.");
    }
    void stop(String reason){
        if(phase==Phase.STOPPING)return;
        if(phase==Phase.IDLE)return;
        phase=Phase.STOPPING;status=reason;cancelStartup();publish();
        usb.stopStreaming(()->{usbDrained=true;finishStop();});
        if(source==null){producerStopped=true;finishStop();}
        else source.stopProducer(()->{producerStopped=true;finishStop();});
    }
    private void finishStop(){
        if(phase!=Phase.STOPPING||!usbDrained||!producerStopped||releasing)return;
        releasing=true;
        CaptureSource old=source;
        Runnable readerReleased=()->{
            if(old!=null){lastCaptureStatus=old.status();lastLaunchStatus=old.launchStatus();}
            source=null;
            Runnable finished=()->{
                if(foregroundStarted){foregroundStarted=false;foreground.end();}
                phase=Phase.IDLE;request=null;releasing=false;publish();
                if(closed){helper.close();usb.close();}
            };
            // A real output session retires its shell process before giving up foreground ownership.
            if(request!=null&&request.output&&request.helper)helper.shutdown(finished);else finished.run();
        };
        if(old==null)readerReleased.run();else old.releaseReader(readerReleased);
    }
    private void cancelStartup(){if(startupTimeout!=null)main.removeCallbacks(startupTimeout);startupTimeout=null;}
    @Override public void close(){
        if(closed)return;closed=true;observer=null;usb.setDeviceChooser(null);
        main.removeCallbacks(mouseTick);
        if(phase!=Phase.IDLE)stop("Capture session service stopped.");
        else{usb.close();helper.close();}
    }
}
