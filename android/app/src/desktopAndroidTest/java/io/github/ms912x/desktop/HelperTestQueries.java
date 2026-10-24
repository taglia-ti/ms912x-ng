package io.github.ms912x.desktop;

import android.os.Binder;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import io.github.ms912x.ipc.*;

/** Blocking adapters belong only to the instrumentation worker, never the production UI. */
final class HelperTestQueries {
    private static final AtomicLong requests = new AtomicLong();
    static ProtocolInfo protocol(IDisplayHelper helper, long session) throws Exception {
        long request = requests.incrementAndGet();
        ArrayBlockingQueue<ProtocolInfo> results = new ArrayBlockingQueue<>(1);
        helper.getProtocolInfo(session, request, new IProtocolCallback.Stub() {
            @Override public void onProtocolInfo(long id, long req, ProtocolInfo info) {
                if (Binder.getCallingUid() != HelperProtocol.SHELL_UID) throw new SecurityException("Callback UID");
                if (id == session && req == request && info != null) results.offer(info);
            }
        });
        ProtocolInfo result = results.poll(10, TimeUnit.SECONDS);
        if (result == null) throw new AssertionError("Protocol callback timed out");
        return result;
    }
    static DisplaySourceInfo[] sources(IDisplayHelper helper, long session) throws Exception {
        long request = requests.incrementAndGet();
        ArrayBlockingQueue<SourceListResult> results = new ArrayBlockingQueue<>(1);
        helper.listDisplaySources(session, request, new IInventoryCallback.Stub() {
            @Override public void onSources(SourceListResult result) {
                if (Binder.getCallingUid() != HelperProtocol.SHELL_UID) throw new SecurityException("Callback UID");
                if (result != null && result.sessionId == session && result.requestId == request) results.offer(result);
            }
        });
        SourceListResult result = results.poll(10, TimeUnit.SECONDS);
        if (result == null || result.code != HelperProtocol.READY)
            throw new AssertionError("Source inventory failed: " + (result == null ? "timeout" : result.detail));
        return result.sources;
    }
}
