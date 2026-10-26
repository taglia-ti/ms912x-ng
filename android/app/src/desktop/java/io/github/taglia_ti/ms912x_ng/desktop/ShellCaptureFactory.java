package io.github.taglia_ti.ms912x_ng.desktop;

import android.content.Context;

/** Desktop-only shell backend, constructed once by the session service. */
final class ShellCaptureFactory {
    static HelperBackend createBackend(Context context) { return new HelperCaptureSource(context); }
    private ShellCaptureFactory() {}
}
