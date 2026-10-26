package io.github.ms912x.desktop;

import android.app.Activity;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import org.json.JSONObject;

/** Separate-UID receiver exposes delivered mouse state through accessibility. */
public final class DesktopInputFixtureActivity extends Activity {
    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        setContentView(new View(this){
            private int downs,ups,moves,hovers,scrolls,cancels,buttons;
            private long downTime;
            private long eventDownTime;
            private float startX,startY;
            private float rawX,rawY,vertical,horizontal;
            {setBackgroundColor(0xff204060);publish(null);}
            @Override public void onWindowFocusChanged(boolean focused){super.onWindowFocusChanged(focused);publish(null);}
            @Override protected void onSizeChanged(int w,int h,int oldw,int oldh){publish(null);}
            private void publish(MotionEvent event){
                int[] location=new int[2];getLocationOnScreen(location);
                if(event!=null){
                    rawX=event.getRawX();rawY=event.getRawY();eventDownTime=event.getDownTime();
                    buttons=event.getActionMasked()==MotionEvent.ACTION_CANCEL?0:event.getButtonState();
                    vertical=event.getAxisValue(MotionEvent.AXIS_VSCROLL);horizontal=event.getAxisValue(MotionEvent.AXIS_HSCROLL);
                }
                try{setContentDescription("INPUT:"+new JSONObject().put("downs",downs).put("ups",ups).put("moves",moves)
                        .put("hovers",hovers).put("scrolls",scrolls).put("cancels",cancels).put("x",rawX).put("y",rawY)
                        .put("vertical",vertical).put("horizontal",horizontal)
                        .put("focused",hasWindowFocus()).put("width",getWidth()).put("height",getHeight()).put("left",location[0]).put("top",location[1])
                        .put("buttons",buttons).put("downTime",downTime).put("eventDownTime",eventDownTime)
                        .put("dx",rawX-startX).put("dy",rawY-startY));}
                catch(org.json.JSONException failure){throw new IllegalStateException(failure);}
            }
            @Override public boolean onTouchEvent(MotionEvent event){
                if(event.getActionMasked()==MotionEvent.ACTION_DOWN){downs++;downTime=event.getDownTime();startX=event.getRawX();startY=event.getRawY();}
                if(event.getActionMasked()==MotionEvent.ACTION_UP)ups++;
                if(event.getActionMasked()==MotionEvent.ACTION_MOVE)moves++;
                if(event.getActionMasked()==MotionEvent.ACTION_CANCEL)cancels++;
                publish(event);return true;
            }
            @Override public boolean onGenericMotionEvent(MotionEvent event){
                if(event.getActionMasked()==MotionEvent.ACTION_HOVER_MOVE||event.getActionMasked()==MotionEvent.ACTION_HOVER_ENTER)hovers++;
                if(event.getActionMasked()==MotionEvent.ACTION_SCROLL)scrolls++;
                publish(event);return true;
            }
        });
    }
}
