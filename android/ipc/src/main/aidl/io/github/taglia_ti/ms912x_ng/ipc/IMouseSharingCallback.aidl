package io.github.taglia_ti.ms912x_ng.ipc;
import io.github.taglia_ti.ms912x_ng.ipc.MouseSharingResult;
oneway interface IMouseSharingCallback {
    void onMouseSharing(in MouseSharingResult result);
}
