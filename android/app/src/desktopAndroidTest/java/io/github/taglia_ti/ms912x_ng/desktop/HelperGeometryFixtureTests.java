package io.github.taglia_ti.ms912x_ng.desktop;

import android.app.Instrumentation;
import android.graphics.Bitmap;
import android.graphics.PixelFormat;
import android.media.Image;
import android.media.ImageReader;
import android.os.Binder;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;

import java.io.FileOutputStream;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

import org.json.JSONArray;
import org.json.JSONObject;

import io.github.taglia_ti.ms912x_ng.ipc.CaptureResult;
import io.github.taglia_ti.ms912x_ng.ipc.CaptureSpec;
import io.github.taglia_ti.ms912x_ng.ipc.DisplaySourceInfo;
import io.github.taglia_ti.ms912x_ng.ipc.HelperProtocol;
import io.github.taglia_ti.ms912x_ng.ipc.IDisplayHelper;
import io.github.taglia_ti.ms912x_ng.ipc.IHelperCallback;

/** Optional live shell test: asserts received RGBA pixels before native conversion. */
public final class HelperGeometryFixtureTests {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    public static void run(Instrumentation test, Bundle arguments) throws Exception {
        String digest;
        try (java.io.InputStream input = test.getTargetContext().getAssets().open("helper.sha256")) {
            byte[] text = new byte[65];
            int count = input.read(text);
            digest = new String(text, 0, count, StandardCharsets.US_ASCII).trim();
        }
        AtomicReference<IDisplayHelper> client = new AtomicReference<>();
        AtomicReference<IBinder> owner = new AtomicReference<>();
        CountDownLatch enrolled = new CountDownLatch(1), died = new CountDownLatch(1);
        long session = HelperEnrollment.prepare(test.getTargetContext(), digest, (id, binder, life) -> {
            client.set(IDisplayHelper.Stub.asInterface(binder)); owner.set(life); enrolled.countDown();
        });
        write(test, "helper-geometry-ready.json", new JSONObject().put("ready", true));
        ScheduledExecutorService heartbeat = Executors.newSingleThreadScheduledExecutor();
        HandlerThread images = new HandlerThread("helper-geometry-images");
        images.start();
        JSONObject report = new JSONObject().put("passed", false).put("cases", new JSONArray());
        try {
            check(enrolled.await(30, TimeUnit.SECONDS), "ADB helper did not enroll");
            IDisplayHelper helper = client.get();
            helper.asBinder().linkToDeath(died::countDown, 0);
            io.github.taglia_ti.ms912x_ng.ipc.ProtocolInfo protocol = HelperTestQueries.protocol(helper, session);
            check(protocol.uid == 2000 && digest.equals(protocol.buildDigest), "Helper identity");
            heartbeat.scheduleWithFixedDelay(() -> {
                try { helper.heartbeat(session); } catch (Exception failure) { died.countDown(); }
            }, 0, 2, TimeUnit.SECONDS);
            int sourceId = Integer.parseInt(arguments.getString("sourceId"));
            DisplaySourceInfo source = source(helper, session, sourceId);
            report.put("source_id", sourceId).put("source_identity", source.identity)
                    .put("source_width", source.width).put("source_height", source.height);
            int[][] outputs = {{1920, 1080}, {640, 480}, {1080, 1920}, {1000, 1000}};
            int sequence = 0;
            for (String value : arguments.getString("rotations", "0").split(",")) {
                int rotation = Integer.parseInt(value);
                for (int[] output : outputs) {
                    long request = ++sequence * 2L;
                    CaptureSpec spec = new CaptureSpec(session, request, request, sourceId, source.identity,
                            output[0], output[1], 0, rotation);
                    JSONObject result = capture(test, helper, spec, source, new Handler(images.getLooper()), false);
                    report.getJSONArray("cases").put(result);
                    write(test, "helper-geometry.json", report);
                    check(source(helper, session, sourceId).identity.equals(source.identity), "Original source was replaced");
                }
            }
            if (arguments.getString("sourceChanges", "false").equals("true")) {
                check(source.rotation == 0, "Source-change fixture must start at rotation 0");
                for (int rotation : new int[]{0, 1}) {
                    long request = ++sequence * 2L;
                    CaptureSpec spec = new CaptureSpec(session, request, request, sourceId, source.identity, 640, 480, 0, rotation);
                    JSONObject result = capture(test, helper, spec, source, new Handler(images.getLooper()), true);
                    report.getJSONArray("cases").put(result);
                    write(test, "helper-geometry.json", report);
                }
            }
            report.put("passed", true);
        } catch (Throwable failure) {
            report.put("failure", failure.toString());
            throw failure;
        } finally {
            HelperEnrollment.cancel(session);
            heartbeat.shutdownNow();
            if (client.get() != null) {
                try { client.get().stopSession(session); } catch (Exception ignored) { /* Death is checked below. */ }
                check(died.await(15, TimeUnit.SECONDS), "Helper failed to exit");
            }
            owner.set(null);
            images.quitSafely();
            write(test, "helper-geometry.json", report);
        }
    }

    private static DisplaySourceInfo source(IDisplayHelper helper, long session, int id) throws Exception {
        for (DisplaySourceInfo source : HelperTestQueries.sources(helper, session)) if (source.displayId == id) return source;
        throw new AssertionError("Selected source disappeared");
    }

    private static JSONObject capture(Instrumentation test, IDisplayHelper helper, CaptureSpec spec,
                                      DisplaySourceInfo source, Handler handler, boolean sourceChanges) throws Exception {
        ArrayBlockingQueue<CaptureResult> callbacks = new ArrayBlockingQueue<>(8);
        ArrayBlockingQueue<Image> frames = new ArrayBlockingQueue<>(1);
        ImageReader reader = ImageReader.newInstance(spec.outputWidth, spec.outputHeight, PixelFormat.RGBA_8888, 3);
        reader.setOnImageAvailableListener(r -> {
            Image image = r.acquireLatestImage();
            if (image == null) return;
            Image old = frames.poll();
            if (old != null) old.close();
            if (!frames.offer(image)) image.close();
        }, handler);
        IHelperCallback listener = new IHelperCallback.Stub() {
                @Override public void onResult(CaptureResult result) {
                    check(Binder.getCallingUid() == HelperProtocol.SHELL_UID, "Unexpected callback UID");
                    callbacks.offer(result);
                }
        };
        try {
            helper.attachCapture(spec, reader.getSurface(), listener);
            CaptureResult ready = callbacks.poll(10, TimeUnit.SECONDS);
            check(ready != null && ready.code == HelperProtocol.READY,
                    "Capture did not start: " + (ready == null ? "timeout" : ready.detail));
            verifyMetadata(ready, spec, source);
            verifyFrames(test, frames, source, spec.rotation);
            JSONObject result = new JSONObject().put("width", spec.outputWidth).put("height", spec.outputHeight)
                    .put("rotation", spec.rotation).put("backend", ready.backend).put("passed", true);
            if (sourceChanges) {
                JSONArray changes = new JSONArray();
                String[] steps = {"resize", "rotate", "restore"};
                int[][] expected = {{1600,1200,0},{1200,1600,1},{source.width,source.height,0}};
                for (int step=0;step<steps.length;++step) {
                    write(test, "helper-geometry-step.json", new JSONObject().put("step", steps[step])
                            .put("token", spec.requestId*10+step).put("source_id", spec.sourceId));
                    long deadline=android.os.SystemClock.elapsedRealtime()+20000;
                    CaptureResult updated;
                    do {
                        updated=callbacks.poll(2,TimeUnit.SECONDS);
                        check(android.os.SystemClock.elapsedRealtime()<deadline, "No matching source geometry callback for "+steps[step]);
                    } while(updated==null || updated.code!=HelperProtocol.GEOMETRY_CHANGED || updated.source==null
                            || updated.source.width!=expected[step][0] || updated.source.height!=expected[step][1]
                            || updated.source.rotation!=expected[step][2]);
                    verifyMetadata(updated,spec,updated.source);
                    verifyFrames(test,frames,updated.source,spec.rotation);
                    changes.put(new JSONObject().put("step",steps[step]).put("source_width",updated.source.width)
                            .put("source_height",updated.source.height).put("source_rotation",updated.source.rotation)
                            .put("mirror_id",updated.mirrorId).put("content_bounds",updated.contentBounds.toShortString()));
                }
                result.put("source_changes",changes);
            }
            return result;
        } finally {
            helper.releaseCapture(spec.sessionId, spec.requestId + 1, spec.generation, listener);
            CaptureResult released = callbacks.poll(10, TimeUnit.SECONDS);
            boolean releaseAcknowledged = released != null && released.code == HelperProtocol.RELEASED
                    && released.generation == spec.generation && released.requestId == spec.requestId + 1;
            if (!releaseAcknowledged) {
                CountDownLatch dead = new CountDownLatch(1);
                try { helper.asBinder().linkToDeath(dead::countDown, 0); helper.stopSession(spec.sessionId); }
                catch (Exception ignored) { dead.countDown(); }
                check(dead.await(15, TimeUnit.SECONDS), "Unsafe reader release: helper still alive");
            }
            CountDownLatch closed = new CountDownLatch(1);
            handler.post(() -> {
                reader.setOnImageAvailableListener(null, null);
                Image pending;
                while ((pending = frames.poll()) != null) pending.close();
                reader.close();
                closed.countDown();
            });
            check(closed.await(10, TimeUnit.SECONDS), "ImageReader cleanup timed out");
            check(releaseAcknowledged, "Mirror release was not acknowledged");
        }
    }

    private static void verifyMetadata(CaptureResult result,CaptureSpec spec,DisplaySourceInfo source) {
        check(result.source!=null && result.source.displayId==spec.sourceId && result.source.identity.equals(spec.sourceIdentity),
                "Geometry callback has the wrong source");
        check(result.outputRotation==spec.rotation && result.generation==spec.generation && result.requestId==spec.requestId,
                "Geometry callback has the wrong request or rotation");
        check(expectedBounds(spec.outputWidth,spec.outputHeight,source.width,source.height,spec.rotation).equals(result.contentBounds),
                "Helper reported incorrect content bounds: "+result.contentBounds);
    }

    private static void verifyFrames(Instrumentation test, ArrayBlockingQueue<Image> frames,
                                     DisplaySourceInfo source, int rotation) throws Exception {
        long deadline = android.os.SystemClock.elapsedRealtime() + 8000;
        while (true) {
            Image image = frames.poll(3, TimeUnit.SECONDS);
            check(image != null, "No captured image");
            try {
                try { verify(image, source.width, source.height, rotation); return; }
                catch (AssertionError failure) {
                    if (android.os.SystemClock.elapsedRealtime() >= deadline) {
                        saveImage(test, image, "helper-geometry-failure.png");
                        throw failure;
                    }
                }
            } finally { image.close(); }
        }
    }

    private static void verify(Image image, int sourceWidth, int sourceHeight, int rotation) {
        int w = image.getWidth(), h = image.getHeight();
        android.graphics.Rect bounds=expectedBounds(w,h,sourceWidth,sourceHeight,rotation);
        int left=bounds.left,top=bounds.top,cw=bounds.width(),ch=bounds.height();
        // The independent oracle samples solid regions far from interpolation boundaries.
        double[][] points = {{.08,.08}, {.92,.08}, {.92,.92}, {.08,.92}, {.32,.5}, {.5,.5}, {.68,.5}};
        int[] colors = {0xff0000, 0x00ff00, 0x0000ff, 0xffff00, 0xff0000, 0x00ff00, 0x0000ff};
        for (int i = 0; i < points.length; ++i) {
            double x = points[i][0], y = points[i][1], ox = x, oy = y;
            if (rotation == 1) { ox = 1-y; oy = x; }
            if (rotation == 2) { ox = 1-x; oy = 1-y; }
            if (rotation == 3) { ox = y; oy = 1-x; }
            pixel(image, left + (int)(ox * cw), top + (int)(oy * ch), colors[i], "source sample " + i);
        }
        if (left > 2) {
            pixel(image, left/2, h/2, 0, "left black bar");
            pixel(image, w-1-left/2, h/2, 0, "right black bar");
        }
        if (top > 2) {
            pixel(image, w/2, top/2, 0, "top black bar");
            pixel(image, w/2, h-1-top/2, 0, "bottom black bar");
        }
        int border = Math.max(1, (int)(2.0 * cw / ((rotation % 2 == 0) ? sourceWidth : sourceHeight)));
        pixel(image, left+cw/2, top+border, 0xffffff, "top edge");
        pixel(image, left+cw/2, top+ch-1-border, 0xffffff, "bottom edge");
        pixel(image, left+border, top+ch/2, 0xffffff, "left edge");
        pixel(image, left+cw-1-border, top+ch/2, 0xffffff, "right edge");
    }

    private static android.graphics.Rect expectedBounds(int w,int h,int sourceWidth,int sourceHeight,int rotation) {
        double aspect = (rotation % 2 == 0) ? (double)sourceWidth / sourceHeight : (double)sourceHeight / sourceWidth;
        int cw = Math.min(w, (int)Math.round(h * aspect)), ch = Math.min(h, (int)Math.round(w / aspect));
        int left = (w - cw) / 2, top = (h - ch) / 2;
        return new android.graphics.Rect(left,top,left+cw,top+ch);
    }

    private static int rgb(Image image, int x, int y) {
        Image.Plane p = image.getPlanes()[0];
        ByteBuffer b = p.getBuffer();
        int offset = y*p.getRowStride() + x*p.getPixelStride();
        return ((b.get(offset)&255)<<16) | ((b.get(offset+1)&255)<<8) | (b.get(offset+2)&255);
    }

    private static void pixel(Image image, int x, int y, int expected, String name) {
        int actual = rgb(image,x,y);
        for (int shift : new int[]{16,8,0}) {
            check(Math.abs(((actual>>shift)&255)-((expected>>shift)&255)) <= 12,
                    name+" at "+x+","+y+": expected "+Integer.toHexString(expected)+", got "+Integer.toHexString(actual));
        }
    }

    private static void saveImage(Instrumentation test, Image image, String name) throws Exception {
        int w=image.getWidth(),h=image.getHeight();
        int[] pixels = new int[w*h];
        for (int y=0;y<h;++y) for(int x=0;x<w;++x) pixels[y*w+x]=0xff000000|rgb(image,x,y);
        Bitmap bitmap=Bitmap.createBitmap(pixels,w,h,Bitmap.Config.ARGB_8888);
        try(FileOutputStream file=test.getTargetContext().openFileOutput(name,0)) {
            bitmap.compress(Bitmap.CompressFormat.PNG,100,file);
        } finally { bitmap.recycle(); }
    }

    private static void write(Instrumentation test, String name, JSONObject object) throws Exception {
        try(FileOutputStream file=test.getTargetContext().openFileOutput(name,0)) {
            file.write(object.toString(2).getBytes(StandardCharsets.UTF_8));
        }
    }
}
