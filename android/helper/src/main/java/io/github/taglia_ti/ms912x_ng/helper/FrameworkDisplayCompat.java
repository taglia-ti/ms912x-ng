package io.github.taglia_ti.ms912x_ng.helper;

import android.graphics.Rect;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.os.IBinder;
import android.view.Surface;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

import io.github.taglia_ti.ms912x_ng.ipc.CaptureSpec;
import io.github.taglia_ti.ms912x_ng.ipc.DisplaySourceInfo;

/** All hidden display calls remain in the UID-2000 helper, never in the APK. */
final class FrameworkDisplayCompat {
    private final Object manager;
    private final Class<?> type;

    FrameworkDisplayCompat() throws Exception {
        type = Class.forName("android.hardware.display.DisplayManagerGlobal");
        manager = type.getMethod("getInstance").invoke(null);
    }

    DisplaySourceInfo[] listSources() throws Exception {
        int[] ids;
        try {
            ids = (int[]) type.getMethod("getDisplayIds").invoke(manager);
        } catch (NoSuchMethodException olderSignature) {
            ids = (int[]) type.getMethod("getDisplayIds", boolean.class).invoke(manager, false);
        }
        List<DisplaySourceInfo> sources = new ArrayList<>();
        for (int id : ids) {
            DisplaySourceInfo source = source(id);
            if (source != null && !source.name.startsWith("ms912x-helper-") && !source.name.startsWith("ms912x-desktop-")) sources.add(source);
        }
        return sources.toArray(new DisplaySourceInfo[0]);
    }

    DisplaySourceInfo source(int id) throws Exception {
        Object info = type.getMethod("getDisplayInfo", int.class).invoke(manager, id);
        if (info == null) return null;
        Class<?> fields = info.getClass();
        String identity = (String) fields.getField("uniqueId").get(info);
        String name = (String) fields.getField("name").get(info);
        float refresh;
        try {
            refresh = ((Number) fields.getMethod("getRefreshRate").invoke(info)).floatValue();
        } catch (NoSuchMethodException legacyRate) {
            refresh = ((android.view.Display.Mode) fields.getMethod("getMode").invoke(info)).getRefreshRate();
        }
        return new DisplaySourceInfo(id, identity == null ? "" : identity, name == null ? "" : name,
                fields.getField("logicalWidth").getInt(info), fields.getField("logicalHeight").getInt(info),
                fields.getField("rotation").getInt(info), refresh, fields.getField("layerStack").getInt(info));
    }

    DisplaySourceInfo mouseDesktop() throws Exception {
        Object topology=type.getMethod("getDisplayTopology").invoke(manager);
        if(topology==null)throw new UnsupportedOperationException("Android desktop mouse APIs are unavailable");
        int id=(Integer)topology.getClass().getMethod("getPrimaryDisplayId").invoke(topology);
        Object info=type.getMethod("getDisplayInfo",int.class).invoke(manager,id);
        if(info==null||info.getClass().getField("type").getInt(info)!=android.view.Display.class.getField("TYPE_EXTERNAL").getInt(null))
            throw new IllegalStateException("Connect and activate the main desktop monitor first");
        return source(id);
    }
    int density(int id) throws Exception {
        Object info=type.getMethod("getDisplayInfo",int.class).invoke(manager,id);
        if(info==null)throw new IllegalStateException("Display disappeared");
        return info.getClass().getField("logicalDensityDpi").getInt(info);
    }

    Mirror createScreen(CaptureSpec spec, Surface surface,String appPackage) throws Exception {
        CaptureTransform transform=new CaptureTransform(spec.outputWidth,spec.outputHeight,spec.rotation);
        FrameRenderer renderer=null;
        ShellDesktop desktop=null;
        try{
            if(spec.rotation!=0)renderer=new FrameRenderer(surface,transform,spec.outputWidth,spec.outputHeight);
            desktop=new ShellDesktop("ms912x-desktop-"+spec.sessionId,spec.outputWidth,spec.outputHeight,
                    spec.densityDpi,spec.refreshHz,renderer==null?surface:renderer.input());
            ShellDesktop owned=desktop;
            DisplayCaptureBackend backend=new DisplayCaptureBackend(){
                public int displayId(){return owned.id();}
                public String name(){return "Shell independent desktop";}
                public void resize(int width,int height,DisplaySourceInfo source){ }
                public void close(){owned.close();}
            };
            DisplaySourceInfo source=source(owned.id());
            if(source==null)throw new IllegalStateException("Created desktop is unavailable");
            if(source.width!=spec.outputWidth||source.height!=spec.outputHeight)
                throw new IllegalStateException("Android created a "+source.width+"×"+source.height
                        +" screen, but the selected HDMI mode is "+spec.outputWidth+"×"+spec.outputHeight);
            Mirror result=new Mirror(backend,surface,source,transform,renderer);
            result.desktop=owned;
            owned.home(appPackage);
            return result;
        }catch(Exception failure){
            if(desktop!=null)try{desktop.close();}catch(Exception cleanup){failure.addSuppressed(cleanup);}
            if(renderer!=null)try{renderer.close();}catch(Exception cleanup){failure.addSuppressed(cleanup);}
            throw failure;
        }
    }

    Mirror capture(CaptureSpec spec, Surface surface) throws Exception {
        DisplaySourceInfo source = source(spec.sourceId);
        if (source == null || !source.identity.equals(spec.sourceIdentity)) {
            throw new IllegalArgumentException("Selected display disappeared or changed identity");
        }
        CaptureTransform transform = new CaptureTransform(spec.outputWidth, spec.outputHeight, spec.rotation);
        FrameRenderer renderer = null;
        DisplayCaptureBackend backend = null;
        try {
            // Keep the direct compositor path for unrotated output. Rotated output
            // uses an isolated GPU pass with the same fixed APK reader dimensions.
            if (spec.rotation != 0) renderer = new FrameRenderer(surface, transform, source.width, source.height);
            Surface input = renderer == null ? surface : renderer.input();
            int width = renderer == null ? spec.outputWidth : source.width;
            int height = renderer == null ? spec.outputHeight : source.height;
            backend = createBackend(spec.sessionId, input, width, height, source);
            return new Mirror(backend, surface, source, transform, renderer);
        } catch (Exception failure) {
            if (backend != null) {
                try { backend.close(); } catch (Exception cleanup) { failure.addSuppressed(cleanup); }
            }
            if (renderer != null) {
                try { renderer.close(); } catch (Exception cleanup) { failure.addSuppressed(cleanup); }
            }
            throw failure;
        }
    }

    private DisplayCaptureBackend createBackend(long sessionId, Surface surface, int width, int height,
                                                 DisplaySourceInfo source) throws Exception {
        try {
            Method factory = DisplayManager.class.getDeclaredMethod("createVirtualDisplay",
                    String.class, int.class, int.class, int.class, Surface.class);
            VirtualDisplay display;
            try {
                display = (VirtualDisplay) factory.invoke(null, "ms912x-helper-" + sessionId,
                        width, height, source.displayId, surface);
            } catch (InvocationTargetException failure) {
                throw cause(failure);
            }
            if (display == null) throw new IllegalStateException("DisplayManager returned no mirror");
            try {
                display.getDisplay().getDisplayId(); // Fail here if construction returned an unusable wrapper.
                return new DisplayCaptureBackend() {
                    private VirtualDisplay owned = display;
                    @Override public int displayId() { return owned == null ? -1 : owned.getDisplay().getDisplayId(); }
                    @Override public String name() { return "DisplayManager mirror"; }
                    @Override public void resize(int nextWidth, int nextHeight, DisplaySourceInfo ignored) throws Exception {
                        // Some framework builds retain the original source projection
                        // after a logical-size override. Recreate only our sink so
                        // traversal recomputes it; the APK reader and source survive.
                        close();
                        try {
                            owned = (VirtualDisplay) factory.invoke(null, "ms912x-helper-" + sessionId,
                                    nextWidth, nextHeight, source.displayId, surface);
                        } catch (InvocationTargetException failure) { throw cause(failure); }
                        if (owned == null) throw new IllegalStateException("DisplayManager returned no replacement mirror");
                    }
                    @Override public void close() {
                        if (owned != null) { VirtualDisplay old = owned; owned = null; old.release(); }
                    }
                };
            } catch (RuntimeException failure) {
                display.release();
                throw failure;
            }
        } catch (NoSuchMethodException legacyPlatform) {
            return legacyMirror(sessionId, surface, width, height, source);
        }
    }

    private DisplayCaptureBackend legacyMirror(long sessionId, Surface surface, int width, int height,
                                                DisplaySourceInfo source) throws Exception {
        Class<?> control = Class.forName("android.view.SurfaceControl");
        IBinder token = (IBinder) control.getMethod("createDisplay", String.class, boolean.class)
                .invoke(null, "ms912x-helper-" + sessionId, false);
        try {
            DisplayCaptureBackend backend = new DisplayCaptureBackend() {
                private IBinder owned = token;
                private int currentWidth = width, currentHeight = height;
                @Override public int displayId() { return -1; }
                @Override public String name() { return "SurfaceControl mirror"; }
                @Override public void resize(int w, int h, DisplaySourceInfo next) throws Exception {
                    if (w != currentWidth || h != currentHeight) {
                        // A SurfaceTexture default-size change alone does not
                        // rebuild SurfaceFlinger's virtual framebuffer on API 30.
                        close();
                        owned = (IBinder) control.getMethod("createDisplay", String.class, boolean.class)
                                .invoke(null, "ms912x-helper-" + sessionId, false);
                        currentWidth = w; currentHeight = h;
                    }
                    project(control, owned, surface, w, h, next);
                }
                @Override public void close() {
                    if (owned != null) { IBinder old = owned; owned = null; destroy(control, old); }
                }
            };
            backend.resize(width, height, source);
            return backend;
        } catch (Exception failure) {
            try { destroy(control, token); } catch (Exception cleanup) { failure.addSuppressed(cleanup); }
            throw failure;
        }
    }

    private static void project(Class<?> control, IBinder token, Surface surface, int width, int height,
                                DisplaySourceInfo source) throws Exception {
        Rect bounds = new CaptureTransform(width, height, 0).bounds(source.width, source.height);
        control.getMethod("openTransaction").invoke(null);
        try {
            control.getMethod("setDisplaySurface", IBinder.class, Surface.class).invoke(null, token, surface);
            control.getMethod("setDisplaySize", IBinder.class, int.class, int.class).invoke(null, token, width, height);
            // The layer stack already contains logical, upright source coordinates.
            control.getMethod("setDisplayProjection", IBinder.class, int.class, Rect.class, Rect.class)
                    .invoke(null, token, 0, new Rect(0, 0, source.width, source.height), bounds);
            control.getMethod("setDisplayLayerStack", IBinder.class, int.class).invoke(null, token, source.layerStack);
        } finally {
            control.getMethod("closeTransaction").invoke(null);
        }
    }

    private static void destroy(Class<?> control, IBinder token) {
        try { control.getMethod("destroyDisplay", IBinder.class).invoke(null, token); }
        catch (ReflectiveOperationException failure) { throw new IllegalStateException("Mirror release failed", failure); }
    }

    static Exception cause(InvocationTargetException error) {
        Throwable cause = error.getCause();
        return cause instanceof Exception ? (Exception)cause : new IllegalStateException(cause);
    }

    static final class Mirror implements AutoCloseable {
        int id;
        ShellDesktop desktop;
        final String backend;
        DisplaySourceInfo source;
        final CaptureTransform transform;
        private final DisplayCaptureBackend captureBackend;
        private final FrameRenderer renderer;
        private final Surface surface;
        private boolean closed;
        Mirror(DisplayCaptureBackend backend, Surface surface, DisplaySourceInfo source,
               CaptureTransform transform, FrameRenderer renderer) {
            this.id = backend.displayId(); this.backend = backend.name() + (renderer == null ? " · compositor fit" : " · EGL fit/rotation");
            this.captureBackend = backend; this.surface = surface; this.source = source;
            this.transform = transform; this.renderer = renderer;
        }
        Rect bounds() { return transform.bounds(source.width, source.height); }
        void healthy() { if (renderer != null) renderer.healthy(); }
        void updateSource(DisplaySourceInfo next) throws Exception {
            healthy();
            if (renderer != null) renderer.resize(next.width, next.height);
            captureBackend.resize(renderer == null ? transform.outputWidth : next.width,
                    renderer == null ? transform.outputHeight : next.height, next);
            id = captureBackend.displayId();
            source = next;
        }
        @Override public void close() {
            if (closed) return;
            closed = true;
            RuntimeException failure = null;
            try { captureBackend.close(); } catch (RuntimeException error) { failure = error; }
            try { if (renderer != null) renderer.close(); }
            catch (RuntimeException error) { if (failure == null) failure = error; else failure.addSuppressed(error); }
            finally { surface.release(); }
            if (failure != null) throw failure;
        }
    }
}
