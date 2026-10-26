package io.github.ms912x.helper;

import android.graphics.PointF;
import android.os.Looper;
import android.os.Process;
import io.github.ms912x.ipc.DisplaySourceInfo;
import org.json.JSONObject;

/** Controlled phone probes; leave deliberately kills only this probe to test recovery. */
public final class MouseRecoveryHarness {
    public static void main(String[] args){
        try{
            if(Process.myUid()!=2000||args.length!=1)throw new IllegalArgumentException("Expected shell UID and one probe mode");
            Looper.prepareMainLooper();MouseScalingRecovery.recover();
            FrameworkMouseCompat api=new FrameworkMouseCompat();FrameworkDisplayCompat displays=new FrameworkDisplayCompat();
            DisplaySourceInfo main;
            if(args[0].equals("park")){
                DisplaySourceInfo target;try{target=displays.mouseDesktop();}catch(IllegalStateException absent){target=displays.source(0);}
                main=target;
            }else main=displays.mouseDesktop();
            if(args[0].equals("recover")){System.out.println("RECOVERED");System.exit(0);}
            if(args[0].equals("leave")){
                new MouseScalingLease(api).disable(main.displayId);
                System.out.println("RECOVERY_ARMED:"+main.displayId);System.out.flush();Process.killProcess(Process.myPid());return;
            }
            String[] devices=NativeMouse.devices();if(devices.length!=1)throw new IllegalStateException("Probe requires exactly one physical mouse");
            if(args[0].equals("park")){
                for(int side=1;side<=4;side++){
                    try(NativeMouse mouse=new NativeMouse(devices[0]);MouseScalingLease scaling=new MouseScalingLease(api)){
                        Thread.sleep(250);scaling.disable(main.displayId);
                        DefaultMouseEndpoint endpoint=DefaultMouseEndpoint.calibrate(mouse,api,main.displayId,main.width,main.height,scaling);
                        endpoint.layout(side);endpoint.leave();Thread.sleep(60);
                        PointF parked=api.position(main.displayId);
                        float expectedX=side==1?0:main.width-1,expectedY=side==3?0:main.height-1;
                        if(Math.abs(parked.x-expectedX)>.1||Math.abs(parked.y-expectedY)>.1)throw new IllegalStateException("Wrong parking corner: "+parked);
                        float returnX=side==1?0:side==2?main.width-1:main.width*.25f;
                        float returnY=side==3?0:side==4?main.height-1:main.height*.25f;
                        endpoint.enterAt(returnX,returnY);Thread.sleep(60);PointF returned=api.position(main.displayId);
                        if(Math.abs(returned.x-returnX)>endpoint.gainX()||Math.abs(returned.y-returnY)>endpoint.gainY())throw new IllegalStateException("Wrong edge return: "+returned);
                        endpoint.leave();Thread.sleep(30);mouse.releaseGrab();endpoint.close();Thread.sleep(60);
                        PointF stopped=api.position(main.displayId);
                        if(Math.abs(stopped.x-returned.x)>endpoint.gainX()||Math.abs(stopped.y-returned.y)>endpoint.gainY())throw new IllegalStateException("Stop did not restore parked cursor: "+stopped);
                        System.out.println(new JSONObject().put("side",side).put("parked",parked.toString()).put("returned",returned.toString()).put("stopped",stopped.toString()));
                    }
                }
                System.out.println("PASS native parking, edge return and stop restoration on all four sides");System.exit(0);return;
            }
            try(NativeMouse mouse=new NativeMouse(devices[0]);MouseScalingLease scaling=new MouseScalingLease(api)){
                Thread.sleep(250);
                if(args[0].equals("edge")){
                    mouse.emit(32768,0,0,0,0);Thread.sleep(80);
                    PointF position=api.position(main.displayId);
                    if(Math.abs(position.x-(main.width-1))>.1)throw new IllegalStateException("Cursor did not reach right edge");
                    System.out.println("EDGE:"+position);
                }else if(args[0].equals("calibrate")){
                    scaling.disable(main.displayId);
                    DefaultMouseEndpoint endpoint=DefaultMouseEndpoint.calibrate(mouse,api,main.displayId,main.width,main.height,scaling);
                    PointF before=endpoint.position();float x=before.x-12*endpoint.gainX(),y=before.y-7*endpoint.gainY();
                    endpoint.moveTo(x,y);Thread.sleep(50);PointF actual=api.position(main.displayId);
                    if(Math.abs(actual.x-x)>1.5||Math.abs(actual.y-y)>1.5)throw new IllegalStateException("Calibrated mapping mismatch");
                    System.out.println(new JSONObject().put("gainX",endpoint.gainX()).put("gainY",endpoint.gainY())
                            .put("expectedX",x).put("expectedY",y).put("actualX",actual.x).put("actualY",actual.y));
                    endpoint.moveTo(before.x,before.y);Thread.sleep(50);
                }else throw new IllegalArgumentException("Expected calibrate, edge, leave or recover");
            }
            System.out.println("PASS");System.exit(0);
        }catch(Throwable failure){failure.printStackTrace();System.exit(1);}
    }
}
