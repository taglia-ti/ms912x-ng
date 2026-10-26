package io.github.ms912x.ipc;
import android.view.Surface;
import io.github.ms912x.ipc.ProtocolInfo;
import io.github.ms912x.ipc.DisplaySourceInfo;
import io.github.ms912x.ipc.CaptureSpec;
import io.github.ms912x.ipc.IHelperCallback;
import io.github.ms912x.ipc.IInventoryCallback;
import io.github.ms912x.ipc.IProtocolCallback;
import io.github.ms912x.ipc.ScreenInputCommand;
import io.github.ms912x.ipc.IScreenInputCallback;
import io.github.ms912x.ipc.IMouseSharingCallback;
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
