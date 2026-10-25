package io.github.ms912x.helper;

import android.os.Binder;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.Process;
import android.os.RemoteException;
import android.os.SystemClock;
import android.view.Surface;

import java.io.FileInputStream;
import java.security.MessageDigest;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

import io.github.ms912x.ipc.CaptureResult;
import io.github.ms912x.ipc.CaptureSpec;
import io.github.ms912x.ipc.DisplaySourceInfo;
import io.github.ms912x.ipc.HelperProtocol;
import io.github.ms912x.ipc.IDisplayHelper;
import io.github.ms912x.ipc.IHelperCallback;
import io.github.ms912x.ipc.ProtocolInfo;
import io.github.ms912x.ipc.IInventoryCallback;
import io.github.ms912x.ipc.IProtocolCallback;
import io.github.ms912x.ipc.SourceListResult;

/** Standalone app_process entry point. ADB supplies shell privileges, never the APK. */
public final class HelperMain {
    public static void main(String[] args) {
        if (Process.myUid() != HelperProtocol.SHELL_UID) {
            System.err.println("MS912x helper requires the Android shell UID 2000");
            System.exit(1);
        }
        boolean embedded=args.length==1&&args[0].equals("--bootstrap-stdin");
        boolean debug=args.length==2&&args[0].equals("--debug-claim")&&args[1].matches("[a-zA-Z0-9_.]+\\.helper");
        if(!embedded&&!debug){System.err.println("Expected a supported bootstrap mode");System.exit(1);return;}
        try {
            Looper.prepareMainLooper();
            String digest = digest();
            BootstrapEnvelope envelope=embedded?BootstrapEnvelope.read(System.in):null;
            String authority=embedded?envelope.authority:args[1];
            try (ProviderConnection provider = new ProviderConnection(authority)) {
                Bundle enrollment;
                if(embedded){
                    enrollment=new Bundle();enrollment.putLong("session",envelope.session);
                    enrollment.putInt("appUid",envelope.appUid);enrollment.putInt("protocol",envelope.protocol);
                    enrollment.putString("digest",envelope.digest);enrollment.putString("token",envelope.token);
                    envelope=null;
                }else enrollment=provider.call("claimDebug",null);
                if (enrollment == null) throw new IllegalStateException("Open one debug enrollment in the app first");
                long sessionId = enrollment.getLong("session");
                int appUid = enrollment.getInt("appUid", -1);
                if (sessionId <= 0 || appUid < 10000 || enrollment.getInt("protocol") != HelperProtocol.VERSION
                        || !digest.equals(enrollment.getString("digest"))) {
                    throw new SecurityException("Helper build or enrollment mismatch");
                }
                try (Session session = new Session(sessionId, appUid, digest, authority.substring(0,authority.length()-".helper".length()))) {
                    Bundle registration = new Bundle(enrollment);
                    registration.putBinder("helper", session);
                    Bundle result = provider.call("enroll", registration);
                    enrollment.remove("token");
                    registration.remove("token");
                    IBinder owner = result == null ? null : result.getBinder("owner");
                    if (owner == null) throw new SecurityException("Helper enrollment rejected");
                    IBinder.DeathRecipient ownerDeath = session::shutdown;
                    owner.linkToDeath(ownerDeath, 0);
                    Thread eof = new Thread(() -> {
                        try { while (System.in.read() != -1) { /* Bootstrap stream lifetime only. */ } }
                        catch (Exception ignored) { /* ADB disconnect is cancellation. */ }
                        session.shutdown();
                    }, "ms912x-adb-lifetime");
                    eof.setDaemon(true);
                    eof.start();
                    System.out.println("MS912x helper connected; uid=2000; protocol=" + HelperProtocol.VERSION);
                    Looper.loop();
                    owner.unlinkToDeath(ownerDeath, 0);
                }
            }
        } catch (Throwable failure) {
            Throwable detail = failure;
            while (detail.getCause() != null) detail = detail.getCause();
            // Never dump the enrollment Bundle or token into diagnostics.
            System.err.println("MS912x helper: " + detail.getClass().getSimpleName()
                    + (embedded ? " (bootstrap/capture failed)" : ": " + detail.getMessage()));
            System.exit(1);
        }
        System.exit(0);
    }

    private static String digest() throws Exception {
        String path = System.getenv("CLASSPATH");
        if (path == null || path.contains(":")) throw new IllegalArgumentException("Expected one helper JAR");
        MessageDigest sha = MessageDigest.getInstance("SHA-256");
        try (FileInputStream file = new FileInputStream(path)) {
            byte[] chunk = new byte[16384];
            int count;
            while ((count = file.read(chunk)) != -1) sha.update(chunk, 0, count);
        }
        StringBuilder value = new StringBuilder(64);
        for (byte b : sha.digest()) value.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
        return value.toString();
    }

    private static final class Session extends IDisplayHelper.Stub implements AutoCloseable {
        private final long sessionId;
        private final int appUid;
        private final String appPackage;
        private final String digest;
        private FrameworkDisplayCompat framework;
        private final HandlerThread control = new HandlerThread("ms912x-helper-control");
        private final Handler handler;
        private final ScheduledExecutorService watchdog = Executors.newSingleThreadScheduledExecutor();
        private final ControlQueue queue;
        private final CaptureRequests requests = new CaptureRequests();
        private final HelperLifetime lifetime = new HelperLifetime(SystemClock.elapsedRealtime(),
                HelperProtocol.WATCHDOG_MS, HelperProtocol.STOP_GRACE_MS);
        private final AtomicBoolean sourceCheckPending = new AtomicBoolean();
        private long nextSourceCheck;
        private volatile boolean closing;
        private volatile FrameworkDisplayCompat.Mirror mirror;
        private CaptureSpec active;
        private IHelperCallback callback;

        Session(long sessionId, int appUid, String digest,String appPackage) throws Exception {
            this.appPackage=appPackage;
            this.sessionId = sessionId; this.appUid = appUid; this.digest = digest;
            control.start();
            handler = new Handler(control.getLooper());
            queue = new ControlQueue(handler::post, 64);
            watchdog.scheduleWithFixedDelay(this::checkLifetime, 100, 100, TimeUnit.MILLISECONDS);
        }

        private FrameworkDisplayCompat framework() throws Exception {
            if (framework == null) framework = new FrameworkDisplayCompat();
            return framework;
        }

        private void authorize(long requestedSession) {
            if (Binder.getCallingUid() != appUid || requestedSession != sessionId || closing) {
                throw new SecurityException("Unexpected helper caller or session");
            }
        }

        @Override public void getProtocolInfo(long requestedSession, long requestId, IProtocolCallback callback) {
            authorize(requestedSession);
            if (requestId <= 0 || callback == null) throw new IllegalArgumentException("Invalid protocol request");
            try { callback.onProtocolInfo(sessionId, requestId,
                    new ProtocolInfo(HelperProtocol.VERSION, digest, Process.myUid(), Build.VERSION.SDK_INT)); }
            catch (RemoteException deadApp) { shutdown(); }
        }

        @Override public void listDisplaySources(long requestedSession, long requestId, IInventoryCallback callback) {
            authorize(requestedSession);
            if (requestId <= 0 || callback == null) throw new IllegalArgumentException("Invalid inventory request");
            if (!queue.post(() -> {
                if (closing) { sources(callback, requestId, HelperProtocol.CANCELLED, "Session closing", null); return; }
                try {
                    DisplaySourceInfo[] inventory = framework().listSources();
                    if (inventory.length > 128) inventory = java.util.Arrays.copyOf(inventory, 128);
                    sources(callback, requestId, HelperProtocol.READY, "", inventory);
                } catch (Exception failure) {
                    sources(callback, requestId, HelperProtocol.FAILED, "Display enumeration failed: "
                            + failure.getClass().getSimpleName(), null);
                }
            })) sources(callback, requestId, HelperProtocol.BUSY, "Helper control queue is busy", null);
        }

        private void sources(IInventoryCallback listener, long request, int code, String detail, DisplaySourceInfo[] sources) {
            try { listener.onSources(new SourceListResult(sessionId, request, code, detail, sources)); }
            catch (RemoteException deadApp) { shutdown(); }
        }

        private void rejectSurface(Surface surface, IHelperCallback listener, CaptureSpec spec, int code, String detail) {
            if (surface != null) surface.release();
            send(listener, spec, code, -1, "", detail);
        }

        private boolean valid(CaptureSpec spec, Surface surface, IHelperCallback callback) {
            return spec != null && spec.requestId > 0 && spec.generation > 0 && (spec.sourceId >= 0 || spec.sourceId == HelperProtocol.CREATE_SCREEN)
                    && spec.sourceIdentity != null && (spec.sourceId==HelperProtocol.CREATE_SCREEN ? spec.sourceIdentity.isEmpty() : !spec.sourceIdentity.isEmpty()) && spec.sourceIdentity.length() <= 512
                    && spec.densityDpi>=80 && spec.densityDpi<=640 && spec.refreshHz>=24 && spec.refreshHz<=240
                    && spec.outputWidth > 0 && spec.outputHeight > 0 && spec.outputWidth <= 8192 && spec.outputHeight <= 8192
                    && (long)spec.outputWidth * spec.outputHeight <= 16_777_216L && spec.rotation >= 0 && spec.rotation <= 3
                    && surface != null && surface.isValid() && callback != null;
        }

        private void attach(CaptureSpec spec, Surface surface, IHelperCallback result) {
            if (closing || mirror != null || !requests.current(spec.generation)) {
                rejectSurface(surface, result, spec, HelperProtocol.CANCELLED, "Capture cancelled or another capture is active");
                return;
            }
            try {
                FrameworkDisplayCompat.Mirror created = requests.create(spec.generation, surface::release,
                        () -> spec.sourceId==HelperProtocol.CREATE_SCREEN?framework().createScreen(spec,surface,appPackage):framework().capture(spec, surface));
                if (created == null) {
                    send(result, spec, HelperProtocol.CANCELLED, -1, "", "Capture cancelled before publication");
                    return;
                }
                mirror = created; active = spec; callback = result;
                send(result, spec, HelperProtocol.READY, created.id, created.backend,
                        "Mirror created; waiting for the app to accept its first image");
            } catch (Exception failure) {
                send(result, spec, HelperProtocol.FAILED, -1, "", failure.getClass().getSimpleName() + ": " + failure.getMessage());
                if (!requests.current(spec.generation) || failure.getSuppressed().length != 0) shutdown();
            }
        }

        @Override public void attachCapture(CaptureSpec spec, Surface surface, IHelperCallback result) {
            try {
                authorize(spec == null ? -1 : spec.sessionId);
            } catch (RuntimeException rejectedCaller) {
                if (surface != null) surface.release();
                throw rejectedCaller;
            }
            if (!valid(spec, surface, result)) {
                rejectSurface(surface, result, spec, HelperProtocol.FAILED, "Invalid capture specification or Surface");
                return;
            }
            if (!requests.attach(spec.requestId, spec.generation)) {
                rejectSurface(surface, result, spec, HelperProtocol.CANCELLED, "Stale or cancelled capture request");
                return;
            }
            if (!queue.post(() -> attach(spec, surface, result)))
                rejectSurface(surface, result, spec, HelperProtocol.BUSY, "Helper control queue is busy");
        }

        @Override public void releaseCapture(long requestedSession, long requestId, long generation, IHelperCallback listener) {
            authorize(requestedSession);
            CaptureSpec stopped = new CaptureSpec(sessionId, requestId, generation, -1, "", 0, 0, 0, 0);
            if (listener == null || !requests.cancel(requestId, generation)) {
                send(listener, stopped, HelperProtocol.CANCELLED, -1, "", "Stale release request");
                return;
            }
            if (!queue.post(() -> {
                try {
                    if (active != null && active.generation <= generation) release();
                    send(listener, stopped, HelperProtocol.RELEASED, -1, "", "Helper mirror released");
                } catch (RuntimeException failure) {
                    send(listener, stopped, HelperProtocol.FAILED, -1, "", "Mirror release failed");
                    shutdown();
                }
            })) {
                send(listener, stopped, HelperProtocol.BUSY, -1, "", "Release queue is busy; stopping helper");
                shutdown();
            }
        }

        @Override public void launchApp(long requestedSession,long requestId,long generation,String component,IHelperCallback listener){
            authorize(requestedSession);
            CaptureSpec request=new CaptureSpec(sessionId,requestId,generation,-1,"",0,0,0,0);
            if(listener==null||requestId<=0||generation<=0||component==null||component.length()>512)throw new IllegalArgumentException("Invalid app launch");
            if(!requests.command(requestId,generation)){send(listener,request,HelperProtocol.APP_FAILED,-1,"","Stale app launch request");return;}
            if(!queue.post(()->{
                try{
                    if(closing||active==null||active.generation!=generation||!requests.current(generation)
                            ||mirror==null||mirror.desktop==null)throw new IllegalStateException("No owned desktop in this generation");
                    mirror.desktop.launch(component);
                    send(listener,request,HelperProtocol.APP_LAUNCHED,mirror.id,mirror.backend,"App launched on independent screen #"+mirror.id);
                }catch(Exception failure){send(listener,request,HelperProtocol.APP_FAILED,-1,"",failure.getClass().getSimpleName()+": "+failure.getMessage());}
            }))send(listener,request,HelperProtocol.APP_FAILED,-1,"","Helper control queue is busy");
        }

        @Override public void heartbeat(long requestedSession) {
            authorize(requestedSession);
            lifetime.heartbeat(SystemClock.elapsedRealtime());
        }

        @Override public void stopSession(long requestedSession) {
            authorize(requestedSession);
            shutdown();
        }

        private void checkLifetime() {
            long now = SystemClock.elapsedRealtime();
            HelperLifetime.Action action = lifetime.check(now);
            if (action == HelperLifetime.Action.EXIT) {
                // Do not depend on either Looper, framework Binder calls, or Java
                // shutdown hooks when the ordinary teardown deadline has expired.
                Process.killProcess(Process.myPid());
                return;
            }
            if (action == HelperLifetime.Action.STOP) {
                shutdown();
                return;
            }
            FrameworkDisplayCompat.Mirror current = mirror;
            if (current == null || closing) return;
            if (now < nextSourceCheck) return;
            nextSourceCheck = now + 1000;
            if (sourceCheckPending.compareAndSet(false, true)) handler.post(() -> {
                try { checkSource(current); }
                finally { sourceCheckPending.set(false); }
            });
        }

        private void checkSource(FrameworkDisplayCompat.Mirror current) {
            if (mirror != current || closing) return;
            try {
                current.healthy();
                if (active == null || !requests.current(active.generation)) return;
                DisplaySourceInfo now = framework().source(current.source.displayId);
                if (now == null || !current.source.identity.equals(now.identity)) {
                    CaptureSpec stopped = active;
                    IHelperCallback listener = callback;
                    if (stopped == null) return;
                    release();
                    send(listener, stopped, HelperProtocol.SOURCE_REMOVED, -1, "",
                            "Source disappeared or was replaced; select it again");
                } else if (now.width != current.source.width || now.height != current.source.height
                        || now.rotation != current.source.rotation || now.layerStack != current.source.layerStack) {
                    current.updateSource(now);
                    send(callback, active, HelperProtocol.GEOMETRY_CHANGED, current.id, current.backend,
                            "Source geometry updated; output dimensions are unchanged");
                }
            } catch (Exception failure) { shutdown(); }
        }

        private void send(IHelperCallback listener, CaptureSpec spec, int code, int id, String backend, String detail) {
            if (listener == null) return;
            if (detail.length() > 240) detail = detail.substring(0, 240);
            FrameworkDisplayCompat.Mirror current = mirror;
            boolean geometry = current != null && (code == HelperProtocol.READY || code == HelperProtocol.GEOMETRY_CHANGED);
            try { listener.onResult(new CaptureResult(sessionId, spec == null ? 0 : spec.requestId,
                    spec == null ? 0 : spec.generation, code, id, backend, detail,
                    geometry ? current.source : null, geometry ? current.bounds() : null, spec == null ? 0 : spec.rotation)); }
            catch (RemoteException deadApp) { shutdown(); }
        }

        private void release() {
            FrameworkDisplayCompat.Mirror owned = mirror;
            if (owned != null) owned.close();
            mirror = null; active = null; callback = null;
        }

        void shutdown() {
            if (!lifetime.stop(SystemClock.elapsedRealtime())) return;
            closing = true;
            requests.close();
            queue.close();
            handler.post(() -> {
                try { release(); }
                finally { new Handler(Looper.getMainLooper()).post(() -> System.exit(0)); }
            });
        }

        @Override public void close() {
            watchdog.shutdownNow();
            control.quitSafely();
        }
    }
}
