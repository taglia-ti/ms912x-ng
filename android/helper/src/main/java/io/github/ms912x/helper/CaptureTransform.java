package io.github.ms912x.helper;

import android.graphics.Rect;

/** Fit logical source pixels into fixed output pixels, with clockwise output rotation. */
final class CaptureTransform {
    final int outputWidth, outputHeight, rotation;
    CaptureTransform(int outputWidth, int outputHeight, int rotation) {
        if (rotation < 0 || rotation > 3) throw new IllegalArgumentException("Invalid output rotation");
        this.outputWidth = outputWidth; this.outputHeight = outputHeight; this.rotation = rotation;
    }
    Rect bounds(int sourceWidth, int sourceHeight) {
        if (sourceWidth <= 0 || sourceHeight <= 0) throw new IllegalArgumentException("Invalid source dimensions");
        int width = rotation % 2 == 0 ? sourceWidth : sourceHeight;
        int height = rotation % 2 == 0 ? sourceHeight : sourceWidth;
        double scale = Math.min((double)outputWidth / width, (double)outputHeight / height);
        int w = Math.max(1, Math.min(outputWidth, (int)Math.round(width * scale)));
        int h = Math.max(1, Math.min(outputHeight, (int)Math.round(height * scale)));
        int left = (outputWidth - w) / 2, top = (outputHeight - h) / 2;
        return new Rect(left, top, left + w, top + h);
    }
}
