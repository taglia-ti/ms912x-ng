package io.github.ms912x.helper;

import android.graphics.PointF;
import android.os.Handler;
import io.github.ms912x.ipc.DisplaySourceInfo;
import io.github.ms912x.ipc.MouseSharingResult;

/** Control-thread owner of an opt-in router; it never changes or releases a display. */
final class MouseSharingManager implements AutoCloseable {
    interface Listener {void changed();}
    private final Handler control;
    private final FrameworkDisplayCompat framework;
    private final DisplaySourceInfo screen;
    private final Listener listener;
    private final DisplayInputController screenInput;
    private volatile SharedMouseRouter router;
    private volatile int state=MouseSharingResult.OFF;
    private int side;
    private long epoch;
    private String message="Mouse sharing is off. Connect the main desktop monitor and select a mouse.";
    private String[] devices=new String[0];
    private String selectedIdentity;

    MouseSharingManager(Handler control,FrameworkDisplayCompat framework,DisplaySourceInfo screen,DisplayInputController screenInput,Listener listener){
        this.control=control;this.framework=framework;this.screen=screen;this.screenInput=screenInput;this.listener=listener;
    }
    MouseSharingResult snapshot(long session,long generation,long request){return new MouseSharingResult(session,generation,request,state,side,message,devices);}
    void refresh(){
        try{
            new FrameworkMouseCompat();
            framework.mouseDesktop();devices=NativeMouse.devices();
            if(router==null){state=MouseSharingResult.OFF;message=devices.length==0?"Connect a USB or Bluetooth mouse.":"Choose the adapter position and mouse, then start sharing.";}
        }catch(Exception failure){if(router==null){state=MouseSharingResult.UNAVAILABLE;message=reason(failure);devices=new String[0];}}
    }
    void start(int requestedSide,String identity) {
        if(active()&&identity.equals(selectedIdentity)&&requestedSide>=1&&requestedSide<=4){router.reposition(requestedSide);side=requestedSide;return;}
        stop();
        MouseScalingLease scaling=null;NativeMouse source=null;
        try{
            if(requestedSide<1||requestedSide>4)throw new IllegalArgumentException("Choose an adapter position");
            DisplaySourceInfo main=framework.mouseDesktop();
            FrameworkMouseCompat mouse=new FrameworkMouseCompat();
            PointF mainPosition=mouse.position(main.displayId);
            if(!valid(mainPosition))throw new IllegalStateException("Android did not expose the main desktop cursor");
            source=new NativeMouse(identity);
            // Give InputReader time to register our unassociated uinput mouse.
            Thread.sleep(250);
            mainPosition=mouse.position(main.displayId);
            if(!valid(mainPosition))throw new IllegalStateException("Main desktop mouse disappeared");
            scaling=new MouseScalingLease(mouse);scaling.disable(main.displayId);
            DefaultMouseEndpoint mainEndpoint=DefaultMouseEndpoint.calibrate(source,mouse,main.displayId,main.width,main.height,scaling);
            mainPosition=mainEndpoint.position();
            source.beginReports();
            int mainDensity=framework.density(main.displayId),screenDensity=framework.density(screen.displayId);
            MouseRouteGeometry geometry=new MouseRouteGeometry(main.width,main.height,mainDensity,
                    screen.width,screen.height,screenDensity,requestedSide,mainPosition.x,mainPosition.y);
            long current=++epoch;
            SharedMouseRouter.Endpoint ownedEndpoint=screenInput.mouseEndpoint();screenInput.suspendPointer(true);
            router=new SharedMouseRouter(source,geometry,mainEndpoint,
                    ownedEndpoint,mainEndpoint.gainX(),mainEndpoint.gainY(),()->{
                        checkDisplay(main);checkDisplay(screen);
                        if(framework.density(main.displayId)!=mainDensity||framework.density(screen.displayId)!=screenDensity)
                            throw new IllegalStateException("Display density changed");
                    },new SharedMouseRouter.Listener(){
                        public void stopped(String detail){control.post(()->{
                            if(current!=epoch)return;
                            router=null;state=MouseSharingResult.OFF;message=detail;listener.changed();
                        });}
                        public void crossed(int display){control.post(()->{
                            if(current!=epoch)return;
                            message=display==0?"Mouse sharing is active on the main desktop.":"Mouse sharing is active on the adapter screen.";
                            listener.changed();
                        });}
                    });
            side=requestedSide;state=MouseSharingResult.ACTIVE;message="Mouse sharing is active on the main desktop.";
            selectedIdentity=identity;
            router.start();
            scaling=null;source=null;
        }catch(Exception failure){
            if(source!=null)source.close();
            if(scaling!=null)try{scaling.close();}catch(Exception ignored){ }
            state=MouseSharingResult.FAILED;message=reason(failure);
        }
    }
    boolean active(){return router!=null&&state==MouseSharingResult.ACTIVE;}
    void stop(){
        ++epoch;SharedMouseRouter previous=router;router=null;
        if(previous!=null)previous.close();
        state=MouseSharingResult.OFF;message="Mouse sharing is off.";
    }
    private void checkDisplay(DisplaySourceInfo original) throws Exception {
        DisplaySourceInfo current=framework.source(original.displayId);
        if(current==null||!current.identity.equals(original.identity)||current.width!=original.width||current.height!=original.height||current.rotation!=original.rotation)
            throw new IllegalStateException("A monitor was disconnected or its resolution changed");
    }
    private static boolean valid(PointF p){return p!=null&&Float.isFinite(p.x)&&Float.isFinite(p.y);}
    private static String reason(Throwable error){
        while(error instanceof java.lang.reflect.InvocationTargetException&&error.getCause()!=null)error=error.getCause();
        if(error instanceof ClassNotFoundException||error instanceof NoSuchMethodException)return "This Android version does not provide the required mouse APIs.";
        String result=error.getMessage();return result==null?"Mouse sharing is unavailable on this device.":result;
    }
    @Override public void close(){stop();}
}
