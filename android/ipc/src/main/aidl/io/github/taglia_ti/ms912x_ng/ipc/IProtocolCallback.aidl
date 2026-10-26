package io.github.taglia_ti.ms912x_ng.ipc;
import io.github.taglia_ti.ms912x_ng.ipc.ProtocolInfo;
oneway interface IProtocolCallback {
    void onProtocolInfo(long sessionId, long requestId, in ProtocolInfo info);
}
