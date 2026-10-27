package io.github.taglia_ti.ms912x_ng.helper;

import android.content.Context;
import android.graphics.Bitmap;
import android.provider.Settings;
import android.view.PointerIcon;
import java.lang.reflect.Field;
import java.lang.reflect.Method;

/** Best-effort access to the framework's default cursor artwork and hotspot. */
final class SystemPointerIcon {
    private static final int MAX_DIMENSION = 1024;
    final Bitmap bitmap;
    final float hotspotX, hotspotY;

    private SystemPointerIcon(Bitmap bitmap, float hotspotX, float hotspotY) {
        this.bitmap = bitmap;
        this.hotspotX = hotspotX;
        this.hotspotY = hotspotY;
    }

    static SystemPointerIcon load(Context context) {
        try {
            PointerIcon icon = loadedIcon(context);
            Field bitmapField = PointerIcon.class.getDeclaredField("mBitmap");
            Field xField = PointerIcon.class.getDeclaredField("mHotSpotX");
            Field yField = PointerIcon.class.getDeclaredField("mHotSpotY");
            bitmapField.setAccessible(true);
            xField.setAccessible(true);
            yField.setAccessible(true);
            Bitmap bitmap = (Bitmap) bitmapField.get(icon);
            float x = xField.getFloat(icon), y = yField.getFloat(icon);
            return fromBitmap(bitmap, x, y);
        } catch (ReflectiveOperationException | RuntimeException | LinkageError unavailable) {
            return null;
        }
    }

    static SystemPointerIcon fromBitmap(Bitmap bitmap, float x, float y) {
        if (bitmap == null || bitmap.isRecycled() || bitmap.getWidth() <= 0 ||
                bitmap.getHeight() <= 0 || bitmap.getWidth() > MAX_DIMENSION ||
                bitmap.getHeight() > MAX_DIMENSION || !Float.isFinite(x) ||
                !Float.isFinite(y) || x < 0 || y < 0 ||
                x >= bitmap.getWidth() || y >= bitmap.getHeight()) return null;
        try {
            int[] row = new int[bitmap.getWidth()];
            for (int line = 0; line < bitmap.getHeight(); line++) {
                bitmap.getPixels(row, 0, row.length, 0, line, row.length, 1);
                for (int pixel : row) if ((pixel >>> 24) != 0)
                    return new SystemPointerIcon(bitmap, x, y);
            }
        } catch (RuntimeException unavailable) {
            // Hardware-only or inaccessible bitmaps cannot be drawn here.
        }
        return null;
    }

    private static PointerIcon loadedIcon(Context context) throws ReflectiveOperationException {
        boolean large = false;
        try {
            large = Settings.Secure.getInt(context.getContentResolver(),
                    "accessibility_large_pointer_icon", 0) != 0;
        } catch (RuntimeException unavailable) {
            // Some shell display contexts cannot access the settings provider.
        }
        try {
            Method method = PointerIcon.class.getDeclaredMethod("getLoadedSystemIcon",
                    Context.class, int.class, boolean.class, float.class);
            method.setAccessible(true);
            return (PointerIcon) method.invoke(null, context, PointerIcon.TYPE_ARROW, large, 1.0f);
        } catch (NoSuchMethodException older) {
            try {
                Method method = PointerIcon.class.getDeclaredMethod("getLoadedSystemIcon",
                        Context.class, int.class, boolean.class);
                method.setAccessible(true);
                return (PointerIcon) method.invoke(null, context, PointerIcon.TYPE_ARROW, large);
            } catch (NoSuchMethodException legacy) {
                PointerIcon icon = PointerIcon.getSystemIcon(context, PointerIcon.TYPE_ARROW);
                Method method = PointerIcon.class.getDeclaredMethod("load", Context.class);
                method.setAccessible(true);
                return (PointerIcon) method.invoke(icon, context);
            }
        }
    }
}
