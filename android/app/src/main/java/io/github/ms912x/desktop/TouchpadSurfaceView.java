package io.github.ms912x.desktop;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;

/** Relative pointer surface; it never places a touch directly on the phone display. */
final class TouchpadSurfaceView extends View {
    interface Listener { void send(int type,float x,float y,int button); }
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float touchSlop;
    private final Listener listener;
    private int outputWidth=1280,outputHeight=720,background=0xffe6edf5,grid=0xff91a4b8,ink=0xff526477;
    private float lastX,lastY,downX,downY,scrollX,scrollY,lastTapX,lastTapY;
    private long lastTapTime;
    private int fingers;
    private boolean dragged,pointerMoved,doubleTap,multiTouch;
    private Runnable pendingClick,longPress;

    TouchpadSurfaceView(Context context,Listener listener){
        super(context);this.listener=listener;touchSlop=ViewConfiguration.get(context).getScaledTouchSlop();
        setContentDescription("Touchpad for the HDMI screen. Move one finger to move the pointer; tap to click; hold then move or double tap and drag to hold the left button; use two fingers to scroll.");
        setFocusable(true);setMinimumHeight(Math.round(220*getResources().getDisplayMetrics().density));
    }

    @Override protected void onDraw(Canvas canvas){
        super.onDraw(canvas);float d=getResources().getDisplayMetrics().density;
        paint.setStyle(Paint.Style.FILL);paint.setColor(background);canvas.drawRoundRect(new RectF(0,0,getWidth(),getHeight()),18*d,18*d,paint);
        paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(1.5f*d);paint.setColor(grid);
        float step=28*d;
        for(float x=step;x<getWidth();x+=step)canvas.drawLine(x,0,x,getHeight(),paint);
        for(float y=step;y<getHeight();y+=step)canvas.drawLine(0,y,getWidth(),y,paint);
        paint.setStyle(Paint.Style.FILL);paint.setColor(ink);paint.setTextSize(14*d);
        String[] hints={"Move · tap to click","Hold + move to drag · two fingers scroll"};
        float maxWidth=Math.max(1,getWidth()-24*d);
        for(String hint:hints)if(paint.measureText(hint)>maxWidth){
            paint.setTextSize(Math.max(10*d,14*d*maxWidth/paint.measureText(hint)));
        }
        float lineHeight=paint.getFontMetrics().descent-paint.getFontMetrics().ascent;
        float firstBaseline=getHeight()/2f-lineHeight*.62f;
        for(int i=0;i<hints.length;i++)
            canvas.drawText(hints[i],Math.max(12*d,(getWidth()-paint.measureText(hints[i]))/2),firstBaseline+i*lineHeight*1.2f,paint);
        paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(2*d);canvas.drawRoundRect(new RectF(1,1,getWidth()-1,getHeight()-1),18*d,18*d,paint);
    }
    void setOutputSize(int width,int height){outputWidth=Math.max(1,width);outputHeight=Math.max(1,height);}
    void setColors(int background,int grid,int ink){this.background=background;this.grid=grid;this.ink=ink;invalidate();}

    @Override public boolean onTouchEvent(MotionEvent event){
        int action=event.getActionMasked();
        switch(action){
            case MotionEvent.ACTION_DOWN:
                fingers=1;dragged=false;pointerMoved=false;multiTouch=false;
                doubleTap=isDoubleTap(event.getX(),event.getY(),event.getEventTime());
                if(doubleTap)cancelPendingClick();
                lastX=downX=event.getX();lastY=downY=event.getY();
                if(doubleTap){dragged=true;listener.send(TouchpadInput.DRAG_START,0,0,0);}
                else scheduleLongPress();
                return true;
            case MotionEvent.ACTION_POINTER_DOWN:
                cancelDragLongPress();
                if(dragged)listener.send(TouchpadInput.DRAG_END,0,0,0);
                if(doubleTap&&!dragged)listener.send(TouchpadInput.CLICK,0,0,1);
                dragged=false;doubleTap=false;pointerMoved=true;multiTouch=true;fingers=event.getPointerCount();scrollX=averageX(event);scrollY=averageY(event);return true;
            case MotionEvent.ACTION_MOVE:
                if(event.getPointerCount()>1||multiTouch){
                    float x=averageX(event),y=averageY(event);
                    if(fingers>1){float unit=Math.max(24,getResources().getDisplayMetrics().density*32);
                        listener.send(TouchpadInput.SCROLL,clamp((scrollX-x)/unit),clamp((scrollY-y)/unit),0);}
                    scrollX=x;scrollY=y;fingers=event.getPointerCount();return true;
                }
                float x=event.getX(),y=event.getY();float d=getResources().getDisplayMetrics().density;
                if(!pointerMoved&&Math.hypot(x-downX,y-downY)>Math.max(touchSlop,8*d)){
                    pointerMoved=true;
                    cancelDragLongPress();
                }
                float width=Math.max(1,getWidth()),height=Math.max(1,getHeight());
                listener.send(TouchpadInput.MOVE,(x-lastX)*outputWidth/width,(y-lastY)*outputHeight/height,0);
                lastX=x;lastY=y;return true;
            case MotionEvent.ACTION_POINTER_UP:
                multiTouch=true;fingers=event.getPointerCount()-1;scrollX=averageOtherX(event,event.getActionIndex());scrollY=averageOtherY(event,event.getActionIndex());return true;
            case MotionEvent.ACTION_UP:
                cancelDragLongPress();
                boolean wasDrag=dragged;
                if(wasDrag)listener.send(TouchpadInput.DRAG_END,0,0,0);
                if(wasDrag){
                    // A held press is already the complete gesture. Sending a
                    // click after releasing it turns a drag into an extra tap.
                    lastTapTime=0;
                }else if(doubleTap&&!multiTouch&&!pointerMoved){
                    // The down/up pair held across this second tap is the first
                    // click; send one more to complete a double click.
                    listener.send(TouchpadInput.CLICK,0,0,1);lastTapTime=0;
                }else if(!wasDrag&&!multiTouch&&!pointerMoved){
                    if(doubleTap){listener.send(TouchpadInput.CLICK,0,0,1);listener.send(TouchpadInput.CLICK,0,0,1);lastTapTime=0;}
                    else{lastTapX=event.getX();lastTapY=event.getY();lastTapTime=event.getEventTime();scheduleSingleClick();}
                } else lastTapTime=0;
                reset();performClick();return true;
            case MotionEvent.ACTION_CANCEL:
                cancelGesture();return true;
            default:return true;
        }
    }

    private float averageX(MotionEvent e){float sum=0;for(int i=0;i<e.getPointerCount();++i)sum+=e.getX(i);return sum/e.getPointerCount();}
    private float averageY(MotionEvent e){float sum=0;for(int i=0;i<e.getPointerCount();++i)sum+=e.getY(i);return sum/e.getPointerCount();}
    private float averageOtherX(MotionEvent e,int removed){float sum=0;int n=0;for(int i=0;i<e.getPointerCount();++i)if(i!=removed){sum+=e.getX(i);n++;}return n==0?0:sum/n;}
    private float averageOtherY(MotionEvent e,int removed){float sum=0;int n=0;for(int i=0;i<e.getPointerCount();++i)if(i!=removed){sum+=e.getY(i);n++;}return n==0?0:sum/n;}
    private boolean isDoubleTap(float x,float y,long time){
        return lastTapTime>0&&time-lastTapTime<=ViewConfiguration.getDoubleTapTimeout()
                &&Math.hypot(x-lastTapX,y-lastTapY)<=ViewConfiguration.get(getContext()).getScaledDoubleTapSlop();
    }
    private void scheduleSingleClick(){
        cancelPendingClick();
        pendingClick=()->{pendingClick=null;listener.send(TouchpadInput.CLICK,0,0,1);lastTapTime=0;};
        postDelayed(pendingClick,ViewConfiguration.getDoubleTapTimeout());
    }
    private void scheduleLongPress(){
        cancelDragLongPress();
        longPress=()->{
            longPress=null;
            if(fingers==1&&!pointerMoved&&!multiTouch&&!dragged){
                dragged=true;listener.send(TouchpadInput.DRAG_START,0,0,0);
            }
        };
        postDelayed(longPress,ViewConfiguration.getLongPressTimeout());
    }
    private void cancelDragLongPress(){if(longPress!=null){removeCallbacks(longPress);longPress=null;}}
    private void cancelPendingClick(){if(pendingClick!=null){removeCallbacks(pendingClick);pendingClick=null;}}
    private static float clamp(float value){return Math.max(-24,Math.min(24,value));}
    private void reset(){fingers=0;dragged=false;pointerMoved=false;doubleTap=false;multiTouch=false;}
    void cancelGesture(){cancelPendingClick();cancelDragLongPress();if(dragged)listener.send(TouchpadInput.CANCEL,0,0,0);lastTapTime=0;reset();}
    @Override public boolean performClick(){super.performClick();return true;}
    @Override public void onWindowFocusChanged(boolean hasWindowFocus){
        super.onWindowFocusChanged(hasWindowFocus);if(!hasWindowFocus)cancelGesture();
    }
}
