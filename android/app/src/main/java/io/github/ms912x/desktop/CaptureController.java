package io.github.ms912x.desktop;

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
    private volatile boolean closed;
    private boolean backendLogged;
    private final long[] encodedSequence = new long[1];

    CaptureController(int width, int height, long generation,
                      OfferListener offerListener) {
        if (generation <= 0) {
            throw new IllegalArgumentException("generation must be positive");
        }
        this.generation = generation;
        this.offerListener = offerListener;
        this.width = width;
        this.height = height;
        nativeCapture = new NativeCapture(width, height, generation);
        try {
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

    private synchronized int completeBatch(long sequence, boolean transferred) {
        return closed ? -4 : nativeCapture.completeBatch(sequence, transferred);
    }

    synchronized int packetCapacity() {
        return closed ? 0 : nativeCapture.packetCapacity();
    }

    int width() {
        return width;
    }

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
