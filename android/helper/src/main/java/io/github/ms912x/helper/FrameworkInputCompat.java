package io.github.ms912x.helper;

import android.content.Context;
import android.os.Build;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;
import java.lang.reflect.Method;

/** Small cached bridge for shell-only input injection and per-display event targeting. */
final class FrameworkInputCompat {
    // Wait until the system has dispatched each event before returning. Async
    // injection let a fast drag release race ahead of its queued button-down.
    private static final int INJECT_WAIT_FOR_FINISH=2;
    private final Object manager;
    private final Method inject, setDisplay;
    private final Method actionButton=MotionEvent.class.getMethod("setActionButton",int.class);
    private long mouseDownTime;

    FrameworkInputCompat(Context context) throws Exception {
        Class<?> type=Class.forName("android.hardware.input.InputManager");
        manager=context.getSystemService(Context.INPUT_SERVICE);
        if(manager==null)throw new IllegalStateException("Android input manager unavailable");
        inject=type.getMethod("injectInputEvent",InputEvent.class,int.class);
        setDisplay=InputEvent.class.getMethod("setDisplayId",int.class);
    }

    boolean send(InputEvent event,int displayId) throws Exception {
        return send(event,displayId,INJECT_WAIT_FOR_FINISH);
    }
    private boolean send(InputEvent event,int displayId,int mode) throws Exception {
        if(Build.VERSION.SDK_INT<29)throw new UnsupportedOperationException("Per-display input requires Android 10 or later");
        setDisplay.invoke(event,displayId);
        return Boolean.TRUE.equals(inject.invoke(manager,event,mode));
    }

    boolean move(int displayId,float x,float y,int buttons) throws Exception {
        return move(displayId,x,y,buttons,false);
    }
    boolean mouseMove(int displayId,float x,float y,int buttons) throws Exception {
        return move(displayId,x,y,buttons,true);
    }
    private boolean move(int displayId,float x,float y,int buttons,boolean immediateHover) throws Exception {
        long now=android.os.SystemClock.uptimeMillis();
        long down=buttons!=0&&mouseDownTime!=0?mouseDownTime:now;
        return motion(displayId,down,now,buttons==0?MotionEvent.ACTION_HOVER_MOVE:MotionEvent.ACTION_MOVE,x,y,buttons,0,0,0,
                immediateHover&&buttons==0?0:INJECT_WAIT_FOR_FINISH);
    }
    boolean button(int displayId,float x,float y,int button,boolean down) throws Exception {
        long now=android.os.SystemClock.uptimeMillis();
        if(down)mouseDownTime=now;
        long start=down?now:mouseDownTime==0?now:mouseDownTime;
        boolean accepted=motion(displayId,start,now,down?MotionEvent.ACTION_DOWN:MotionEvent.ACTION_UP,x,y,down?button:0,0,0);
        if(!down||!accepted)mouseDownTime=0;
        return accepted;
    }
    boolean scroll(int displayId,float x,float y,float horizontal,float vertical) throws Exception {
        return motion(displayId,MotionEvent.ACTION_SCROLL,x,y,0,horizontal,vertical);
    }
    boolean buttons(int displayId,float x,float y,int previous,int next) throws Exception {
        if(previous==next)return true;
        long now=android.os.SystemClock.uptimeMillis();
        if(previous==0)mouseDownTime=now;
        long start=mouseDownTime==0?now:mouseDownTime;
        boolean accepted=true;
        if(previous==0)accepted=motion(displayId,start,now,MotionEvent.ACTION_DOWN,x,y,next,0,0);
        for(int i=0;i<5;i++){
            int bit=1<<i;if(((previous^next)&bit)==0)continue;
            accepted=motion(displayId,start,now,(next&bit)!=0?MotionEvent.ACTION_BUTTON_PRESS:MotionEvent.ACTION_BUTTON_RELEASE,
                    x,y,next,0,0,bit)&&accepted;
        }
        if(next==0){accepted=motion(displayId,start,now,MotionEvent.ACTION_UP,x,y,0,0,0)&&accepted;mouseDownTime=0;}
        return accepted;
    }
    boolean back(int displayId) throws Exception {
        long downTime=android.os.SystemClock.uptimeMillis();
        KeyEvent down=new KeyEvent(downTime,downTime,KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_BACK,0,0,
                KeyCharacterMap.VIRTUAL_KEYBOARD,0,0,InputDevice.SOURCE_KEYBOARD);
        boolean downAccepted=send(down,displayId);
        long now=android.os.SystemClock.uptimeMillis();
        KeyEvent up=new KeyEvent(downTime,now,KeyEvent.ACTION_UP,KeyEvent.KEYCODE_BACK,0,0,
                KeyCharacterMap.VIRTUAL_KEYBOARD,0,0,InputDevice.SOURCE_KEYBOARD);
        boolean upAccepted=send(up,displayId);
        return downAccepted&&upAccepted;
    }

    private boolean motion(int displayId,int action,float x,float y,int buttons,float horizontal,float vertical) throws Exception {
        long now=android.os.SystemClock.uptimeMillis();
        return motion(displayId,now,now,action,x,y,buttons,horizontal,vertical);
    }
    private boolean motion(int displayId,long downTime,long eventTime,int action,float x,float y,int buttons,float horizontal,float vertical) throws Exception {
        return motion(displayId,downTime,eventTime,action,x,y,buttons,horizontal,vertical,0);
    }
    private boolean motion(int displayId,long downTime,long eventTime,int action,float x,float y,int buttons,float horizontal,float vertical,int changedButton) throws Exception {
        return motion(displayId,downTime,eventTime,action,x,y,buttons,horizontal,vertical,changedButton,INJECT_WAIT_FOR_FINISH);
    }
    private boolean motion(int displayId,long downTime,long eventTime,int action,float x,float y,int buttons,float horizontal,float vertical,int changedButton,int mode) throws Exception {
        MotionEvent.PointerProperties property=new MotionEvent.PointerProperties();property.id=0;property.toolType=MotionEvent.TOOL_TYPE_MOUSE;
        MotionEvent.PointerCoords coordinates=new MotionEvent.PointerCoords();coordinates.x=x;coordinates.y=y;coordinates.pressure=buttons==0?0:1;coordinates.size=1;
        if(action==MotionEvent.ACTION_SCROLL){coordinates.setAxisValue(MotionEvent.AXIS_HSCROLL,horizontal);coordinates.setAxisValue(MotionEvent.AXIS_VSCROLL,vertical);}
        MotionEvent event=MotionEvent.obtain(downTime,eventTime,action,1,new MotionEvent.PointerProperties[]{property},
                new MotionEvent.PointerCoords[]{coordinates},0,buttons,1,1,0,0,InputDevice.SOURCE_MOUSE,0);
        try{if(changedButton!=0)actionButton.invoke(event,changedButton);return send(event,displayId,mode);}finally{event.recycle();}
    }
}
