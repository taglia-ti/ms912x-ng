package io.github.ms912x.desktop;

import android.content.Context;

/** Desktop-only shell backend, constructed once by the session service. */
final class ShellCaptureFactory {
    static HelperBackend createBackend(Context context) { return new HelperCaptureSource(context); }
    private ShellCaptureFactory() {}
}
