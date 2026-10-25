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
            private int downs,ups,moves;
            private long downTime;
            private float startX,startY;
            {setBackgroundColor(0xff204060);publish(null);}
            @Override public void onWindowFocusChanged(boolean focused){super.onWindowFocusChanged(focused);publish(null);}
            @Override protected void onSizeChanged(int w,int h,int oldw,int oldh){publish(null);}
            private void publish(MotionEvent event){
                int[] location=new int[2];getLocationOnScreen(location);
                try{setContentDescription("INPUT:"+new JSONObject().put("downs",downs).put("ups",ups).put("moves",moves)
                        .put("focused",hasWindowFocus()).put("width",getWidth()).put("height",getHeight()).put("left",location[0]).put("top",location[1])
                        .put("buttons",event==null?0:event.getButtonState()).put("downTime",downTime)
                        .put("eventDownTime",event==null?0:event.getDownTime())
                        .put("dx",event==null?0:event.getRawX()-startX).put("dy",event==null?0:event.getRawY()-startY));}
                catch(org.json.JSONException failure){throw new IllegalStateException(failure);}
            }
            @Override public boolean onTouchEvent(MotionEvent event){
                if(event.getActionMasked()==MotionEvent.ACTION_DOWN){downs++;downTime=event.getDownTime();startX=event.getRawX();startY=event.getRawY();}
                if(event.getActionMasked()==MotionEvent.ACTION_UP)ups++;
                if(event.getActionMasked()==MotionEvent.ACTION_MOVE)moves++;
                publish(event);return true;
            }
        });
    }
}
