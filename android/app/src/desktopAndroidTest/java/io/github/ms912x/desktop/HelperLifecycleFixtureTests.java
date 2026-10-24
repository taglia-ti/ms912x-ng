package io.github.ms912x.desktop;

import android.app.Instrumentation;
import android.graphics.PixelFormat;
import android.media.Image;
import android.media.ImageReader;
import android.os.Binder;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.SystemClock;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.json.JSONArray;
import org.json.JSONObject;
import io.github.ms912x.ipc.*;

/** Optional real-UID-2000 acceptance. The host closes only this test's helper/app resources. */
public final class HelperLifecycleFixtureTests {
    private static void check(boolean value,String message){if(!value)throw new AssertionError(message);}
    private static void write(Instrumentation test,String name,JSONObject json) throws Exception {
        try(java.io.FileOutputStream file=test.getTargetContext().openFileOutput(name,0)) {
            file.write(json.toString(2).getBytes(StandardCharsets.UTF_8));
        }
    }
    private static final class Sink implements AutoCloseable {
        final ImageReader reader=ImageReader.newInstance(640,480,PixelFormat.RGBA_8888,3);
        final ArrayBlockingQueue<CaptureResult> replies=new ArrayBlockingQueue<>(32);
        final AtomicInteger images=new AtomicInteger();
        final Handler handler;
        final IHelperCallback callback=new IHelperCallback.Stub(){
            @Override public void onResult(CaptureResult result){
                if(Binder.getCallingUid()!=2000)throw new SecurityException("Callback UID");
                replies.offer(result);
            }
        };
        Sink(Handler handler){
            this.handler=handler;
            reader.setOnImageAvailableListener(r->{
                Image image=r.acquireLatestImage();
                if(image!=null){images.incrementAndGet();image.close();}
            },handler);
        }
        CaptureResult await(long request,int...codes) throws Exception {
            long deadline=SystemClock.elapsedRealtime()+10000;
            while(SystemClock.elapsedRealtime()<deadline){
                CaptureResult result=replies.poll(1,TimeUnit.SECONDS);
                if(result==null||result.requestId!=request)continue;
                for(int code:codes)if(result.code==code)return result;
                throw new AssertionError("Unexpected result "+result.code+": "+result.detail);
            }
            throw new AssertionError("Timed out waiting for request "+request);
        }
        void imageAfter(int previous) throws Exception {
            long deadline=SystemClock.elapsedRealtime()+5000;
            while(images.get()<=previous){check(SystemClock.elapsedRealtime()<deadline,"No new source image");Thread.sleep(10);}
        }
        @Override public void close() throws Exception {
            CountDownLatch closed=new CountDownLatch(1);
            handler.post(()->{reader.setOnImageAvailableListener(null,null);reader.close();closed.countDown();});
            check(closed.await(5,TimeUnit.SECONDS),"Reader close timed out");
        }
    }
    private static CaptureSpec spec(long session,long request,long generation,DisplaySourceInfo source){
        return new CaptureSpec(session,request,generation,source.displayId,source.identity,640,480,0,0);
    }
    private static void release(IDisplayHelper helper,long session,long request,long generation,Sink sink) throws Exception {
        helper.releaseCapture(session,request,generation,sink.callback);
        check(sink.await(request,HelperProtocol.RELEASED).generation==generation,"Release generation mismatch");
    }

    public static void run(Instrumentation test,Bundle arguments) throws Exception {
        String mode=arguments.getString("termination","watchdog");
        String digest;
        try(java.io.InputStream input=test.getTargetContext().getAssets().open("helper.sha256")){
            byte[] bytes=new byte[65];int count=input.read(bytes);digest=new String(bytes,0,count,StandardCharsets.US_ASCII).trim();
        }
        AtomicReference<IDisplayHelper> peer=new AtomicReference<>();
        AtomicReference<IBinder> owner=new AtomicReference<>();
        CountDownLatch enrolled=new CountDownLatch(1),dead=new CountDownLatch(1);
        long session=HelperEnrollment.prepare(test.getTargetContext(),digest,(id,binder,life)->{
            peer.set(IDisplayHelper.Stub.asInterface(binder));owner.set(life);enrolled.countDown();
        });
        write(test,"helper-lifecycle-ready.json",new JSONObject().put("ready",true));
        HandlerThread frames=new HandlerThread("helper-lifecycle-images");frames.start();
        ScheduledExecutorService timer=Executors.newSingleThreadScheduledExecutor();
        JSONObject report=new JSONObject().put("passed",false).put("termination",mode).put("checks",new JSONArray());
        Sink active=null;
        ArrayList<Sink> sinks=new ArrayList<>();
        try {
            check(enrolled.await(30,TimeUnit.SECONDS),"Helper enrollment timed out");
            IDisplayHelper helper=peer.get();helper.asBinder().linkToDeath(dead::countDown,0);
            ProtocolInfo protocol=HelperTestQueries.protocol(helper,session);
            check(protocol.uid==2000&&protocol.version==HelperProtocol.VERSION&&digest.equals(protocol.buildDigest),"Helper identity mismatch");
            ScheduledFuture<?> heartbeat=timer.scheduleWithFixedDelay(()->{
                try{helper.heartbeat(session);}catch(Exception ignored){/* Confirmed Binder death owns cleanup. */}
            },0,2,TimeUnit.SECONDS);
            int id=Integer.parseInt(arguments.getString("sourceId"));
            DisplaySourceInfo source=null;
            for(DisplaySourceInfo candidate:HelperTestQueries.sources(helper,session))if(candidate.displayId==id)source=candidate;
            check(source!=null,"Test source unavailable");
            report.put("source_id",id).put("source_identity",source.identity).put("helper_uid",protocol.uid);
            Handler handler=new Handler(frames.getLooper());
            long request=0,generation=0;
            if(mode.equals("watchdog")){
                { Sink sink=new Sink(handler);sinks.add(sink);
                    ++generation;long attach=++request;
                    CaptureSpec missing=new CaptureSpec(session,attach,generation,Integer.MAX_VALUE,"missing",640,480,0,0);
                    helper.attachCapture(missing,sink.reader.getSurface(),sink.callback);
                    sink.await(attach,HelperProtocol.FAILED);release(helper,session,++request,generation,sink);
                    report.getJSONArray("checks").put("missing display and release acknowledgement");
                }
                { Sink sink=new Sink(handler);sinks.add(sink);
                    ++generation;long attach=++request;
                    helper.attachCapture(spec(session,attach,generation,source),null,sink.callback);
                    sink.await(attach,HelperProtocol.FAILED);release(helper,session,++request,generation,sink);
                    report.getJSONArray("checks").put("null Surface rejection and release acknowledgement");
                }
                { Sink sink=new Sink(handler);sinks.add(sink);
                    ++generation;long attach=++request;
                    CaptureSpec invalid=new CaptureSpec(session,attach,generation,id,source.identity,0,480,0,0);
                    helper.attachCapture(invalid,sink.reader.getSurface(),sink.callback);
                    sink.await(attach,HelperProtocol.FAILED);release(helper,session,++request,generation,sink);
                    report.getJSONArray("checks").put("invalid geometry rejection and release acknowledgement");
                }
                { Sink sink=new Sink(handler);sinks.add(sink);
                    ++generation;long attach=++request;
                    CaptureSpec cancelled=spec(session,attach,generation,source);
                    helper.attachCapture(cancelled,sink.reader.getSurface(),sink.callback);
                    release(helper,session,++request,generation,sink);
                    helper.attachCapture(cancelled,sink.reader.getSurface(),sink.callback);
                    sink.await(attach,HelperProtocol.CANCELLED);
                    report.getJSONArray("checks").put("cancel pending attach and reject stale replay");
                }
                { Sink sink=new Sink(handler);sinks.add(sink);
                    ++generation;long attach=++request;
                    helper.attachCapture(spec(session,attach,generation,source),sink.reader.getSurface(),sink.callback);
                    sink.await(attach,HelperProtocol.READY);sink.imageAfter(0);
                    release(helper,session,++request,generation-1,sink);
                    sink.imageAfter(sink.images.get());
                    report.getJSONArray("checks").put("old generation release preserves new capture");
                    long stop=++request;
                    release(helper,session,stop,generation,sink);
                    release(helper,session,stop,generation,sink);
                    report.getJSONArray("checks").put("duplicate stop acknowledged safely");
                }
            }
            active=new Sink(handler);
            sinks.add(active);
            long attach=++request;++generation;
            helper.attachCapture(spec(session,attach,generation,source),active.reader.getSurface(),active.callback);
            active.await(attach,HelperProtocol.READY);active.imageAfter(0);
            long started=SystemClock.elapsedRealtime();
            write(test,"helper-lifecycle-active.json",new JSONObject().put("active",true).put("termination",mode)
                    .put("source_identity",source.identity).put("accepted_images",active.images.get()));
            if(mode.equals("watchdog"))heartbeat.cancel(false);
            check(dead.await(HelperProtocol.WATCHDOG_MS+HelperProtocol.STOP_GRACE_MS+5000,TimeUnit.MILLISECONDS),"Helper failed to exit");
            report.put("exit_elapsed_ms",SystemClock.elapsedRealtime()-started).put("accepted_images",active.images.get());
            report.getJSONArray("checks").put("confirmed helper death before reader close");
            boolean found=false;
            for(android.view.Display display:test.getTargetContext().getSystemService(android.hardware.display.DisplayManager.class).getDisplays())
                if(display.getDisplayId()==id)found=true;
            check(found,"Original source display disappeared");
            report.put("passed",true);
        } catch(Throwable failure){report.put("failure",failure.toString());throw failure;}
        finally{
            HelperEnrollment.cancel(session);timer.shutdownNow();
            if(peer.get()!=null&&dead.getCount()!=0){
                try{peer.get().stopSession(session);}catch(Exception ignored){/* Await actual death below. */}
                check(dead.await(15,TimeUnit.SECONDS),"Cannot close reader while helper is alive");
            }
            for(Sink sink:sinks)sink.close();
            owner.set(null);frames.quitSafely();write(test,"helper-lifecycle.json",report);
        }
    }
}
