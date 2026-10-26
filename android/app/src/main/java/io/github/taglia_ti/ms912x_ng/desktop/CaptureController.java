package io.github.taglia_ti.ms912x_ng.desktop;

import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.view.Surface;
import android.util.Log;

import java.io.IOException;
import java.nio.ByteBuffer;

/** Owns the app-side Surface consumer and copies each acquired image into native memory. */
final class CaptureController implements AutoCloseable {
    interface OfferListener {
        void onOfferResult(int status);
    }

    private static final int MAX_IMAGES = 3;

    private final HandlerThread thread;
    private final Handler handler;
    private final ImageReader reader;
    private final NativeCapture nativeCapture;
    private final int width;
    private final int height;
    private final OfferListener offerListener;
    private final long generation;
    private volatile ImageOptions imageOptions;
    private volatile boolean closed;
    private boolean backendLogged;
    private final long[] encodedSequence = new long[2];
    private final Object frameSignal = new Object();
    private long frameVersion;
    private long changeVersion;
    private long pendingWaitNs;

    CaptureController(int width, int height, long generation,
                      OfferListener offerListener) {
        this(width,height,generation,offerListener,ImageOptions.DEFAULT);
    }

    CaptureController(int width,int height,long generation,OfferListener offerListener,ImageOptions options){
        if (generation <= 0) {
            throw new IllegalArgumentException("generation must be positive");
        }
        this.generation = generation;
        this.offerListener = offerListener;
        this.width = width;
        this.height = height;
        imageOptions=options;
        nativeCapture = new NativeCapture(width, height, generation);
        try {
            nativeCapture.configure(options);
            reader = ImageReader.newInstance(width, height, PixelFormat.RGBA_8888, MAX_IMAGES);
        } catch (RuntimeException | Error failure) {
            nativeCapture.close();
            throw failure;
        }
        thread = new HandlerThread("ms912x-image-reader");
        try {
            thread.start();
            handler = new Handler(thread.getLooper());
            reader.setOnImageAvailableListener(this::onImageAvailable, handler);
        } catch (RuntimeException | Error failure) {
            thread.quitSafely();
            reader.close();
            nativeCapture.close();
            throw failure;
        }
    }

    Surface surface() {
        return reader.getSurface();
    }

    synchronized FrameSubmission prepareLatestFrame(ByteBuffer destination) throws IOException {
        if (closed || destination == null || !destination.isDirect()) {
            return null;
        }
        destination.clear();
        int length = nativeCapture.encodeLatest(destination, encodedSequence);
        if (length < 0) throw new IOException("Frame encoder failed: " + length);
        if (length == 0) return null;
        destination.position(0);
        destination.limit(length);
        return new FrameSubmission(destination, encodedSequence[0], this::completeBatch);
    }

    synchronized FrameSubmission preparePacedFrame(ByteBuffer destination, int refresh) throws IOException {
        if (closed || destination == null || !destination.isDirect()) return null;
        destination.clear();
        int length = nativeCapture.encodePaced(destination, encodedSequence,
                android.os.SystemClock.elapsedRealtimeNanos(), refresh);
        pendingWaitNs = encodedSequence[1];
        if (length < 0) throw new IOException("Frame encoder failed: " + length);
        if (length == 0) return null;
        destination.position(0); destination.limit(length);
        return new FrameSubmission(destination, encodedSequence[0], this::completeBatch);
    }

    synchronized void recordUsb(int bytes, long durationNs) {
        if (!closed) nativeCapture.recordUsb(bytes, durationNs);
    }

    long frameVersion() {
        synchronized (frameSignal) { return frameVersion; }
    }
    long changeVersion(){synchronized(frameSignal){return changeVersion;}}

    void awaitFrame(long observedVersion) throws InterruptedException {
        long waitNs;
        synchronized (this) { waitNs = pendingWaitNs; }
        synchronized (frameSignal) {
            if (waitNs > 0) {
                if (!closed && changeVersion == observedVersion)
                    frameSignal.wait(waitNs / 1_000_000L, (int)(waitNs % 1_000_000L));
            } else {
                while (!closed && changeVersion == observedVersion) frameSignal.wait();
            }
        }
    }

    private synchronized int completeBatch(long sequence, boolean transferred) {
        return closed ? -4 : nativeCapture.completeBatch(sequence, transferred);
    }

    synchronized int packetCapacity() {
        return closed ? 0 : nativeCapture.packetCapacity();
    }

    int width() {
        return width;
    }
    ImageOptions imageOptions(){return imageOptions;}

    int height() {
        return height;
    }

    boolean isOpen() {
        return !closed;
    }

    synchronized void invalidateBaseline() {
        if (!closed) {
            nativeCapture.invalidateBaseline();
        }
    }

    synchronized void setShowDraws(boolean enabled) {
        if (!closed) nativeCapture.setShowDraws(enabled);
    }
    synchronized void setImageOptions(ImageOptions options){
        if(closed||options.equals(imageOptions))return;
        nativeCapture.configure(options);imageOptions=options;
        synchronized(frameSignal){changeVersion++;frameSignal.notifyAll();}
    }

    private void onImageAvailable(ImageReader source) {
        Image image = null;
        try {
            image = source.acquireLatestImage();
            if (image == null || closed) {
                return;
            }

            Image.Plane[] planes = image.getPlanes();
            if (planes.length != 1) {
                return;
            }
            ByteBuffer pixels = planes[0].getBuffer();
            Rect crop = image.getCropRect();
            int status = nativeCapture.offerRgba(pixels,
                    planes[0].getRowStride(), planes[0].getPixelStride(),
                    crop.left, crop.top, crop.width(), crop.height(), generation);
            if (status == 0) {
                synchronized (frameSignal) { ++frameVersion; ++changeVersion; frameSignal.notifyAll(); }
            }
            if (status == 0 && !backendLogged) {
                Log.i("ms912x-capture", "RGBX -> UYVY backend: "
                        + nativeCapture.conversionBackend());
                backendLogged = true;
            }
            if (offerListener != null) {
                offerListener.onOfferResult(status);
            }
        } finally {
            if (image != null) {
                image.close();
            }
        }
    }

    @Override
    public synchronized void close() {
        if (closed) return;
        closed = true;
        synchronized (frameSignal) { frameSignal.notifyAll(); }
        reader.setOnImageAvailableListener(null, null);
        if (Thread.currentThread() == thread) {
            // Finish the current Image callback before releasing its backing memory.
            handler.post(() -> {
                reader.close();
                nativeCapture.close();
                thread.quitSafely();
            });
            return;
        }
        thread.quitSafely();
        boolean interrupted = false;
        while (thread.isAlive()) {
            try {
                thread.join();
            } catch (InterruptedException ignored) {
                interrupted = true;
            }
        }
        reader.close();
        nativeCapture.close();
        if (interrupted) Thread.currentThread().interrupt();
    }
}
