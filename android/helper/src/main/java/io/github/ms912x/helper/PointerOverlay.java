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
    private final Handler handler;
    private final SurfaceControl layer;
    private final Surface surface;
    private final int displayWidth,displayHeight,cursorWidth,cursorHeight;
    private final Path arrow=new Path();
    private final Paint fill=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint outline=new Paint(Paint.ANTI_ALIAS_FLAG);
    private boolean closed;
    private final Runnable hide=()->setVisible(false);
    private float x,y;

    PointerOverlay(Handler handler,android.content.Context context,int displayId,int width,int height) {
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
        layer=new SurfaceControl.Builder().setName("MS912x HDMI pointer")
                .setBufferSize(cursorWidth,cursorHeight).setFormat(PixelFormat.TRANSLUCENT).build();
        surface=new Surface(layer);
        draw();
        SurfaceControl.Transaction transaction=new SurfaceControl.Transaction();
        setLayerStack(transaction,layer,displayId);
        transaction.setLayer(layer,Integer.MAX_VALUE).setPosition(layer,0,0)
                .setVisibility(layer,true).apply();
    }

    synchronized void moveTo(float nextX,float nextY) {
        if(closed)return;
        x=nextX;y=nextY;
        handler.removeCallbacks(hide);
        handler.post(()->{
            if(closed)return;
            new SurfaceControl.Transaction().setPosition(layer,
                    Math.max(0,Math.min(displayWidth-cursorWidth,Math.round(x))),
                    Math.max(0,Math.min(displayHeight-cursorHeight,Math.round(y))))
                    .setVisibility(layer,true).apply();
        });
        handler.postDelayed(hide,HIDE_DELAY_MS);
    }

    private void draw(){
        Canvas canvas=surface.lockCanvas(null);
        try{
            canvas.drawColor(android.graphics.Color.TRANSPARENT,PorterDuff.Mode.CLEAR);
            canvas.drawPath(arrow,outline);canvas.drawPath(arrow,fill);
        }catch(RuntimeException failure){
            surface.unlockCanvasAndPost(canvas);throw failure;
        }
        surface.unlockCanvasAndPost(canvas);
    }

    private void setVisible(boolean visible){
        if(closed)return;
        SurfaceControl.Transaction transaction=new SurfaceControl.Transaction();
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
        if(closed)return;closed=true;handler.removeCallbacks(hide);
        handler.post(()->{surface.release();layer.release();});
    }
}
