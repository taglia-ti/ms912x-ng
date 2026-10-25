package io.github.ms912x.ipc;
oneway interface IScreenInputCallback {
    void onInputError(long sessionId, long generation, long sequence, String detail);
}
