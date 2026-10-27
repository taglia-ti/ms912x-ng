package io.github.taglia_ti.ms912x_ng.helper;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.PorterDuff;
import android.graphics.Rect;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.view.Surface;
import android.view.SurfaceControl;
import java.lang.reflect.Method;

/** Click-through cursor composed directly into the helper-owned display. */
final class PointerOverlay implements AutoCloseable {
    private static final long HIDE_DELAY_MS=5_000;
    private static final int CLICK_CLEARANCE_PX=1;
    private final Handler handler;
    private final SurfaceControl layer;
    private final Surface surface;
    private final SurfaceControl.Transaction transaction;
    private final int displayWidth,displayHeight,cursorWidth,cursorHeight;
    private final SystemPointerIcon icon;
    private final float hotspotX,hotspotY;
    private final Path arrow=new Path();
    private final Paint fill=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint outline=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint shadow=new Paint(Paint.ANTI_ALIAS_FLAG);
    private boolean closed,suspended;
    private final Runnable hide=()->setVisible(false);
    private final Runnable position=this::applyPosition;
    private float x,y;

    PointerOverlay(Handler handler,android.content.Context context,int displayId,int layerStack,int width,int height) {
        this(handler,layerStack,width,height,loadIcon(context,displayId));
    }

    private static SystemPointerIcon loadIcon(android.content.Context context,int displayId){
        android.view.Display display=context.getSystemService(DisplayManager.class).getDisplay(displayId);
        if(display==null)throw new IllegalStateException("Helper display disappeared before cursor setup");
        try{return SystemPointerIcon.load(context.createDisplayContext(display));}
        catch(RuntimeException|LinkageError unavailable){return null;}
    }

    PointerOverlay(Handler handler,int layerStack,int width,int height,SystemPointerIcon icon) {
        this.handler=handler;displayWidth=width;displayHeight=height;this.icon=icon;
        cursorWidth=icon==null?24:icon.bitmap.getWidth();cursorHeight=icon==null?24:icon.bitmap.getHeight();
        hotspotX=icon==null?1f:icon.hotspotX;hotspotY=icon==null?1f:icon.hotspotY;
        // Mocu's CC0 default cursor, at its original 24-pixel size.
        arrow.moveTo(1f,1f);arrow.lineTo(1f,14.75f);arrow.lineTo(4.94f,13.12f);
        arrow.lineTo(6.66f,17.28f);arrow.lineTo(8.5f,16.5f);arrow.lineTo(6.79f,12.35f);
        arrow.lineTo(10.73f,10.72f);arrow.close();
        fill.setColor(0xfffafbfc);fill.setStyle(Paint.Style.FILL);
        outline.setColor(0xff1a1b1c);outline.setStyle(Paint.Style.FILL_AND_STROKE);
        outline.setStrokeWidth(2f);outline.setStrokeJoin(Paint.Join.ROUND);
        shadow.setColor(0x1a0a0b0c);shadow.setStyle(Paint.Style.FILL_AND_STROKE);
        shadow.setStrokeWidth(2f);shadow.setStrokeJoin(Paint.Join.ROUND);
        SurfaceControl nextLayer=new SurfaceControl.Builder().setName("MS912x HDMI pointer")
                .setBufferSize(cursorWidth,cursorHeight).setFormat(PixelFormat.TRANSLUCENT).build();
        Surface nextSurface=null;
        SurfaceControl.Transaction nextTransaction=null;
        try{
            nextSurface=new Surface(nextLayer);
            nextTransaction=new SurfaceControl.Transaction();
            layer=nextLayer;surface=nextSurface;transaction=nextTransaction;
            draw();
            setLayerStack(transaction,layer,layerStack);
            if(android.os.Build.VERSION.SDK_INT>=31)try{
                // Without this shell-only flag, Android treats the cursor's
                // pixels as an obscuring overlay and blocks clicks beneath it.
                SurfaceControl.Transaction.class.getMethod("setTrustedOverlay",SurfaceControl.class,boolean.class)
                        .invoke(transaction,layer,true);
            }catch(ReflectiveOperationException failure){throw new IllegalStateException("Cannot make cursor input-transparent",failure);}
            transaction.setLayer(layer,Integer.MAX_VALUE).setPosition(layer,CLICK_CLEARANCE_PX-hotspotX,CLICK_CLEARANCE_PX-hotspotY).setVisibility(layer,true).apply();
        }catch(RuntimeException failure){
            if(nextTransaction!=null)nextTransaction.close();
            if(nextSurface!=null)nextSurface.release();
            nextLayer.release();
            throw failure;
        }
    }

    synchronized void moveTo(float nextX,float nextY) {
        if(closed||suspended)return;
        x=nextX;y=nextY;
        handler.removeCallbacks(hide);
        handler.removeCallbacks(position);
        handler.post(position);
        handler.postDelayed(hide,HIDE_DELAY_MS);
    }

    private synchronized void applyPosition(){
        if(closed||suspended)return;
        transaction.setPosition(layer,
                CLICK_CLEARANCE_PX+Math.max(0,Math.min(displayWidth-1,Math.round(x)))-hotspotX,
                CLICK_CLEARANCE_PX+Math.max(0,Math.min(displayHeight-1,Math.round(y)))-hotspotY)
                .setVisibility(layer,true).apply();
    }

    synchronized void suspend(boolean value){
        if(closed||suspended==value)return;suspended=value;
        handler.removeCallbacks(position);handler.removeCallbacks(hide);
        handler.post(()->{synchronized(this){if(!closed)setVisible(!suspended);}});
        if(!value)handler.postDelayed(hide,HIDE_DELAY_MS);
    }

    private void draw(){
        Canvas canvas=surface.lockCanvas(null);
        try{
            canvas.drawColor(android.graphics.Color.TRANSPARENT,PorterDuff.Mode.CLEAR);
            if(icon==null){
                canvas.save();canvas.translate(1,1);canvas.drawPath(arrow,shadow);canvas.restore();
                canvas.drawPath(arrow,outline);canvas.drawPath(arrow,fill);
            }
            else canvas.drawBitmap(icon.bitmap,null,new Rect(0,0,cursorWidth,cursorHeight),null);
        }finally{surface.unlockCanvasAndPost(canvas);}
    }

    private void setVisible(boolean visible){
        if(closed)return;
        transaction.setVisibility(layer,visible).apply();
    }

    private static void setLayerStack(SurfaceControl.Transaction transaction,SurfaceControl layer,int id){
        try{
            Method method=SurfaceControl.Transaction.class.getMethod("setLayerStack",SurfaceControl.class,int.class);
            method.invoke(transaction,layer,id);
        }catch(ReflectiveOperationException failure){
            throw new IllegalStateException("Cannot attach cursor to helper display",failure);
        }
    }

    @Override public synchronized void close(){
        if(closed)return;closed=true;handler.removeCallbacks(hide);handler.removeCallbacks(position);
        handler.post(()->{
            try{transaction.setVisibility(layer,false).reparent(layer,null).apply();}
            finally{surface.release();layer.release();transaction.close();}
        });
    }
}
