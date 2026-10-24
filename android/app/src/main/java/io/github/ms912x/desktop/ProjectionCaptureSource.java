package io.github.ms912x.desktop;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Build;
import android.os.IBinder;

/** Retains the consented producer binding until USB and its reader have drained. */
final class ProjectionCaptureSource implements CaptureSource {
    private final Context context;
    private Listener listener;
    private Request request;
    private ProjectionCaptureService service;
    private CaptureController capture;
    private boolean bound,stopped,closed,ready,commandIssued;
    private Intent consentData;
    private int consentCode;
    private String status="Waiting for capture consent.";
    ProjectionCaptureSource(Context context){this.context=context.getApplicationContext();}
    @Override public void start(Request request,Listener listener){this.request=request;this.listener=listener;listener.onConsentRequired();}
    @Override public void consent(int resultCode,Intent data){
        if(stopped||closed||bound)return;
        if(resultCode!=Activity.RESULT_OK||data==null){listener.onEnded("Screen capture consent was declined.");return;}
        consentCode=resultCode;consentData=new Intent(data);
        status="Consent granted. Starting screen capture…";listener.onChanged();
        try{
            bound=context.bindService(new Intent(context,ProjectionCaptureService.class),connection,Context.BIND_AUTO_CREATE);
            if(!bound)listener.onEnded("Screen capture service could not bind.");
        }catch(RuntimeException failure){listener.onEnded("Screen capture could not start: "+failure.getMessage());}
    }
    private final ServiceConnection connection=new ServiceConnection(){
        @Override public void onServiceConnected(ComponentName name,IBinder binder){
            service=((ProjectionCaptureService.LocalBinder)binder).service();
            if(stopped||closed){if(bound){context.unbindService(this);bound=false;}service=null;return;}
            service.setListener((state,message)->{
                status=message;
                if(stopped||!commandIssued)return;
                listener.onChanged();
                if(state==ProjectionCaptureService.State.FAILED||state==ProjectionCaptureService.State.STOPPED)listener.onEnded(message);
                else if(state==ProjectionCaptureService.State.ACTIVE&&service!=null){
                    capture=service.captureController();
                    if(!ready&&capture!=null&&capture.frameVersion()>0){ready=true;listener.onReady(capture);}
                }
            });
            if(stopped||closed)return;
            commandIssued=true;
            try{
                Intent keep=new Intent(context,ProjectionCaptureService.class).setAction(ProjectionCaptureService.ACTION_KEEP_ALIVE);
                if(Build.VERSION.SDK_INT>=26)context.startForegroundService(keep);else context.startService(keep);
                service.beginCapture(new Intent().putExtra(ProjectionCaptureService.EXTRA_RESULT_CODE,consentCode)
                        .putExtra(ProjectionCaptureService.EXTRA_RESULT_DATA,consentData)
                        .putExtra(ProjectionCaptureService.EXTRA_WIDTH,request.mode.width)
                        .putExtra(ProjectionCaptureService.EXTRA_HEIGHT,request.mode.height)
                        .putExtra(ProjectionCaptureService.EXTRA_ROTATION,request.rotation)
                        .putExtra(ProjectionCaptureService.EXTRA_DENSITY_DPI,request.densityDpi));
                capture=service.captureController();
                consentData=null;
            }catch(RuntimeException failure){listener.onEnded("Screen capture could not start: "+failure.getMessage());}
        }
        @Override public void onServiceDisconnected(ComponentName name){
            service=null;
            if(!stopped)listener.onEnded("Screen capture service disconnected.");
        }
        @Override public void onNullBinding(ComponentName name){if(!stopped)listener.onEnded("Screen capture service returned no binding.");}
        @Override public void onBindingDied(ComponentName name){onServiceDisconnected(name);}
    };
    @Override public void stopProducer(Runnable completed){
        stopped=true;consentData=null;
        if(service!=null)service.stopProducer();
        completed.run();
    }
    @Override public void releaseReader(Runnable completed){
        closed=true;
        Runnable finish=()->{
            if(service!=null){service.setListener(null);service.stopSelf();service=null;}
            if(bound){context.unbindService(connection);bound=false;}
            capture=null;completed.run();
        };
        if(service!=null)service.releaseReader(finish);else CaptureCleanup.close(capture,finish);
    }
    @Override public CaptureController capture(){return capture;}
    @Override public String status(){return status;}
}
