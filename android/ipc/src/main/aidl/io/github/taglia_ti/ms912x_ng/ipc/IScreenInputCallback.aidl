package io.github.taglia_ti.ms912x_ng.ipc;
oneway interface IScreenInputCallback {
    void onInputError(long sessionId, long generation, long sequence, String detail);
}
