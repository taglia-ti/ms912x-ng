package io.github.ms912x.desktop;

import android.util.DisplayMetrics;
import android.view.Display;

/** An Android display or an explicit request for a new screen/app picker. */
final class ScreenSource {
    static final int CREATE_NEW = -1;
    static final int APP_PICKER = -2;

    final int id;
    final String label;
    final float refreshHz;
    final int rotation;

    ScreenSource(int id, String label, float refreshHz, int rotation) {
        this.id = id;
        this.label = label;
        this.refreshHz = refreshHz;
        this.rotation = rotation;
    }

    static ScreenSource fromDisplay(Display display) {
        DisplayMetrics metrics = new DisplayMetrics();
        display.getRealMetrics(metrics);
        String name = display.getDisplayId() == Display.DEFAULT_DISPLAY
                ? "Phone screen" : display.getName() + " (#" + display.getDisplayId() + ")";
        return new ScreenSource(display.getDisplayId(), name
                + " · " + metrics.widthPixels + "×" + metrics.heightPixels
                + " (" + DisplayModeOption.aspectRatio(metrics.widthPixels, metrics.heightPixels) + ")",
                display.getRefreshRate(), display.getRotation());
    }

    boolean canStart() {
        return id == Display.DEFAULT_DISPLAY || id == CREATE_NEW || id == APP_PICKER;
    }

    @Override
    public String toString() {
        return label;
    }
}
