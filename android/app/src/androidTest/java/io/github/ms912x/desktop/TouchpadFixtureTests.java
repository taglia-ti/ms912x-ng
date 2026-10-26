package io.github.ms912x.desktop;

import android.app.Instrumentation;
import android.content.Intent;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.ViewConfiguration;
import android.widget.Button;
import android.widget.LinearLayout;
import java.util.ArrayList;
import java.util.List;
import static io.github.ms912x.desktop.AndroidFixtures.*;
import static io.github.ms912x.desktop.OutputFixtureTests.main;

/** Real View timers and MotionEvents verify complete touchpad command streams. */
final class TouchpadFixtureTests {
    private static void send(TouchpadSurfaceView pad,long down,long time,int action,float x,float y){
        MotionEvent event=MotionEvent.obtain(down,time,action,x,y,0);
        try{pad.onTouchEvent(event);}finally{event.recycle();}
    }
    static void run(Instrumentation test) throws Exception {
        MainActivity activity=(MainActivity)test.startActivitySync(new Intent(test.getTargetContext(),MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        List<Integer> commands=new ArrayList<>();
        TouchpadSurfaceView[] pad=new TouchpadSurfaceView[1];
        long[] secondDown=new long[1];
        try{
            main(test,()->{appearance(activity);return null;});
            main(test,()->{
                pad[0]=new TouchpadSurfaceView(activity,(type,x,y,button)->commands.add(type));
                activity.setContentView(pad[0]);pad[0].setOutputSize(1920,1200);return null;
            });
            test.waitForIdleSync();
            main(test,()->{
                long t=SystemClock.uptimeMillis();send(pad[0],t,t,MotionEvent.ACTION_DOWN,100,100);
                send(pad[0],t,t+20,MotionEvent.ACTION_UP,100,100);return null;
            });
            Thread.sleep(ViewConfiguration.getDoubleTapTimeout()+60);
            main(test,()->{
                require(commands.equals(java.util.Arrays.asList(TouchpadInput.CLICK)),"One tap emits exactly one click");commands.clear();
                long t=SystemClock.uptimeMillis();send(pad[0],t,t,MotionEvent.ACTION_DOWN,100,100);
                send(pad[0],t,t+20,MotionEvent.ACTION_UP,100,100);
                send(pad[0],t+60,t+60,MotionEvent.ACTION_DOWN,100,100);
                send(pad[0],t+60,t+90,MotionEvent.ACTION_UP,100,100);
                require(commands.equals(java.util.Arrays.asList(TouchpadInput.DRAG_START,TouchpadInput.DRAG_END,TouchpadInput.CLICK)),
                        "Quick double tap emits two clicks: held down/up plus one click");commands.clear();return null;
            });
            long down=SystemClock.uptimeMillis();
            main(test,()->{send(pad[0],down,down,MotionEvent.ACTION_DOWN,100,100);return null;});
            Thread.sleep(ViewConfiguration.getLongPressTimeout()+60);
            main(test,()->{
                send(pad[0],down,SystemClock.uptimeMillis(),MotionEvent.ACTION_MOVE,160,140);
                require(commands.equals(java.util.Arrays.asList(TouchpadInput.DRAG_START,TouchpadInput.MOVE)),"Hold then move keeps the primary button pressed");return null;
            });
            Thread.sleep(1050);
            main(test,()->{
                require(!commands.contains(TouchpadInput.DRAG_END),"Held drag does not release after one second");
                send(pad[0],down,SystemClock.uptimeMillis(),MotionEvent.ACTION_UP,160,140);
                require(commands.get(commands.size()-1)==TouchpadInput.DRAG_END&&!commands.contains(TouchpadInput.CLICK),"Drag lift releases without an extra tap");
                commands.clear();long t=SystemClock.uptimeMillis();
                send(pad[0],t,t,MotionEvent.ACTION_DOWN,100,100);send(pad[0],t,t+20,MotionEvent.ACTION_UP,100,100);
                secondDown[0]=t+60;send(pad[0],secondDown[0],secondDown[0],MotionEvent.ACTION_DOWN,100,100);return null;
            });
            Thread.sleep(ViewConfiguration.getLongPressTimeout()+100);
            main(test,()->{
                send(pad[0],secondDown[0],SystemClock.uptimeMillis(),MotionEvent.ACTION_UP,100,100);
                require(commands.equals(java.util.Arrays.asList(TouchpadInput.DRAG_START,TouchpadInput.DRAG_END)),"Double tap and hold stays one held click");
                commands.clear();long t=SystemClock.uptimeMillis();
                send(pad[0],t,t,MotionEvent.ACTION_DOWN,100,100);send(pad[0],t,t+20,MotionEvent.ACTION_MOVE,220,150);
                send(pad[0],t,t+40,MotionEvent.ACTION_UP,220,150);
                require(commands.equals(java.util.Arrays.asList(TouchpadInput.MOVE)),"Single finger movement never presses a button");
                commands.clear();t=SystemClock.uptimeMillis();
                send(pad[0],t,t,MotionEvent.ACTION_DOWN,100,100);send(pad[0],t,t+20,MotionEvent.ACTION_UP,100,100);
                activity.setContentView(new android.view.View(activity));return null;
            });
            Thread.sleep(ViewConfiguration.getDoubleTapTimeout()+60);
            main(test,()->{require(commands.isEmpty(),"Detached touchpad cannot emit a delayed click");return null;});
        }finally{main(test,()->{if(pad[0]!=null)pad[0].cancelGesture();activity.finish();return null;});}
    }

    private static void renderedPalette(TouchpadSurfaceView pad,boolean dark){
        float density=pad.getResources().getDisplayMetrics().density;
        int left=pad.getLeft(),top=pad.getTop(),right=pad.getRight(),bottom=pad.getBottom();
        int width=Math.round(200*density),height=Math.round(260*density);
        Bitmap bitmap=Bitmap.createBitmap(width,height,Bitmap.Config.ARGB_8888);
        try{
            pad.layout(0,0,width,height);pad.draw(new Canvas(bitmap));
            int y=Math.round(14*density);
            require(bitmap.getPixel(Math.round(14*density),y)==(dark?0xff192531:0xffe6edf5),
                    "Rendered touchpad surface follows the resolved appearance");
            require(bitmap.getPixel(Math.round(28*density),y)==(dark?0xff344454:0xff91a4b8),
                    "Rendered touchpad grid follows the resolved appearance");
        }finally{bitmap.recycle();pad.layout(left,top,right,bottom);}
    }

    private static void appearance(MainActivity activity) throws Exception {
        MainScreenView screen=ActivityFixtureTests.screen(activity);
        AppAppearance appearance=(AppAppearance)ActivityFixtureTests.field(screen,"appearance");
        AppAppearance.Mode original=appearance.mode();
        Configuration day=new Configuration(activity.getResources().getConfiguration());
        day.uiMode=(day.uiMode&~Configuration.UI_MODE_NIGHT_MASK)|Configuration.UI_MODE_NIGHT_NO;
        Configuration night=new Configuration(day);
        night.uiMode=(night.uiMode&~Configuration.UI_MODE_NIGHT_MASK)|Configuration.UI_MODE_NIGHT_YES;
        try{
            for(AppAppearance.Mode mode:AppAppearance.Mode.values()){
                for(Configuration configuration:new Configuration[]{day,night}){
                    boolean dark=mode==AppAppearance.Mode.DARK||(mode==AppAppearance.Mode.AUTO&&configuration==night);
                    appearance.select(mode,configuration);screen.configurationChanged(configuration);
                    renderedPalette(screen.touchpadSurface,dark);
                    TouchpadSurfaceView standalone=new TouchpadSurfaceView(activity.createConfigurationContext(configuration),
                            (type,x,y,button)->{});
                    renderedPalette(standalone,dark);
                    if(mode==AppAppearance.Mode.AUTO){
                        standalone.onConfigurationChanged(configuration==night?day:night);
                        renderedPalette(standalone,!dark);
                    }
                }
            }
            List<Integer> shortcuts=new ArrayList<>();
            MainScreenView.Actions actions=(MainScreenView.Actions)java.lang.reflect.Proxy.newProxyInstance(
                    MainScreenView.Actions.class.getClassLoader(),new Class<?>[]{MainScreenView.Actions.class},
                    (proxy,method,args)->{
                        if(method.getName().equals("openTouchpadApps"))shortcuts.add(-1);
                        else if(method.getName().equals("sendTouchpadInput"))shortcuts.add((Integer)args[0]);
                        return null;
                    });
            MainScreenView isolated=new MainScreenView(activity,appearance,actions);
            LinearLayout row=(LinearLayout)isolated.touchpadPage.getChildAt(isolated.touchpadPage.getChildCount()-1);
            String[] labels={"Apps","Home","Back"};
            require(row.getChildCount()==3,"Touchpad has three navigation shortcuts");
            for(int i=0;i<labels.length;i++){
                Button button=(Button)row.getChildAt(i);
                require(labels[i].contentEquals(button.getText()),"Touchpad shortcut order: Apps, Home, Back");
                require(button.performClick(),"Touchpad shortcut click");
            }
            require(shortcuts.equals(java.util.Arrays.asList(-1,TouchpadInput.HOME,TouchpadInput.BACK)),
                    "Reordered shortcuts retain their original actions");
        }finally{
            appearance.select(original,activity.getResources().getConfiguration());
            screen.configurationChanged(activity.getResources().getConfiguration());
        }
    }
}
