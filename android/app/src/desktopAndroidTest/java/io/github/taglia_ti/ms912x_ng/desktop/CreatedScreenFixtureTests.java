package io.github.taglia_ti.ms912x_ng.desktop;

import android.app.ActivityOptions;
import android.app.Instrumentation;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.SystemClock;
import java.nio.charset.StandardCharsets;
import org.json.JSONObject;
import static io.github.taglia_ti.ms912x_ng.desktop.AndroidFixtures.require;
import static io.github.taglia_ti.ms912x_ng.desktop.OutputFixtureTests.main;

/** Real shell-owned creation on API 30+, without a USB fixture or a host-created source. */
public final class CreatedScreenFixtureTests {
    private static JSONObject inputNode(android.view.accessibility.AccessibilityNodeInfo node) throws Exception {
        if(node==null)return null;
        CharSequence description=node.getContentDescription();
        if(description!=null&&description.toString().startsWith("INPUT:"))return new JSONObject(description.toString().substring(6));
        for(int i=0;i<node.getChildCount();i++){
            JSONObject found=inputNode(node.getChild(i));if(found!=null)return found;
        }
        return null;
    }
    static JSONObject inputState(Instrumentation test,int display) throws Exception {
        java.util.List<android.view.accessibility.AccessibilityWindowInfo> windows=test.getUiAutomation().getWindowsOnAllDisplays().get(display);
        if(windows!=null)for(android.view.accessibility.AccessibilityWindowInfo window:windows){
            JSONObject found=inputNode(window.getRoot());if(found!=null)return found;
        }
        return new JSONObject();
    }
    private static String layers(Instrumentation test) throws Exception {
        try(java.io.InputStream stream=new android.os.ParcelFileDescriptor.AutoCloseInputStream(
                test.getUiAutomation().executeShellCommand("dumpsys SurfaceFlinger --list"));
                java.io.ByteArrayOutputStream output=new java.io.ByteArrayOutputStream()){
            byte[] chunk=new byte[8192];int count;
            while((count=stream.read(chunk))!=-1)output.write(chunk,0,count);
            return output.toString("UTF-8");
        }
    }
    public static void run(Instrumentation test,Bundle arguments) throws Exception {
        ActivityOptions phone=ActivityOptions.makeBasic();phone.setLaunchDisplayId(0);
        MainActivity activity=(MainActivity)test.startActivitySync(new Intent(test.getTargetContext(),MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),phone.toBundle());
        ActivityFixtureTests.awaitService(test,activity);
        SessionCoordinator owner=main(test,()->ActivityFixtureTests.screencast(activity).coordinator());
        int id=-1;
        try{
            main(test,()->{owner.prepareHelper();return null;});
            try(java.io.FileOutputStream file=test.getTargetContext().openFileOutput("created-screen-ready.json",0)){
                file.write("{\"ready\":true}".getBytes(StandardCharsets.US_ASCII));
            }
            long deadline=SystemClock.elapsedRealtime()+60_000;
            while(!main(test,()->owner.snapshot().helperConnected)){
                require(SystemClock.elapsedRealtime()<deadline,"Connect real shell helper for created-screen test");Thread.sleep(100);
            }
            main(test,()->{
                ActivityFixtureTests.select(activity,ScreenSource.CREATE_NEW);
                ActivityFixtureTests.screencast(activity).startSession(false);return null;
            });
            CaptureFixtureTests.waitFor(()->owner.snapshot().phase==SessionCoordinator.Phase.STREAMING||!owner.active(),"Created screen first native image");
            require(owner.snapshot().phase==SessionCoordinator.Phase.STREAMING,"Create failed: "+owner.snapshot().status);
            id=owner.snapshot().createdDisplayId;require(id>0,"New screen has an independent display ID");
            require(layers(test).contains("MS912x HDMI pointer"),"Created screen owns a compositor cursor layer");
            CaptureController capture=main(test,()->owner.source().capture());long before=capture.frameVersion();
            main(test,()->{owner.launchApp(test.getTargetContext().getPackageName()+".test/io.github.taglia_ti.ms912x_ng.desktop.DesktopGeometryFixtureActivity");return null;});
            CaptureFixtureTests.waitFor(()->owner.snapshot().launchStatus.contains("App launched"),"Typed app launch on the owned screen");
            CaptureFixtureTests.waitFor(()->capture.frameVersion()>before+1,"Created screen app continues producing native images");
            main(test,()->{owner.stop("Created-screen fixture stopped.");return null;});
            CaptureFixtureTests.waitFor(()->!owner.active(),"Created-screen reader and display cleanup");
            require(!capture.isOpen()&&test.getTargetContext().getSystemService(DisplayManager.class).getDisplay(id)==null,"Stop closes the reader and removes the created desktop");
            CaptureFixtureTests.waitFor(()->{
                try{return !layers(test).contains("MS912x HDMI pointer");}
                catch(Exception failure){throw new AssertionError(failure);}
            },"Cursor Surface and compositor layer are released with the desktop");
            android.accessibilityservice.AccessibilityServiceInfo accessibility=test.getUiAutomation().getServiceInfo();
            accessibility.flags|=android.accessibilityservice.AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS;
            test.getUiAutomation().setServiceInfo(accessibility);
            for(int rotation=0;rotation<4;rotation++){
                final int angle=rotation;
                main(test,()->{require(owner.start(ScreenSource.CREATE_NEW,"",AndroidFixtures.mode640(),angle,160,false,true),"Start rotated desktop");return null;});
                CaptureFixtureTests.waitFor(()->owner.snapshot().phase==SessionCoordinator.Phase.STREAMING,"Rotated desktop frame");
                int display=owner.snapshot().createdDisplayId;
                main(test,()->{owner.launchApp(test.getTargetContext().getPackageName()+".test/io.github.taglia_ti.ms912x_ng.desktop.DesktopInputFixtureActivity");return null;});
                CaptureFixtureTests.waitFor(()->{
                    try{JSONObject state=inputState(test,display);return state.optInt("width")>0;}catch(Exception failure){throw new AssertionError(failure);}
                },"Separate-UID mouse receiver on rotated desktop");
                Thread.sleep(300);
                JSONObject window=inputState(test,display);
                Bundle progress=new Bundle();progress.putString("stream","INPUT rotation="+rotation+" display="+display+" window="+window
                        +" supported="+owner.snapshot().inputSupported+" "+owner.snapshot().inputStatus+"\n");test.sendStatus(0,progress);
                double sourceX=Math.max(1,Math.min(638,window.getInt("left")+window.getInt("width")/2.0))-320;
                double sourceY=Math.max(1,Math.min(478,window.getInt("top")+window.getInt("height")/2.0))-240;
                double fit=rotation%2==0?1:.75;
                float outputX=(float)((rotation==1?-sourceY:rotation==2?-sourceX:rotation==3?sourceY:sourceX)*fit);
                float outputY=(float)((rotation==1?sourceX:rotation==2?-sourceY:rotation==3?-sourceX:sourceY)*fit);
                main(test,()->{owner.sendInput(TouchpadInput.MOVE,outputX,outputY,0);return null;});
                main(test,()->{owner.sendInput(TouchpadInput.DRAG_START,0,0,0);return null;});
                Thread.sleep(300);
                progress.putString("stream","AFTER DOWN "+inputState(test,display)+" supported="+owner.snapshot().inputSupported+" "+owner.snapshot().inputStatus+"\n");test.sendStatus(0,progress);
                CaptureFixtureTests.waitFor(()->{
                    try{return inputState(test,display).optInt("downs")==1;}catch(Exception failure){throw new AssertionError(failure);}
                },"Mouse button delivered");
                Thread.sleep(1100);
                main(test,()->{owner.sendInput(TouchpadInput.MOVE,12,8,0);return null;});
                CaptureFixtureTests.waitFor(()->{
                    try{return inputState(test,display).optInt("moves")>0;}catch(Exception failure){throw new AssertionError(failure);}
                },"Held mouse move delivered");
                JSONObject held=inputState(test,display);
                require(held.getInt("ups")==0&&held.getInt("buttons")==android.view.MotionEvent.BUTTON_PRIMARY
                        &&held.getLong("eventDownTime")==held.getLong("downTime"),"Mouse remains down with stable downTime after one second");
                double scale=rotation%2==0?1:4.0/3;
                double expectedX=rotation==1?8*scale:rotation==2?-12:rotation==3?-8*scale:12;
                double expectedY=rotation==1?-12*scale:rotation==2?-8:rotation==3?12*scale:8;
                require(Math.abs(held.getDouble("dx")-expectedX)<1&&Math.abs(held.getDouble("dy")-expectedY)<1,
                        "Delivered pointer displacement uses inverse rotation "+rotation+": "+held);
                main(test,()->{owner.sendInput(TouchpadInput.DRAG_END,0,0,0);return null;});
                CaptureFixtureTests.waitFor(()->{
                    try{return inputState(test,display).optInt("ups")==1;}catch(Exception failure){throw new AssertionError(failure);}
                },"Mouse release delivered exactly once");
                main(test,()->{owner.stop("Rotated input tested.");return null;});
                CaptureFixtureTests.waitFor(()->!owner.active(),"Rotated desktop cleanup");
            }
            try(java.io.FileOutputStream file=test.getTargetContext().openFileOutput("created-screen-result.json",0)){
                file.write(new JSONObject().put("passed",true).put("created_display_id",id).put("frames",capture.frameVersion())
                        .toString(2).getBytes(StandardCharsets.UTF_8));
            }
        }finally{main(test,()->{if(owner.active())owner.stop("Created-screen fixture finished.");activity.finish();return null;});}
    }
}
