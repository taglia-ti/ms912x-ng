package io.github.ms912x.desktop;

import android.app.Activity;
import android.graphics.Rect;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.RemoteException;
import android.os.SystemClock;
import android.util.Log;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.zip.CRC32;
import java.util.function.Consumer;

import io.github.ms912x.ipc.CaptureResult;
import io.github.ms912x.ipc.CaptureSpec;
import io.github.ms912x.ipc.DisplaySourceInfo;
import io.github.ms912x.ipc.HelperProtocol;
import io.github.ms912x.ipc.IDisplayHelper;
import io.github.ms912x.ipc.IHelperCallback;
import io.github.ms912x.ipc.ProtocolInfo;
import io.github.ms912x.ipc.IProtocolCallback;
import io.github.ms912x.ipc.IInventoryCallback;
import io.github.ms912x.ipc.SourceListResult;

/** P0 visible-Activity preview. Pixels use CaptureController; Binder transports Surface handles only. */
final class HelperCaptureController implements ShellCaptureSupport {
    private final Activity activity;
    private final MainScreenView screen;
    private final int callbackUid;
    private final long requestTimeoutMs, firstFrameTimeoutMs;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ScheduledExecutorService work = Executors.newSingleThreadScheduledExecutor();
    private final ScheduledExecutorService keepalive = Executors.newSingleThreadScheduledExecutor();
    private final AtomicLong accepted = new AtomicLong(), rejected = new AtomicLong();
    private IDisplayHelper helper;
    private IBinder owner;
    private IBinder helperBinder;
    private IBinder.DeathRecipient helperDeath;
    private long sessionId, generation, requests;
    private String digest;
    private CaptureSpec spec;
    private DisplaySourceInfo activeSource;
    private CaptureController capture;
    private ScheduledFuture<?> heartbeat, diagnostics;
    private boolean preparing, ready, stopping, disconnectAfterRelease, destroyed, verified;
    private String backend = "";
    private long batches, changes, previousCrc = -1;
    private long lastStatusMs;
    private int mirrorId = -1;
    private boolean releaseAcknowledged;
    private Rect contentBounds;
    private Runnable startupTimeout, releaseTimeout, protocolTimeout, sourceTimeout;
    private long protocolRequest, sourceRequest;
    private boolean cleanupPending;

    HelperCaptureController(Activity activity, MainScreenView screen) {
        this(activity, screen, HelperProtocol.SHELL_UID, HelperProtocol.REQUEST_TIMEOUT_MS, 10_000);
    }

    /** Internal transport/timing boundary for deterministic instrumentation fixtures. */
    HelperCaptureController(Activity activity, MainScreenView screen, int callbackUid, long requestTimeoutMs, long firstFrameTimeoutMs) {
        this.activity = activity;
        this.screen = screen;
        this.callbackUid = callbackUid;
        this.requestTimeoutMs = requestTimeoutMs;
        this.firstFrameTimeoutMs = firstFrameTimeoutMs;
        renderControls();
    }

    @Override public void prepare() {
        if (destroyed || isActive()) { status("Stop the current helper preview first."); return; }
        disconnect();
        try (java.io.InputStream input = activity.getAssets().open("helper.sha256")) {
            java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
            byte[] chunk = new byte[128];
            int count;
            while ((count = input.read(chunk)) != -1) {
                if (bytes.size() + count > 128) throw new IllegalStateException("Oversized helper digest");
                bytes.write(chunk, 0, count);
            }
            digest = new String(bytes.toByteArray(), StandardCharsets.US_ASCII).trim();
            if (!digest.matches("[0-9a-f]{64}")) throw new IllegalStateException("Invalid bundled helper digest");
            sessionId = HelperEnrollment.prepare(activity, digest, (id, binder, life) ->
                    main.post(() -> connect(id, binder, life)));
            status("ADB helper enrollment ready for 60 seconds. Run the PC helper launcher, then preview the selected display.");
        } catch (Exception failure) { status("Cannot prepare helper: " + failure.getClass().getSimpleName()); }
        renderControls();
    }

    private void connect(long id, IBinder binder, IBinder life) {
        if (id != sessionId || destroyed) return;
        owner = life;
        IDisplayHelper client = IDisplayHelper.Stub.asInterface(binder);
        helper = client;
        helperBinder = binder;
        helperDeath = () -> main.post(() -> died(id, binder));
        try { binder.linkToDeath(helperDeath, 0); }
        catch (RemoteException failure) { transportFailure(id, binder); return; }
        long request = protocolRequest = ++requests;
        protocolTimeout = () -> {
            if (id == sessionId && protocolRequest == request) {
                status("Helper verification timed out; waiting for helper exit.");
                abortHelper();
            }
        };
        main.postDelayed(protocolTimeout, requestTimeoutMs);
        try {
            client.getProtocolInfo(id, request, new IProtocolCallback.Stub() {
                @Override public void onProtocolInfo(long returnedSession, long returnedRequest, ProtocolInfo protocol) {
                    requireShellCaller();
                    main.post(() -> {
                        if (destroyed || id != sessionId || helper != client || protocolRequest != request
                                || returnedSession != id || returnedRequest != request) return;
                        protocolRequest = 0;
                        remove(protocolTimeout);
                        if (protocol == null || protocol.uid != HelperProtocol.SHELL_UID || protocol.version != HelperProtocol.VERSION
                                || !digest.equals(protocol.buildDigest)) {
                            status("Helper verification failed; waiting for helper exit.");
                            abortHelper();
                            return;
                        }
                        heartbeat = keepalive.scheduleWithFixedDelay(() -> {
                            try { client.heartbeat(id); }
                            catch (Exception failure) { main.post(() -> transportFailure(id, binder)); }
                        }, 0, HelperProtocol.HEARTBEAT_MS, TimeUnit.MILLISECONDS);
                        querySources(id, client, sources -> {
                            verified = true;
                            StringBuilder inventory = new StringBuilder("Helper connected · UID 2000 · API ")
                                    .append(protocol.api).append("\nSources:");
                            for (DisplaySourceInfo source : sources) inventory.append("\n#").append(source.displayId)
                                    .append(" ").append(source.name).append(" · ").append(source.width).append("×").append(source.height);
                            status(inventory.toString());
                            renderControls();
                        });
                    });
                }
            });
        } catch (Exception failure) { transportFailure(id, binder); }
    }

    private void requireShellCaller() {
        if (Binder.getCallingUid() != callbackUid) throw new SecurityException("Unexpected helper callback UID");
    }

    private void querySources(long id, IDisplayHelper client, Consumer<DisplaySourceInfo[]> received) {
        cancelQuery();
        long request = sourceRequest = ++requests;
        sourceTimeout = () -> {
            if (id == sessionId && sourceRequest == request) {
                status("Source inventory timed out; waiting for helper exit.");
                abortHelper();
            }
        };
        main.postDelayed(sourceTimeout, requestTimeoutMs);
        try {
            client.listDisplaySources(id, request, new IInventoryCallback.Stub() {
                @Override public void onSources(SourceListResult result) {
                    requireShellCaller();
                    main.post(() -> {
                        if (destroyed || stopping || id != sessionId || helper != client || sourceRequest != request
                                || result == null || result.sessionId != id || result.requestId != request) return;
                        cancelQuery();
                        if (result.code != HelperProtocol.READY) {
                            preparing = false;
                            status("Cannot list source displays: " + result.detail);
                            renderControls();
                            return;
                        }
                        received.accept(result.sources);
                    });
                }
            });
        } catch (Exception failure) { transportFailure(id, client.asBinder()); }
    }

    private void cancelQuery() { sourceRequest = 0; remove(sourceTimeout); sourceTimeout = null; }
    private void remove(Runnable callback) { if (callback != null) main.removeCallbacks(callback); }

    @Override public boolean isActive() { return preparing || capture != null || stopping; }

    @Override public void preview(ScreenSource source, DisplayModeOption mode, int rotation) {
        if (destroyed || isActive()) return;
        IDisplayHelper client = helper;
        if (client == null || !verified) { status("Prepare and connect the ADB helper first."); return; }
        if (source == null || source.id < 0 || mode == null) {
            status("Select an existing Android display before starting the helper preview."); return;
        }
        long id = sessionId, current = ++generation;
        preparing = true;
        status("Validating selected display #" + source.id + "…");
        renderControls();
        querySources(id, client, sources -> {
            if (current != generation || !preparing) return;
            for (DisplaySourceInfo candidate : sources) {
                if (candidate.displayId == source.id && !candidate.identity.isEmpty()) {
                    startCapture(id, current, candidate, mode, rotation, client);
                    return;
                }
            }
            preparing = false;
            status("Selected display is unavailable; select a current source.");
            renderControls();
        });
    }

    private void startCapture(long id, long current, DisplaySourceInfo source,
                              DisplayModeOption mode, int rotation, IDisplayHelper client) {
        if (id != sessionId || current != generation || !preparing || destroyed) return;
        try {
            accepted.set(0); rejected.set(0); batches = 0; changes = 0; previousCrc = -1;
            lastStatusMs = 0; mirrorId = -1; releaseAcknowledged = false; activeSource = source; backend = "";
            contentBounds = null;
            ready = false; stopping = false;
            spec = new CaptureSpec(id, ++requests, current, source.displayId, source.identity,
                    mode.width, mode.height, mode.id, rotation);
            capture = new CaptureController(mode.width, mode.height, current, result -> {
                if (result == 0) accepted.incrementAndGet(); else rejected.incrementAndGet();
            });
            CaptureController reader = capture;
            ByteBuffer packet = ByteBuffer.allocateDirect(reader.packetCapacity());
            client.attachCapture(spec, reader.surface(), callback);
            startupTimeout = () -> {
                if (id == sessionId && current == generation && capture == reader && !stopping
                        && (!ready || accepted.get() == 0)) {
                    status("The selected source produced no accepted image; stopping preview.");
                    stop();
                }
            };
            main.postDelayed(startupTimeout, firstFrameTimeoutMs);
            diagnostics = work.scheduleWithFixedDelay(() -> inspectFrame(id, current, reader, packet),
                    0, 200, TimeUnit.MILLISECONDS);
            status("Helper capture starting · source #" + source.displayId + " " + source.width + "×" + source.height
                    + " → " + mode.width + "×" + mode.height + ". Waiting for the first accepted image.");
        } catch (Exception failure) {
            status("Helper capture failed: " + failure.getClass().getSimpleName());
            disconnectAfterRelease = true;
            abortHelper();
        }
        renderControls();
    }

    private void inspectFrame(long id, long current, CaptureController reader, ByteBuffer packet) {
        try (FrameSubmission frame = reader.prepareLatestFrame(packet)) {
            if (frame != null) {
                CRC32 crc = new CRC32();
                crc.update(frame.packet());
                long signature = crc.getValue();
                ++batches;
                if (previousCrc != signature) { ++changes; previousCrc = signature; }
                frame.commit();
            }
            long total = batches, changed = changes;
            long now = SystemClock.elapsedRealtime();
            if (now - lastStatusMs < 1000) return;
            lastStatusMs = now;
            main.post(() -> {
                if (id != sessionId || current != generation || stopping || !ready || capture != reader) return;
                if (accepted.get() > 0) {
                    preparing = false;
                    remove(startupTimeout);
                    status("Helper preview active · " + backend + " · source #" + spec.sourceId
                            + "\nAccepted " + accepted.get() + " frames · rejected " + rejected.get()
                            + "\nEncoded batches " + total + " · changed payload signatures " + changed
                            + "\n" + geometryStatus());
                    renderControls();
                }
            });
        } catch (Exception failure) {
            main.post(() -> {
                if (id == sessionId && current == generation && !stopping) {
                    status("Helper preview encoding stopped: " + failure.getClass().getSimpleName());
                    stop();
                }
            });
        }
    }

    private final IHelperCallback callback = new IHelperCallback.Stub() {
        @Override public void onResult(CaptureResult result) {
            requireShellCaller();
            main.post(() -> result(result));
        }
    };

    private void result(CaptureResult result) {
        if (result == null || spec == null || result.sessionId != sessionId
                || result.generation != spec.generation || result.requestId != spec.requestId) return;
        if (result.code == HelperProtocol.READY || result.code == HelperProtocol.GEOMETRY_CHANGED) {
            if (stopping) return;
            ready = true; backend = result.backend; mirrorId = result.mirrorId;
            activeSource = result.source == null ? activeSource : result.source;
            contentBounds = result.contentBounds;
            status(result.detail + " · " + backend + " · mirror #" + result.mirrorId);
        } else if (result.code == HelperProtocol.RELEASED || result.code == HelperProtocol.SOURCE_REMOVED) {
            releaseAcknowledged = true;
            finishReader(result.detail);
        } else {
            status("Helper operation failed: " + result.detail);
            disconnectAfterRelease = true;
            abortHelper();
        }
    }

    @Override public void stop() {
        if (stopping) return;
        remove(startupTimeout);
        cancelQuery();
        if (diagnostics != null) diagnostics.cancel(false);
        preparing = false;
        if (capture == null || spec == null) {
            ++generation;
            if (disconnectAfterRelease) disconnect();
            renderControls();
            return;
        }
        stopping = true;
        spec = new CaptureSpec(spec.sessionId, ++requests, spec.generation, spec.sourceId,
                spec.sourceIdentity, spec.outputWidth, spec.outputHeight, spec.modeId, spec.rotation);
        status("Stopping helper preview; waiting for mirror release…");
        renderControls();
        long stoppedSession = sessionId, stoppedGeneration = spec.generation;
        long stoppedRequest = spec.requestId;
        CaptureController stoppedReader = capture;
        releaseTimeout = () -> {
            if (sessionId == stoppedSession && spec != null && spec.generation == stoppedGeneration
                    && stopping && capture == stoppedReader) {
                status("Mirror release was not acknowledged; waiting for helper exit.");
                abortHelper();
            }
        };
        main.postDelayed(releaseTimeout, requestTimeoutMs);
        try { helper.releaseCapture(stoppedSession, stoppedRequest, stoppedGeneration, callback); }
        catch (Exception failure) { abortHelper(); }
    }

    private void abortHelper() {
        if (heartbeat != null) heartbeat.cancel(false);
        protocolRequest = 0;
        remove(protocolTimeout);
        cancelQuery();
        IDisplayHelper client = helper;
        if (client == null) { finishReader("Helper disconnected."); return; }
        stopping = true;
        try { client.stopSession(sessionId); }
        catch (Exception failure) {
            // A failed transaction is not proof that its producer has died.
            if (!client.asBinder().isBinderAlive()) died(sessionId, client.asBinder());
        }
        renderControls();
    }

    private void transportFailure(long id, IBinder binder) {
        if (id != sessionId || helper == null || helper.asBinder() != binder) return;
        if (!binder.isBinderAlive()) died(id, binder);
        else {
            status("Helper transport failed; waiting for confirmed helper exit.");
            abortHelper();
        }
    }

    private void died(long id, IBinder binder) {
        if (id != sessionId || helper == null || helper.asBinder() != binder) return;
        helper = null; owner = null;
        verified = false;
        protocolRequest = 0;
        remove(protocolTimeout);
        if (heartbeat != null) heartbeat.cancel(false);
        finishReader("Helper disconnected; its capture session ended.");
    }

    private void finishReader(String detail) {
        if (cleanupPending) return;
        cleanupPending = true;
        stopping = true;
        remove(startupTimeout);
        remove(releaseTimeout);
        cancelQuery();
        if (diagnostics != null) diagnostics.cancel(false);
        CaptureController old = capture;
        CaptureSpec finishedSpec = spec;
        DisplaySourceInfo source = activeSource;
        capture = null; spec = null; ready = false; preparing = false;
        ++generation;
        // This queue also owns encoding, so no batch can outlive reader destruction.
        work.execute(() -> {
            if (old != null) old.close();
            if (finishedSpec != null) writeReport(finishedSpec, source, detail);
            main.post(() -> {
                cleanupPending = false;
                stopping = false;
                status(detail);
                if (disconnectAfterRelease) disconnect();
                renderControls();
                if (destroyed) { work.shutdown(); keepalive.shutdown(); }
            });
        });
    }

    private void writeReport(CaptureSpec finished, DisplaySourceInfo source, String detail) {
        try {
            org.json.JSONObject report = new org.json.JSONObject();
            report.put("source_id", finished.sourceId);
            report.put("source_identity", finished.sourceIdentity);
            report.put("source_width", source == null ? 0 : source.width);
            report.put("source_height", source == null ? 0 : source.height);
            report.put("output_width", finished.outputWidth);
            report.put("output_height", finished.outputHeight);
            report.put("output_rotation", finished.rotation);
            report.put("source_rotation", source == null ? 0 : source.rotation);
            if (contentBounds != null) report.put("content_bounds", new org.json.JSONArray(
                    new int[]{contentBounds.left,contentBounds.top,contentBounds.right,contentBounds.bottom}));
            report.put("backend", backend);
            report.put("mirror_id", mirrorId);
            report.put("accepted_images", accepted.get());
            report.put("rejected_images", rejected.get());
            report.put("encoded_batches", batches);
            report.put("changed_payload_signatures", changes);
            report.put("release_acknowledged", releaseAcknowledged);
            report.put("result", detail);
            try (java.io.FileOutputStream out = activity.openFileOutput("helper-preview.json", 0)) {
                out.write(report.toString(2).getBytes(StandardCharsets.UTF_8));
            }
            Log.i("ms912x-helper", "Preview ended: source=" + finished.sourceId
                    + ", accepted=" + accepted.get() + ", encoded=" + batches
                    + ", changed payload signatures=" + changes + ", release ack=" + releaseAcknowledged);
        } catch (Exception failure) { Log.w("ms912x-helper", "Could not write preview diagnostics"); }
    }

    private void disconnect() {
        long oldSessionId = sessionId;
        sessionId = 0; // Invalidate enrollment callbacks already queued on the main thread.
        HelperEnrollment.cancel(oldSessionId);
        protocolRequest = 0;
        remove(protocolTimeout);
        remove(releaseTimeout);
        remove(startupTimeout);
        cancelQuery();
        if (heartbeat != null) heartbeat.cancel(false);
        IDisplayHelper old = helper;
        if (helperBinder != null && helperDeath != null) {
            helperBinder.unlinkToDeath(helperDeath, 0);
        }
        helperBinder = null; helperDeath = null; verified = false;
        helper = null; owner = null;
        if (old != null) {
            try { old.stopSession(oldSessionId); } catch (Exception ignored) { /* Death/watchdog releases resources. */ }
        }
        disconnectAfterRelease = false;
    }

    @Override public void onStop() {
        disconnectAfterRelease = true;
        stop();
    }

    @Override public void close() {
        destroyed = true;
        onStop();
        if (!isActive()) { work.shutdown(); keepalive.shutdown(); }
    }

    private void status(String value) { screen.helperStatus.setText(value); }
    private String geometryStatus() {
        if (spec == null || activeSource == null) return "";
        return activeSource.width + "×" + activeSource.height + " → " + spec.outputWidth + "×" + spec.outputHeight
                + " · output rotation " + (spec.rotation * 90) + "° · content "
                + (contentBounds == null ? "pending" : contentBounds.toShortString());
    }
    private void renderControls() {
        screen.prepareHelperButton.setEnabled(!isActive() && !destroyed);
        screen.helperPreviewButton.setEnabled(helper != null && verified && !isActive() && !destroyed);
        screen.stopHelperButton.setEnabled(isActive() && !stopping);
        screen.helperRotationSelector.setEnabled(!isActive() && !destroyed);
    }
}
