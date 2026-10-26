package io.github.ms912x.helper;

import android.graphics.PointF;

/** Unassociated uinput follows Android's default mouse display, including native Samsung DeX. */
final class DefaultMouseEndpoint implements SharedMouseRouter.Endpoint {
    private final NativeMouse source;
    private final MouseScalingLease scaling;
    private final float gainX,gainY;
    private final FrameworkMouseCompat api;
    private final int display;
    private long lastWrite,lastSync;
    private float x,y;
    private int buttons;
    private DefaultMouseEndpoint(NativeMouse source,PointF position,MouseScalingLease scaling,float gainX,float gainY,FrameworkMouseCompat api,int display){
        this.source=source;x=position.x;y=position.y;this.scaling=scaling;this.gainX=gainX;this.gainY=gainY;this.api=api;this.display=display;
    }
    static DefaultMouseEndpoint calibrate(NativeMouse source,FrameworkMouseCompat api,int display,int width,int height,MouseScalingLease scaling) throws Exception {
        Thread.sleep(60);PointF before=api.position(display);
        int dx=before.x>width/2f?-8:8,dy=before.y>height/2f?-8:8;
        source.emit(dx,dy,0,0,0);PointF after=awaitMovement(api,display,before);
        float gx=(after.x-before.x)/dx,gy=(after.y-before.y)/dy;
        if(!Float.isFinite(gx)||!Float.isFinite(gy)||gx<.05f||gx>16||gy<.05f||gy>16)
            throw new IllegalStateException("Android did not expose a usable desktop mouse mapping");
        source.emit(-dx,-dy,0,0,0);PointF returned=awaitMovement(api,display,after);
        return new DefaultMouseEndpoint(source,returned,scaling,gx,gy,api,display);
    }
    private static PointF awaitMovement(FrameworkMouseCompat api,int display,PointF before) throws Exception {
        long deadline=android.os.SystemClock.uptimeMillis()+300;
        PointF point=before;
        do{Thread.sleep(2);point=api.position(display);}while(point!=null&&point.equals(before.x,before.y)&&android.os.SystemClock.uptimeMillis()<deadline);
        if(point==null||point.equals(before.x,before.y))throw new IllegalStateException("Desktop mouse did not respond");
        return point;
    }
    PointF position(){return new PointF(x,y);}
    float gainX(){return gainX;}
    float gainY(){return gainY;}
    @Override public void moveTo(float px,float py) throws Exception {
        long now=android.os.SystemClock.uptimeMillis();
        if(now-lastWrite>30&&now-lastSync>500){
            PointF actual=api.position(display);
            if(actual==null)throw new IllegalStateException("Desktop mouse disappeared");
            x=actual.x;y=actual.y;lastSync=now;
        }
        int dx=Math.round((px-x)/gainX),dy=Math.round((py-y)/gainY);
        if(dx!=0||dy!=0){source.emit(dx,dy,buttons,0,0);x+=dx*gainX;y+=dy*gainY;lastWrite=now;}
    }
    @Override public void enterAt(float px,float py) throws Exception {
        boolean stationary=Math.round((px-x)/gainX)==0&&Math.round((py-y)/gainY)==0;moveTo(px,py);
        if(stationary){int step=px>0?-1:1;source.emit(step,0,0,0,0);source.emit(-step,0,0,0,0);}
    }
    @Override public void buttons(int next) throws Exception {source.emit(0,0,next,0,0);buttons=next;}
    @Override public void scroll(int h,int v) throws Exception {source.emit(0,0,buttons,h,v);}
    @Override public void close() throws Exception {scaling.close();}
}
