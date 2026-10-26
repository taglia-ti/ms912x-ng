package io.github.ms912x.ipc;
import io.github.ms912x.ipc.MouseSharingResult;
oneway interface IMouseSharingCallback {
    void onMouseSharing(in MouseSharingResult result);
}
