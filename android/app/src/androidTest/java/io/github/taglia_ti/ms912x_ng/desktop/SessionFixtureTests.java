package io.github.taglia_ti.ms912x_ng.desktop;

import android.app.Instrumentation;
import android.graphics.Canvas;
import android.graphics.Color;
import android.os.Handler;
import android.os.Looper;
import java.util.ArrayList;
import java.util.List;
import static io.github.taglia_ti.ms912x_ng.desktop.AndroidFixtures.*;
import static io.github.taglia_ti.ms912x_ng.desktop.OutputFixtureTests.main;

/** Exercises the coordinator's ownership barrier with real readers and fixture USB. */
final class SessionFixtureTests {
    private static class Producer implements CaptureSource {
        CaptureController capture;
        Listener listener;
        Runnable stopped;
        int closes;
        public void start(Request request,Listener listener){
            this.listener=listener;
            capture=new CaptureController(request.mode.width,request.mode.height,request.generation,status->{
                if(status==0)new Handler(Looper.getMainLooper()).post(()->listener.onReady(capture));
            });
        }
        void frame(){Canvas canvas=capture.surface().lockCanvas(null);canvas.drawColor(Color.GREEN);capture.surface().unlockCanvasAndPost(canvas);}
        public void stopProducer(Runnable stopped){this.stopped=stopped;}
        public void releaseReader(Runnable released){++closes;CaptureCleanup.close(capture,released);}
        public CaptureController capture(){return capture;}
        public String status(){return "Fixture source";}
    }
    private static final class Peer extends Producer implements HelperBackend {
        Runnable retired;
        public void prepare(){}
        public void refreshInventory(){}
        public boolean connected(){return true;}
        public boolean isActive(){return capture!=null&&capture.isOpen();}
        public java.util.List<SourceCapability> sources(){return java.util.Collections.singletonList(new SourceCapability(7,"fixture-source","Fixture",640,480,0,true,""));}
        public void setObserver(Runnable observer){}
        public void shutdown(Runnable completed){retired=completed;}
        public void close(){}
    }
    static void run(Instrumentation test) throws Exception {
        Host host=new Host(new Connection(edid(mode640())));host.devices.add(adapter());
        List<Producer> producers=new ArrayList<>();int[] foreground={0,0};
        SessionCoordinator owner=main(test,()->new SessionCoordinator(test.getTargetContext(),new SessionCoordinator.ForegroundOwner(){
            public void begin(){++foreground[0];}public void end(){++foreground[1];}
        },host,ShellCaptureFactory.createBackend(test.getTargetContext()),id->{Producer producer=new Producer();producers.add(producer);return producer;}));
        try{
            main(test,()->{owner.probe();return null;});
            CaptureFixtureTests.waitFor(()->!owner.usb().isBusy(),"Coordinator EDID probe");
            main(test,()->{
                host.permission=false;
                require(!owner.start(ScreenSource.TEST_SCREEN,"",mode640(),0,160,true,false)&&foreground[0]==0,"Permission precedes foreground promotion");
                host.permission=true;
                require(owner.start(ScreenSource.TEST_SCREEN,"",mode640(),0,160,true,false),"Output starts with validated mode and permission");
                require(foreground[0]==1&&!owner.usb().isBusy(),"No USB worker before the first accepted image");
                require(!owner.start(ScreenSource.TEST_SCREEN,"",mode640(),0,160,true,false),"One active session authority");
                owner.stop("Cancelled before first image");
                require(owner.snapshot().phase==SessionCoordinator.Phase.STOPPING,"Cancellation enters cleanup");
                require(producers.get(0).closes==0&&producers.get(0).capture.isOpen(),"Reader retained until producer confirmation");
                return null;
            });
            test.waitForIdleSync();
            main(test,()->{
                require(producers.get(0).closes==0&&foreground[1]==0,"USB drain alone cannot close the reader or foreground role");
                producers.get(0).stopped.run();return null;
            });
            CaptureFixtureTests.waitFor(()->!owner.active(),"Cancelled source cleanup");
            require(producers.get(0).closes==1&&!producers.get(0).capture.isOpen()&&foreground[1]==1,"Reader and foreground released once");
            main(test,()->{
                require(owner.start(ScreenSource.TEST_SCREEN,"",mode640(),0,160,true,false),"Replacement session starts after cleanup");
                producers.get(0).listener.onReady(producers.get(0).capture);
                require(!owner.usb().isBusy(),"Late callback cannot start replacement USB");
                producers.get(1).frame();return null;
            });
            CaptureFixtureTests.waitFor(()->owner.snapshot().phase==SessionCoordinator.Phase.STREAMING,"First accepted image and completed USB batch activate output");
            require(host.connection.bulkBytes>0,"Native encoded payload traversed fixture USB");
            main(test,()->{
                owner.observe(state->{});owner.observe(null);
                require(owner.active()&&producers.get(1).capture.isOpen(),"UI observer detachment does not stop the session");
                owner.stop("Fixture output failure");owner.stop("Duplicate stop");
                producers.get(1).listener.onEnded("Concurrent source failure");
                require(producers.get(1).closes==0,"Reader stays open while teardown is pending");
                producers.get(1).stopped.run();return null;
            });
            CaptureFixtureTests.waitFor(()->!owner.active(),"Output cleanup barrier");
            require(producers.get(1).closes==1&&foreground[0]==2&&foreground[1]==2,"Concurrent stop paths release resources once");
            require(owner.snapshot().status.equals("Fixture output failure"),"Terminal failure survives cleanup");
        }finally{main(test,()->{owner.close();return null;});}
        Peer peer=new Peer();
        Host helperHost=new Host(new Connection(edid(mode640())));helperHost.devices.add(adapter());
        int[] ended={0};
        SessionCoordinator helperOwner=main(test,()->new SessionCoordinator(test.getTargetContext(),new SessionCoordinator.ForegroundOwner(){
            public void begin(){} public void end(){++ended[0];}
        },helperHost,peer,null));
        try{
            main(test,()->{helperOwner.probe();return null;});
            CaptureFixtureTests.waitFor(()->!helperOwner.usb().isBusy(),"Helper coordinator EDID probe");
            main(test,()->{
                require(!helperOwner.start(7,"reused-identity",mode640(),0,160,true,true),"Reused display ID is ineligible");
                require(helperOwner.start(7,"fixture-source",mode640(),0,160,true,true),"Validated helper source starts");
                peer.frame();return null;
            });
            CaptureFixtureTests.waitFor(()->helperOwner.snapshot().phase==SessionCoordinator.Phase.STREAMING,"Helper USB fixture active");
            main(test,()->{helperOwner.stop("Stop helper output");peer.stopped.run();return null;});
            CaptureFixtureTests.waitFor(()->peer.retired!=null,"Reader drained before helper retirement");
            require(peer.closes==1&&!peer.capture.isOpen()&&ended[0]==0&&helperOwner.active(),"Foreground ownership waits for confirmed helper retirement");
            main(test,()->{peer.retired.run();return null;});
            require(!helperOwner.active()&&ended[0]==1,"Helper retirement completes foreground teardown exactly once");
        }finally{main(test,()->{helperOwner.close();return null;});}
    }
}
