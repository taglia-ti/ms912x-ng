package io.github.ms912x.desktop;

/** Capture-only helper prototype; normal capture controls stay with ScreencastController. */
interface ShellCaptureSupport extends AutoCloseable {
    void prepare();
    void preview(ScreenSource source, DisplayModeOption mode, int rotation);
    void stop();
    void onStop();
    boolean isActive();
    @Override void close();
}
