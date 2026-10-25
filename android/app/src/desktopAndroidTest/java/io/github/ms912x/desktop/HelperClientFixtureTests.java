package io.github.ms912x.desktop;

import android.app.Instrumentation;
import android.content.Intent;
import android.graphics.Rect;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Parcel;
import android.os.RemoteException;
import android.os.SystemClock;
import android.view.Surface;
import java.util.ArrayList;
import java.util.concurrent.Callable;
import io.github.ms912x.ipc.*;

import static io.github.ms912x.desktop.AndroidFixtures.*;
import static io.github.ms912x.desktop.ActivityFixtureTests.field;
import static io.github.ms912x.desktop.OutputFixtureTests.main;

/** Local transport faults exercise the real controller/reader; they do not simulate shell privileges. */
public final class HelperClientFixtureTests {
    private static final ScreenSource SOURCE = new ScreenSource(0,"Fixture",60,0);
    private static final Handler MAIN = new Handler(Looper.getMainLooper());

    private static final class Peer extends IDisplayHelper.Stub {
        final ArrayList<DeathRecipient> deaths = new ArrayList<>();
        final String digest;
        boolean alive=true, holdProtocol, holdSources, holdReady, releaseFailure, stopFailure, dieOnRelease;
        long releaseDelay, sourceSession, sourceRequest, protocolSession, protocolRequest;
        int attaches, releases, stops;
        IProtocolCallback protocolReply;
        IInventoryCallback sourceReply;
        IHelperCallback captureReply;
        CaptureSpec spec;
        Surface owned;
        Peer(String digest) { this.digest=digest; }
        @Override public boolean isBinderAlive() { return alive; }
        @Override public void linkToDeath(DeathRecipient listener,int flags) { deaths.add(listener); }
        @Override public boolean unlinkToDeath(DeathRecipient listener,int flags) { return deaths.remove(listener); }
        @Override public void getProtocolInfo(long session,long request,IProtocolCallback callback) throws RemoteException {
            protocolSession=session;protocolRequest=request;protocolReply=callback;
            if(!holdProtocol) protocol();
        }
        void protocol() throws RemoteException {
            protocolReply.onProtocolInfo(protocolSession,protocolRequest,new ProtocolInfo(HelperProtocol.VERSION,digest,2000,30));
        }
        @Override public void listDisplaySources(long session,long request,IInventoryCallback callback) throws RemoteException {
            sourceSession=session;sourceRequest=request;sourceReply=callback;
            if(!holdSources) sources();
        }
        void sources() throws RemoteException {
            sourceReply.onSources(new SourceListResult(sourceSession,sourceRequest,HelperProtocol.READY,"",
                    new DisplaySourceInfo[]{new DisplaySourceInfo(0,"fixture","Fixture",640,480,0,60,0)}));
        }
        Runnable savedSourceReply() {
            IInventoryCallback callback=sourceReply;
            SourceListResult result=new SourceListResult(sourceSession,sourceRequest,HelperProtocol.READY,"",
                    new DisplaySourceInfo[]{new DisplaySourceInfo(0,"fixture","Fixture",640,480,0,60,0)});
            return ()->{try{callback.onSources(result);}catch(RemoteException failure){throw new AssertionError(failure);}};
        }
        @Override public void attachCapture(CaptureSpec spec,Surface input,IHelperCallback callback) throws RemoteException {
            ++attaches;this.spec=spec;captureReply=callback;
            Parcel parcel=Parcel.obtain();
            try {input.writeToParcel(parcel,0);parcel.setDataPosition(0);owned=Surface.CREATOR.createFromParcel(parcel);}
            finally {parcel.recycle();}
            if(!holdReady) ready();
        }
        void ready() throws RemoteException {
            captureReply.onResult(new CaptureResult(spec.sessionId,spec.requestId,spec.generation,HelperProtocol.READY,
                    -1,"Fixture","Created",new DisplaySourceInfo(0,"fixture","Fixture",640,480,0,60,0),new Rect(0,0,640,480),0));
        }
        @Override public void releaseCapture(long session,long request,long generation,IHelperCallback callback) throws RemoteException {
            ++releases;
            if(releaseFailure) throw new RemoteException("Injected release transport failure");
            Runnable release=() -> {
                closeSurface();
                try {callback.onResult(new CaptureResult(session,request,generation,HelperProtocol.RELEASED,-1,"","Helper mirror released"));}
                catch(RemoteException failure){throw new AssertionError(failure);}
                if(dieOnRelease) die();
            };
            if(releaseDelay==0) release.run(); else MAIN.postDelayed(release,releaseDelay);
        }
        @Override public void launchApp(long session,long request,long generation,String component,IHelperCallback callback){}
        @Override public void sendInput(ScreenInputCommand command,IScreenInputCallback callback){}
        @Override public void heartbeat(long session) { }
        @Override public void stopSession(long session) throws RemoteException {
            ++stops;
            if(stopFailure) throw new RemoteException("Injected stop transport failure");
            die();
        }
        private void closeSurface(){if(owned!=null){owned.release();owned=null;}}
        void die(){
            if(!alive)return;
            alive=false;closeSurface();
            for(DeathRecipient listener:new ArrayList<>(deaths))listener.binderDied();
        }
    }

    private static final class Fixture implements AutoCloseable {
        final Instrumentation test;
        final HelperCaptureSource controller;
        final MainScreenView screen;
        final Peer peer;
        Fixture(Instrumentation test,MainActivity activity,long requestTimeout,long frameTimeout,boolean holdProtocol) throws Exception {
            this.test=test;
            screen=ActivityFixtureTests.screen(activity);
            String digest;
            try(java.io.InputStream input=test.getTargetContext().getAssets().open("helper.sha256")){
                byte[] bytes=new byte[65];int count=input.read(bytes);
                digest=new String(bytes,0,count,java.nio.charset.StandardCharsets.US_ASCII).trim();
            }
            peer=new Peer(digest);peer.holdProtocol=holdProtocol;
            controller=main(test,()->new HelperCaptureSource(activity,screen,android.os.Process.myUid(),requestTimeout,frameTimeout));
            main(test,()->{
                controller.prepare();
                Bundle envelope=HelperEnrollment.call(activity,2000,"claimDebug",null);
                envelope.putBinder("helper",peer);
                HelperEnrollment.call(activity,2000,"enroll",envelope);
                return null;
            });
            if(!holdProtocol) waitFor(()->screen.helperPreviewButton.isEnabled());
        }
        void waitFor(Callable<Boolean> condition) throws Exception {
            long deadline=SystemClock.elapsedRealtime()+4000;
            while(!main(test,condition)){
                require(SystemClock.elapsedRealtime()<deadline,"Helper controller fixture timed out");
                Thread.sleep(10);
            }
        }
        void preview() throws Exception {main(test,()->{controller.preview(SOURCE,mode640(),0);return null;});}
        void stop() throws Exception {main(test,()->{controller.stop();return null;});}
        void idle() throws Exception {waitFor(()->!controller.isActive());}
        @Override public void close() throws Exception {
            main(test,()->{peer.die();controller.close();return null;});
            idle();
            test.waitForIdleSync();
        }
    }

    public static void run(Instrumentation test) throws Exception {
        MainActivity activity=(MainActivity)test.startActivitySync(new Intent(test.getTargetContext(),MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        try {
            try(Fixture f=new Fixture(test,activity,500,10000,false)){
                main(test,()->{f.peer.holdSources=true;return null;});
                f.preview();
                Runnable oldReply=main(test,()->f.peer.savedSourceReply());
                f.stop();f.preview();
                main(test,()->{oldReply.run();return null;});test.waitForIdleSync();
                require(main(test,()->f.peer.attaches==0),"Old inventory completed a newer start");
                main(test,()->{f.peer.sources();return null;});f.waitFor(()->f.peer.attaches==1);f.stop();f.idle();
            }
            try(Fixture f=new Fixture(test,activity,500,10000,false)){
                main(test,()->{f.peer.holdReady=true;return null;});f.preview();
                f.waitFor(()->f.peer.attaches==1);f.stop();
                main(test,()->{f.peer.ready();return null;});f.idle();
                require(main(test,()->f.screen.helperPreviewButton.isEnabled()),"Late READY revived a stopped capture");
            }
            try(Fixture f=new Fixture(test,activity,500,10000,false)){
                f.preview();f.waitFor(()->f.peer.attaches==1);
                main(test,()->{f.peer.releaseFailure=true;f.peer.stopFailure=true;return null;});f.stop();
                Thread.sleep(150);
                require(main(test,()->f.peer.alive&&f.controller.isActive()&&f.peer.owned!=null),
                        "Transport failure was treated as confirmed producer death");
                main(test,()->{f.peer.die();return null;});f.idle();
            }
            try(Fixture f=new Fixture(test,activity,500,10000,false)){
                f.preview();f.waitFor(()->f.peer.attaches==1);
                long firstStop=SystemClock.elapsedRealtime();f.stop();f.idle();
                f.preview();f.waitFor(()->f.peer.attaches==2);
                long delay=firstStop+400-SystemClock.elapsedRealtime();if(delay>0)Thread.sleep(delay);
                main(test,()->{f.peer.releaseDelay=200;return null;});f.stop();f.idle();
                require(main(test,()->f.peer.alive&&f.peer.stops==0),"A prior release timeout aborted a newer stop");
            }
            try(Fixture f=new Fixture(test,activity,500,10000,false)){
                f.preview();f.waitFor(()->f.peer.attaches==1);
                long generation=main(test,()->(Long)field(f.controller,"generation"));
                main(test,()->{f.peer.dieOnRelease=true;return null;});f.stop();f.idle();
                require(main(test,()->(Long)field(f.controller,"generation")==generation+1),"Release/death scheduled reader cleanup twice");
            }
            try(Fixture f=new Fixture(test,activity,500,80,false)){
                f.preview();f.waitFor(()->f.peer.attaches==1);f.idle();
                require(main(test,()->f.peer.releases==1&&f.peer.alive),"Missing first image failed to release the mirror");
            }
            try(Fixture f=new Fixture(test,activity,80,10000,true)){
                f.waitFor(()->!f.peer.alive);f.idle();
                main(test,()->{f.peer.protocol();return null;});test.waitForIdleSync();
                require(main(test,()->!f.screen.helperPreviewButton.isEnabled()),"Late protocol reply reconnected an expired helper");
            }
            try(Fixture f=new Fixture(test,activity,80,10000,false)){
                main(test,()->{f.peer.holdSources=true;return null;});f.preview();
                f.waitFor(()->!f.peer.alive);f.idle();
                main(test,()->{f.peer.sources();return null;});test.waitForIdleSync();
                require(main(test,()->f.peer.attaches==0),"Late inventory after timeout created a capture");
            }
        } finally {main(test,()->{activity.finish();return null;});}
    }
}
