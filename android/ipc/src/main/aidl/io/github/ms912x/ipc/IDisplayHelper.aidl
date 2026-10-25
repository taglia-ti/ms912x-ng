package io.github.ms912x.ipc;
import android.view.Surface;
import io.github.ms912x.ipc.ProtocolInfo;
import io.github.ms912x.ipc.DisplaySourceInfo;
import io.github.ms912x.ipc.CaptureSpec;
import io.github.ms912x.ipc.IHelperCallback;
import io.github.ms912x.ipc.IInventoryCallback;
import io.github.ms912x.ipc.IProtocolCallback;
oneway interface IDisplayHelper {
    void getProtocolInfo(long sessionId, long requestId, IProtocolCallback callback);
    void listDisplaySources(long sessionId, long requestId, IInventoryCallback callback);
    void attachCapture(in CaptureSpec spec, in Surface surface, IHelperCallback callback);
    void releaseCapture(long sessionId, long requestId, long generation, IHelperCallback callback);
    void launchApp(long sessionId, long requestId, long generation, String component, IHelperCallback callback);
    void heartbeat(long sessionId);
    void stopSession(long sessionId);
}
