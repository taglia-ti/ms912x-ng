package io.github.ms912x.desktop;

/** Diagnostic controls of the helper source; the session service owns its lifetime. */
interface ShellCaptureSupport extends AutoCloseable {
    void prepare();
    void preview(ScreenSource source, DisplayModeOption mode, int rotation);
    void stop();
    void onStop();
    boolean isActive();
    @Override void close();
}
