package io.github.ms912x.desktop;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.Consumer;

/** Service-owned JNI handle. Its persistent Rust runtime outlives all Activity/JNI calls. */
final class EmbeddedHelperConnection {
    private final Context context;
    private final Handler main=new Handler(Looper.getMainLooper());
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private final List<Runnable> closedCallbacks=new ArrayList<>();
    private final Consumer<String> status;
    private final Runnable disconnected;
    private long handle;
    private boolean starting,closing,closed;

    EmbeddedHelperConnection(Context context,Consumer<String> status,Runnable disconnected){
        this.context=context.getApplicationContext();this.status=status;this.disconnected=disconnected;
    }
    boolean busy(){return starting||handle!=0;}
    void start(String host,int port,String digest,byte[] envelope){
        if(busy()||closed){Arrays.fill(envelope,(byte)0);throw new IllegalStateException("Previous ADB helper connection has not closed");}
        starting=true;closing=false;
        worker.execute(()->{
            long started=0;String failure=null;
            try{
                AdbCredentialStore credentials=new AdbCredentialStore(context);
                String device=credentials.pairedDeviceFor(host);
                if(device==null)throw new IllegalStateException("Pair with this phone before connecting its helper.");
                byte[] helper;
                try(java.io.InputStream input=context.getAssets().open("helper.jar")){
                    ByteArrayOutputStream bytes=new ByteArrayOutputStream();byte[] chunk=new byte[16384];int count;
                    while((count=input.read(chunk))!=-1){if(bytes.size()+count>2*1024*1024)throw new IllegalStateException("Helper asset exceeds size limit");bytes.write(chunk,0,count);}
                    helper=bytes.toByteArray();
                }
                started=NativeAdbBridge.nativeStartHelper(credentials,device,host,port,helper,digest,envelope);
                if(started==0)throw new IllegalStateException("Could not create ADB helper session");
            }catch(Exception|LinkageError error){failure=error.getMessage()==null?"ADB helper startup failed":error.getMessage();}
            finally{Arrays.fill(envelope,(byte)0);}
            long result=started;String error=failure;
            main.post(()->{
                starting=false;handle=result;
                if(error!=null){status.accept(error);finish();return;}
                if(closing)NativeAdbBridge.nativeCloseHelper(handle);
                poll();
            });
        });
    }
    private void poll(){
        if(handle==0)return;
        String events=NativeAdbBridge.nativePollHelper(handle);
        if(events!=null){
            StringBuilder progress=new StringBuilder();
            for(String line:events.split("\\n")){
                if(line.equals("SESSION_CLOSED")){
                    if(!NativeAdbBridge.nativeReleaseHelper(handle))throw new IllegalStateException("ADB helper session did not finish");
                    if(progress.length()>0&&!closing)status.accept(progress.toString());
                    handle=0;finish();return;
                }
                if(!line.isEmpty()){if(progress.length()>0)progress.append("\n");progress.append(line);}
            }
            if(progress.length()>0&&!closing)status.accept(progress.toString());
        }
        main.postDelayed(this::poll,200);
    }
    void close(Runnable completed){
        closedCallbacks.add(completed);closing=true;
        if(handle!=0)NativeAdbBridge.nativeCloseHelper(handle);
        else if(!starting)finish();
    }
    void destroy(){closed=true;close(()->worker.shutdown());}
    private void finish(){
        boolean unexpected=!closing;
        List<Runnable> callbacks=new ArrayList<>(closedCallbacks);closedCallbacks.clear();
        for(Runnable callback:callbacks)callback.run();
        if(unexpected)disconnected.run();
    }
}
