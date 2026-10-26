package io.github.taglia_ti.ms912x_ng.helper;

import io.github.taglia_ti.ms912x_ng.ipc.DisplaySourceInfo;

/** Owns a helper-created mirror only; source display lifetime belongs to its original owner. */
interface DisplayCaptureBackend extends AutoCloseable {
    int displayId();
    String name();
    void resize(int width, int height, DisplaySourceInfo source) throws Exception;
    @Override void close();
}
