package io.github.ms912x.desktop;

import android.app.Activity;

/** The API 24 Mirror APK does not include Binder IPC, helper assets or shell code. */
final class ShellCaptureFactory {
    static ShellCaptureSupport create(Activity activity, MainScreenView screen) {
        return new ShellCaptureSupport() {
            public void prepare() {}
            public void preview(ScreenSource source, DisplayModeOption mode, int rotation) {}
            public void stop() {}
            public void onStop() {}
            public boolean isActive() { return false; }
            public void close() {}
        };
    }
    private ShellCaptureFactory() {}
}
