package io.github.taglia_ti.ms912x_ng.helper;

import android.graphics.Rect;

/** Fit logical source pixels into fixed output pixels, with clockwise output rotation. */
final class CaptureTransform {
    final int outputWidth, outputHeight, rotation;
    CaptureTransform(int outputWidth, int outputHeight, int rotation) {
        if (rotation < 0 || rotation > 3) throw new IllegalArgumentException("Invalid output rotation");
        this.outputWidth = outputWidth; this.outputHeight = outputHeight; this.rotation = rotation;
    }
    Rect bounds(int sourceWidth, int sourceHeight) {
        DisplayGeometry geometry=new DisplayGeometry(sourceWidth,sourceHeight,outputWidth,outputHeight,rotation);
        return new Rect(geometry.left,geometry.top,geometry.left+geometry.width,geometry.top+geometry.height);
    }
}
