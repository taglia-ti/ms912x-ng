package io.github.ms912x.desktop;

import android.app.Activity;

final class ShellCaptureFactory {
    static ShellCaptureSupport create(Activity activity, MainScreenView screen) {
        return new HelperCaptureController(activity, screen);
    }
    private ShellCaptureFactory() {}
}
