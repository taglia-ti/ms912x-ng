package io.github.taglia_ti.ms912x_ng.ipc;
import android.view.Surface;
import io.github.taglia_ti.ms912x_ng.ipc.ProtocolInfo;
import io.github.taglia_ti.ms912x_ng.ipc.DisplaySourceInfo;
import io.github.taglia_ti.ms912x_ng.ipc.CaptureSpec;
import io.github.taglia_ti.ms912x_ng.ipc.IHelperCallback;
import io.github.taglia_ti.ms912x_ng.ipc.IInventoryCallback;
import io.github.taglia_ti.ms912x_ng.ipc.IProtocolCallback;
import io.github.taglia_ti.ms912x_ng.ipc.ScreenInputCommand;
import io.github.taglia_ti.ms912x_ng.ipc.IScreenInputCallback;
import io.github.taglia_ti.ms912x_ng.ipc.IMouseSharingCallback;
oneway interface IDisplayHelper {
    void getProtocolInfo(long sessionId, long requestId, IProtocolCallback callback);
    void listDisplaySources(long sessionId, long requestId, IInventoryCallback callback);
    void attachCapture(in CaptureSpec spec, in Surface surface, IHelperCallback callback);
    void releaseCapture(long sessionId, long requestId, long generation, IHelperCallback callback);
    void launchApp(long sessionId, long requestId, long generation, String component, IHelperCallback callback);
    void sendInput(in ScreenInputCommand command, IScreenInputCallback callback);
    void queryMouseSharing(long sessionId, long requestId, long generation, IMouseSharingCallback callback);
    void setMouseSharing(long sessionId, long requestId, long generation, int side, String deviceIdentity, IMouseSharingCallback callback);
    void heartbeat(long sessionId);
    void stopSession(long sessionId);
}
