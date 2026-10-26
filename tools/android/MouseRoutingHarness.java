package io.github.ms912x.helper;

import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.Looper;
import org.json.JSONObject;

/** Real evdev -> production router -> framework event delivery on two isolated fixture displays. */
public final class MouseRoutingHarness {
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final ShellDesktop[] displays=new ShellDesktop[2];
    private final ImageReader[] readers=new ImageReader[2];
    private final DisplayInputController[] inputs=new DisplayInputController[2];
    private SharedMouseRouter router;
    private NativeMouse source;
    private boolean finished;
    private String selected;
    private int side;

    public static void main(String[] args){
        if(android.os.Process.myUid()!=2000)throw new SecurityException("Shell UID required");
        Looper.prepareMainLooper();MouseRoutingHarness harness=new MouseRoutingHarness();
        try{harness.start(args);Looper.loop();}
        catch(Throwable error){error.printStackTrace();harness.finish(1);}
    }
    private void start(String[] args) throws Exception {
        if(args.length!=2)throw new IllegalArgumentException("Expected mouse-name-fragment and side (1..4)");
        selected=null;side=Integer.parseInt(args[1]);
        for(String device:NativeMouse.devices())if(device.contains(args[0])){
            if(selected!=null)throw new IllegalArgumentException("Mouse selection is ambiguous");selected=device;
        }
        if(selected==null)throw new IllegalStateException("No matching relative mouse");
        for(int i=0;i<2;i++){
            readers[i]=ImageReader.newInstance(640,480,1,2);
            readers[i].setOnImageAvailableListener(reader->{try(Image image=reader.acquireLatestImage()){ }},handler);
            displays[i]=new ShellDesktop("ms912x-router-fixture-"+i,640,480,160,60,readers[i].getSurface());
            displays[i].home("io.github.ms912x.desktop");
            Process resume=new ProcessBuilder("/system/bin/am","resume-app-switches").start();
            resume.waitFor();
            displays[i].launch("io.github.ms912x.desktop.test/io.github.ms912x.desktop.DesktopInputFixtureActivity");
            inputs[i]=displays[i].inputController(handler,"io.github.ms912x.desktop",0,
                    new FrameworkDisplayCompat().source(displays[i].id()).layerStack,(sequence,reason)->{throw new IllegalStateException(reason);});
        }
        startRouter();
        System.out.println("READY:"+new JSONObject().put("main",displays[0].id()).put("screen",displays[1].id()).put("mouse",selected));
        Thread eof=new Thread(()->{
            try(java.io.BufferedReader reader=new java.io.BufferedReader(new java.io.InputStreamReader(System.in))){
                String line;while((line=reader.readLine())!=null){String command=line;handler.post(()->{
                    try{
                        if(command.equals("stop")){router.close();System.out.println("CLOSED");}
                        else if(command.equals("start")){startRouter();System.out.println("STARTED");}
                    }catch(Exception error){error.printStackTrace();finish(1);}
                });}
            }catch(Exception ignored){ }
            handler.post(()->finish(0));
        },"mouse-fixture-lifetime");eof.setDaemon(true);eof.start();
        handler.postDelayed(()->finish(0),120_000);
    }
    private void startRouter() throws Exception {
        if(router!=null)router.close();
        source=new NativeMouse(selected);
        MouseRouteGeometry geometry=new MouseRouteGeometry(640,480,160,640,480,160,side,320,240);
        inputs[1].suspendPointer(true);
        router=new SharedMouseRouter(source,geometry,inputs[0].mouseEndpoint(),inputs[1].mouseEndpoint(),
                ()->{},reason->System.out.println("STOPPED:"+reason));
        router.start();
    }
    private void finish(int code){
        if(finished)return;finished=true;
        if(router!=null)router.close();if(source!=null)source.close();
        for(int i=0;i<2;i++){if(inputs[i]!=null)inputs[i].close();if(displays[i]!=null)displays[i].close();if(readers[i]!=null)readers[i].close();}
        System.out.println("FINISHED:"+code);System.exit(code);
    }
}
