package io.github.taglia_ti.ms912x_ng.desktop;

import android.util.DisplayMetrics;
import android.view.Display;

/** An Android display or an explicit request for a new screen/app picker. */
final class ScreenSource {
    static final int CREATE_NEW = -1;
    static final int APP_PICKER = -2;
    static final int TEST_SCREEN = -3;

    final int id;
    final String label;
    final float refreshHz;
    final int rotation;
    final String identity;
    final boolean helper, eligible;
    final String reason;

    ScreenSource(int id, String label, float refreshHz, int rotation) {
        this(id,label,refreshHz,rotation,"",false,id==0||id==TEST_SCREEN||id==APP_PICKER,"ADB setup required for this screen.");
    }

    ScreenSource(int id,String label,float refreshHz,int rotation,String identity,boolean helper,boolean eligible,String reason) {
        this.id = id;
        this.label = label;
        this.refreshHz = refreshHz;
        this.rotation = rotation;
        this.identity=identity;this.helper=helper;this.eligible=eligible;this.reason=reason;
    }

    static ScreenSource fromDisplay(Display display) {
        DisplayMetrics metrics = new DisplayMetrics();
        display.getRealMetrics(metrics);
        String name = display.getDisplayId() == Display.DEFAULT_DISPLAY
                ? "Phone screen" : display.getName() + " (#" + display.getDisplayId() + ")";
        return new ScreenSource(display.getDisplayId(), name
                + " · " + metrics.widthPixels + "×" + metrics.heightPixels
                + " (" + DisplayModeOption.aspectRatio(metrics.widthPixels, metrics.heightPixels) + ")",
                display.getRefreshRate(), display.getRotation(),display.getDisplayId()==0
                        ?"local:default":"android:"+display.getName()+":"+metrics.widthPixels+"x"+metrics.heightPixels,false,
                display.getDisplayId()==0,"ADB setup required for this screen.");
    }

    boolean canStart() {
        return eligible;
    }

    @Override
    public String toString() {
        return label;
    }
}
