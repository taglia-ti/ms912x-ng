package io.github.ms912x.desktop;

import android.graphics.ImageFormat;
import android.graphics.Rect;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;
import android.view.Surface;

import java.nio.ByteBuffer;

/** Owns the app-side Surface consumer and copies each acquired image into native memory. */
final class CaptureController implements AutoCloseable {
    private static final int MAX_IMAGES = 3;
    private static final long MIN_OFFER_INTERVAL_NS = 100_000_000L;

    private final HandlerThread thread;
    private final Handler handler;
    private final ImageReader reader;
    private final NativeCapture nativeCapture;
    private final long generation;
    private volatile boolean closed;
    private long lastOfferNs;

    CaptureController(int width, int height, long generation) {
        if (generation <= 0) {
            throw new IllegalArgumentException("generation must be positive");
        }
        this.generation = generation;
        nativeCapture = new NativeCapture(width, height, generation);
        thread = new HandlerThread("ms912x-image-reader");
        thread.start();
        handler = new Handler(thread.getLooper());
        reader = ImageReader.newInstance(width, height, ImageFormat.RGBA_8888, MAX_IMAGES);
        reader.setOnImageAvailableListener(this::onImageAvailable, handler);
    }

    Surface surface() {
        return reader.getSurface();
    }

    private void onImageAvailable(ImageReader source) {
        Image image = null;
        try {
            image = source.acquireLatestImage();
            if (image == null || closed) {
                return;
            }

            long now = SystemClock.elapsedRealtimeNanos();
            if (now - lastOfferNs < MIN_OFFER_INTERVAL_NS) {
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
                lastOfferNs = now;
            }
        } finally {
            if (image != null) {
                image.close();
            }
        }
    }

    @Override
    public void close() {
        closed = true;
        reader.setOnImageAvailableListener(null, null);
        reader.close();
        thread.quitSafely();
        try {
            thread.join();
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
        }
        nativeCapture.close();
    }
}
