package io.github.ms912x.desktop;

import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.hardware.display.VirtualDisplayConfig;
import android.os.Build;
import android.view.Surface;

/** Keeps public virtual-display controls behind their Android API availability. */
final class VirtualScreenOptions {
    static VirtualDisplay createOwnScreen(DisplayManager manager, int width, int height,
                                         int densityDpi, Surface surface, float refreshHz) {
        int flags = DisplayManager.VIRTUAL_DISPLAY_FLAG_OWN_CONTENT_ONLY
                | DisplayManager.VIRTUAL_DISPLAY_FLAG_PRESENTATION;
        if (Build.VERSION.SDK_INT >= 34) {
            VirtualDisplayConfig config = new VirtualDisplayConfig.Builder(
                    "ms912x-test-screen", width, height, densityDpi)
                    .setFlags(flags).setSurface(surface)
                    .setRequestedRefreshRate(refreshHz).build();
            return manager.createVirtualDisplay(config);
        }
        return manager.createVirtualDisplay("ms912x-test-screen",
                width, height, densityDpi, surface, flags);
    }

    static void applyRotation(VirtualDisplay display, int rotation) {
        if (rotation < Surface.ROTATION_0 || rotation > Surface.ROTATION_270) {
            throw new IllegalArgumentException("Unsupported screen rotation");
        }
        if (Build.VERSION.SDK_INT >= 36) {
            display.setRotation(rotation);
        } else if (rotation != Surface.ROTATION_0) {
            throw new IllegalArgumentException("Virtual-screen rotation needs Android 16+");
        }
    }

    private VirtualScreenOptions() {}
}
