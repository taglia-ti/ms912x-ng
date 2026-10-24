package io.github.ms912x.ipc;
import io.github.ms912x.ipc.ProtocolInfo;
oneway interface IProtocolCallback {
    void onProtocolInfo(long sessionId, long requestId, in ProtocolInfo info);
}
