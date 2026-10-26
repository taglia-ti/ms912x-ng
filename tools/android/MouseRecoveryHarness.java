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
            FrameworkMouseCompat api=new FrameworkMouseCompat();DisplaySourceInfo main=new FrameworkDisplayCompat().mouseDesktop();
            if(args[0].equals("recover")){System.out.println("RECOVERED");System.exit(0);}
            if(args[0].equals("leave")){
                new MouseScalingLease(api).disable(main.displayId);
                System.out.println("RECOVERY_ARMED:"+main.displayId);System.out.flush();Process.killProcess(Process.myPid());return;
            }
            String[] devices=NativeMouse.devices();if(devices.length!=1)throw new IllegalStateException("Probe requires exactly one physical mouse");
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
