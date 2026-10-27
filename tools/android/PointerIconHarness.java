package io.github.taglia_ti.ms912x_ng.helper;

import android.graphics.Bitmap;
import android.graphics.PixelFormat;
import android.hardware.display.DisplayManager;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import java.nio.ByteBuffer;

/** Runs the real cursor loader and compositor layer as the shell helper. */
public final class PointerIconHarness {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static boolean red(Image image, int x, int y) {
        Image.Plane plane=image.getPlanes()[0];
        ByteBuffer pixels=plane.getBuffer();
        int offset=y*plane.getRowStride()+x*plane.getPixelStride();
        return (pixels.get(offset)&255)>230 && (pixels.get(offset+1)&255)<20 &&
                (pixels.get(offset+2)&255)<20;
    }

    private static void waitForPixels(ImageReader reader, boolean synthetic) throws Exception {
        long end=android.os.SystemClock.uptimeMillis()+5000;
        while (android.os.SystemClock.uptimeMillis()<end) {
            try (Image image=reader.acquireLatestImage()) {
                if (image!=null) {
                    if (synthetic && red(image,98,97) && red(image,103,102) &&
                            !red(image,97,97) && !red(image,104,103)) return;
                    if (!synthetic) {
                        Image.Plane plane=image.getPlanes()[0];
                        ByteBuffer bytes=plane.getBuffer();
                        for (int y=100;y<112;y++) for (int x=100;x<112;x++) {
                            int offset=y*plane.getRowStride()+x*plane.getPixelStride();
                            if ((bytes.get(offset)&255)>80 || (bytes.get(offset+1)&255)>80 ||
                                    (bytes.get(offset+2)&255)>80) return;
                        }
                    }
                }
            }
            Thread.sleep(25);
        }
        throw new AssertionError(synthetic ? "Cursor bitmap or hotspot was not composed at the expected position" :
                "Fallback cursor was not composed");
    }

    private static void waitForSystemIcon(ImageReader reader, SystemPointerIcon icon) throws Exception {
        long end=android.os.SystemClock.uptimeMillis()+5000;
        int left=Math.round(101-icon.hotspotX), top=Math.round(101-icon.hotspotY);
        while(android.os.SystemClock.uptimeMillis()<end){
            try(Image image=reader.acquireLatestImage()){
                if(image!=null){
                    Image.Plane plane=image.getPlanes()[0];ByteBuffer bytes=plane.getBuffer();int matches=0;
                    for(int y=0;y<icon.bitmap.getHeight();y++) for(int x=0;x<icon.bitmap.getWidth();x++){
                        int color=icon.bitmap.getPixel(x,y);
                        if((color>>>24)<250)continue;
                        if(((color>>16)&255)+((color>>8)&255)+(color&255)<100 ||
                                left+x<0 || top+y<0 || left+x>=image.getWidth() || top+y>=image.getHeight())continue;
                        int offset=(top+y)*plane.getRowStride()+(left+x)*plane.getPixelStride();
                        if(Math.abs((bytes.get(offset)&255)-((color>>16)&255))<16 &&
                                Math.abs((bytes.get(offset+1)&255)-((color>>8)&255))<16 &&
                                Math.abs((bytes.get(offset+2)&255)-(color&255))<16)matches++;
                    }
                    if(matches>=3)return;
                }
            }
            Thread.sleep(25);
        }
        throw new AssertionError("Android system icon was not composed at its hotspot");
    }

    public static void main(String[] args) throws Exception {
        check(android.os.Process.myUid()==2000,"Shell UID required");
        Looper.prepareMainLooper();
        HandlerThread thread=new HandlerThread("pointer-fixture");
        thread.start();
        Handler handler=new Handler(thread.getLooper());
        ImageReader reader=ImageReader.newInstance(256,256,PixelFormat.RGBA_8888,2);
        ShellDesktop desktop=null;
        PointerOverlay pointer=null;
        try {
            desktop=new ShellDesktop("ms912x-pointer-fixture",256,256,160,60,reader.getSurface());
            android.content.Context context=ShellDesktop.context();
            android.view.Display display=context.getSystemService(DisplayManager.class).getDisplay(desktop.id());
            check(display!=null,"Fixture display available");
            SystemPointerIcon icon=SystemPointerIcon.load(context.createDisplayContext(display));
            check(SystemPointerIcon.load(null)==null,"Failed icon load selects fallback");
            System.out.println(icon==null ? "SYSTEM_ICON_UNAVAILABLE" :
                    "SYSTEM_ICON "+icon.bitmap.getWidth()+"x"+icon.bitmap.getHeight()+
                            " hotspot="+icon.hotspotX+","+icon.hotspotY);
            int stack=new FrameworkDisplayCompat().source(desktop.id()).layerStack;
            Bitmap bitmap=Bitmap.createBitmap(6,6,Bitmap.Config.ARGB_8888);
            check(SystemPointerIcon.fromBitmap(bitmap,3f,4f)==null,"Transparent system bitmap selects fallback");
            check(SystemPointerIcon.fromBitmap(bitmap,Float.NaN,4f)==null,"Invalid hotspot selects fallback");
            bitmap.eraseColor(0xffff0000);
            SystemPointerIcon synthetic=SystemPointerIcon.fromBitmap(bitmap,3f,4f);
            check(synthetic!=null,"Visible bitmap accepted");
            pointer=new PointerOverlay(handler,stack,256,256,synthetic);
            pointer.moveTo(100,100);
            waitForPixels(reader,true);
            pointer.close();pointer=null;
            Thread.sleep(200);
            pointer=new PointerOverlay(handler,context,desktop.id(),stack,256,256);
            pointer.moveTo(100,100);
            if(icon!=null)waitForSystemIcon(reader,icon);else waitForPixels(reader,false);
            pointer.close();pointer=null;
            Thread.sleep(200);
            pointer=new PointerOverlay(handler,stack,256,256,null);
            pointer.moveTo(100,100);
            waitForPixels(reader,false);
            System.out.println("PASS: cursor bitmap, hotspot positioning and fallback composition");
        } finally {
            if(pointer!=null)pointer.close();
            Thread.sleep(200);
            if(desktop!=null)desktop.close();
            reader.close();thread.quitSafely();thread.join(5000);
        }
    }
}
