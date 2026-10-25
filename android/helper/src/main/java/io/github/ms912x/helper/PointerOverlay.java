package io.github.ms912x.helper;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.PorterDuff;
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
    private final Path arrow=new Path();
    private final Paint fill=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint outline=new Paint(Paint.ANTI_ALIAS_FLAG);
    private boolean closed;
    private final Runnable hide=()->setVisible(false);
    private final Runnable position=this::applyPosition;
    private float x,y;

    PointerOverlay(Handler handler,android.content.Context context,int displayId,int layerStack,int width,int height) {
        this.handler=handler;displayWidth=width;displayHeight=height;
        android.view.Display display=context.getSystemService(DisplayManager.class).getDisplay(displayId);
        if(display==null)throw new IllegalStateException("Helper display disappeared before cursor setup");
        cursorWidth=12;cursorHeight=12;
        // Equilateral triangle with its left edge at 80 degrees to horizontal.
        arrow.moveTo(0.5f,0.5f);arrow.lineTo(2.411f,11.333f);arrow.lineTo(10.837f,4.25f);arrow.close();
        boolean dark=(context.getResources().getConfiguration().uiMode
                &android.content.res.Configuration.UI_MODE_NIGHT_MASK)==android.content.res.Configuration.UI_MODE_NIGHT_YES;
        fill.setColor(dark?0xfff5f7fa:0xff111820);fill.setStyle(Paint.Style.FILL);
        outline.setColor(dark?0xff111820:0xffffffff);outline.setStyle(Paint.Style.STROKE);
        outline.setStrokeWidth(1.2f);outline.setStrokeJoin(Paint.Join.ROUND);
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
            transaction.setLayer(layer,Integer.MAX_VALUE).setPosition(layer,CLICK_CLEARANCE_PX,CLICK_CLEARANCE_PX).setVisibility(layer,true).apply();
        }catch(RuntimeException failure){
            if(nextTransaction!=null)nextTransaction.close();
            if(nextSurface!=null)nextSurface.release();
            nextLayer.release();
            throw failure;
        }
    }

    synchronized void moveTo(float nextX,float nextY) {
        if(closed)return;
        x=nextX;y=nextY;
        handler.removeCallbacks(hide);
        handler.removeCallbacks(position);
        handler.post(position);
        handler.postDelayed(hide,HIDE_DELAY_MS);
    }

    private synchronized void applyPosition(){
        if(closed)return;
        transaction.setPosition(layer,
                CLICK_CLEARANCE_PX+Math.max(0,Math.min(displayWidth-1,Math.round(x))),
                CLICK_CLEARANCE_PX+Math.max(0,Math.min(displayHeight-1,Math.round(y))))
                .setVisibility(layer,true).apply();
    }

    private void draw(){
        Canvas canvas=surface.lockCanvas(null);
        try{
            canvas.drawColor(android.graphics.Color.TRANSPARENT,PorterDuff.Mode.CLEAR);
            canvas.drawPath(arrow,outline);canvas.drawPath(arrow,fill);
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
