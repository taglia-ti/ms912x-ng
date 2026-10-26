package io.github.taglia_ti.ms912x_ng.desktop;

import android.content.Context;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.os.Handler;
import android.os.Looper;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

/** An app-owned producer and scene, independent of the observing Activity. */
final class OwnedCaptureSource implements CaptureSource {
    private final Context context;
    private final Handler main=new Handler(Looper.getMainLooper());
    final AtomicLong accepted=new AtomicLong(),rejected=new AtomicLong();
    private final AtomicBoolean statusPending=new AtomicBoolean();
    private Listener listener;
    private CaptureController capture;
    private VirtualDisplay display;
    private TestScreenPresentation scene;
    private Request request;
    private float refresh;
    private boolean ready,stopped;
    private String status="No capture session.",launch="App launch probe not run.";
    OwnedCaptureSource(Context context){this.context=context.getApplicationContext();}
    @Override public void start(Request request,Listener listener){
        this.request=request;this.listener=listener;
        try{
            capture=new CaptureController(request.mode.width,request.mode.height,request.generation,result->{
                if(result==0)accepted.incrementAndGet();else rejected.incrementAndGet();
                if(result==0&&accepted.get()==1)main.post(()->{
                    if(!stopped&&!ready){ready=true;listener.onReady(capture);}
                });
                if(statusPending.compareAndSet(false,true))main.postDelayed(()->{
                    statusPending.set(false);
                    if(stopped)return;
                    publish();
                },2000);
            },new ImageSettingsStore(context).read());
            display=VirtualScreenOptions.createOwnScreen(context.getSystemService(DisplayManager.class),
                    request.mode.width,request.mode.height,160,capture.surface(),request.mode.refreshHz);
            if(display==null)throw new IllegalStateException("Screen creation returned null");
            VirtualScreenOptions.applyRotation(display,request.rotation);
            refresh=display.getDisplay().getRefreshRate();
            scene=new TestScreenPresentation(context,display.getDisplay());scene.show();
            launch="App-owned screen "+display.getDisplay().getDisplayId()+" ready. App launch probe not run.";
            publish();
        }catch(RuntimeException failure){listener.onEnded("New screen failed: "+failure.getMessage());}
    }
    private void publish(){
        status=String.format(Locale.US,"New test screen · %.1f Hz · %d°\nCaptured %d frames · rejected %d",refresh,request.rotation*90,accepted.get(),rejected.get());
        listener.onChanged();
    }
    @Override public void toggleScene(){
        if(display==null)return;
        try{
            if(scene==null){scene=new TestScreenPresentation(context,display.getDisplay());scene.show();}
            else{scene.dismiss();scene=null;}
            launch="Test scene "+(scene==null?"hidden":"shown")+" on screen "+display.getDisplay().getDisplayId()+".";
            listener.onChanged();
        }catch(RuntimeException failure){listener.onEnded("Test scene failed: "+failure.getMessage());}
    }
    @Override public void stopProducer(Runnable completed){
        stopped=true;
        if(scene!=null){scene.dismiss();scene=null;}
        if(display!=null){display.release();display=null;}
        launch="App-owned screen closed.";status="Screencast stopped.";
        completed.run();
    }
    @Override public void releaseReader(Runnable completed){CaptureController old=capture;capture=null;CaptureCleanup.close(old,completed);}
    @Override public CaptureController capture(){return capture;}
    @Override public String status(){return status;}
    @Override public String launchStatus(){return launch;}
    @Override public VirtualDisplay ownedDisplay(){return display;}
    @Override public TestScreenPresentation presentation(){return scene;}
}
