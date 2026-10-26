package io.github.taglia_ti.ms912x_ng.desktop;

import android.content.Context;

/** The API 24 Mirror APK does not include Binder IPC, helper assets or shell code. */
final class ShellCaptureFactory {
    static HelperBackend createBackend(Context context) {
        return new HelperBackend() {
            public void prepare() {}
            public void refreshInventory() {}
            public boolean connected() { return false; }
            public boolean isActive() { return false; }
            public java.util.List<SourceCapability> sources() { return java.util.Collections.emptyList(); }
            public void setObserver(Runnable observer) {}
            public void shutdown(Runnable completed) { completed.run(); }
            public void close() {}
            public void start(CaptureSource.Request request, CaptureSource.Listener listener) { listener.onEnded("Desktop helper required."); }
            public void stopProducer(Runnable completed) { completed.run(); }
            public void releaseReader(Runnable completed) { completed.run(); }
            public CaptureController capture() { return null; }
            public String status() { return "Desktop helper required."; }
        };
    }
    private ShellCaptureFactory() {}
}
