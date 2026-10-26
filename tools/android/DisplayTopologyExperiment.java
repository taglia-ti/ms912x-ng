package io.github.ms912x.helper;

import android.graphics.PixelFormat;
import android.graphics.PointF;
import android.graphics.RectF;
import android.media.Image;
import android.media.ImageReader;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.os.SystemClock;
import android.util.SparseArray;
import io.github.ms912x.ipc.DisplaySourceInfo;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.LinkedHashMap;
import java.util.Map;
import org.json.JSONArray;
import org.json.JSONObject;

/** Opt-in shell tool. This entry point is not included in the APK or its bundled helper. */
public final class DisplayTopologyExperiment {
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Class<?> managerType = Class.forName("android.hardware.display.DisplayManagerGlobal");
    private final Object manager = managerType.getMethod("getInstance").invoke(null);
    private final Class<?> topologyType = Class.forName("android.hardware.display.DisplayTopology");
    private final Method getter = managerType.getMethod("getDisplayTopology");
    private final Method setter = managerType.getMethod("setDisplayTopology", topologyType);
    private final TopologyLease<Object> placement = new TopologyLease<>(new TopologyLease.Backend<Object>() {
        @Override public Object get() throws Exception { return topology(); }
        @Override public void set(Object value) throws Exception { setter.invoke(manager, value); }
    });
    private ImageReader reader;
    private ShellDesktop desktop;
    private boolean finished;
    private long probeDeadline;

    private DisplayTopologyExperiment() throws Exception {
        if (manager == null) throw new IllegalStateException("Display service unavailable; wait for Android to finish booting");
    }

    public static void main(String[] args) {
        if (Process.myUid() != 2000) {
            System.err.println("The topology experiment requires shell UID 2000");
            System.exit(1);
        }
        try {
            Looper.prepareMainLooper();
            DisplayTopologyExperiment experiment = new DisplayTopologyExperiment();
            Thread eof = new Thread(() -> {
                try { while (System.in.read() != -1) { /* ADB stream lifetime only. */ } }
                catch (Exception ignored) { /* Disconnect restores an unchanged placement. */ }
                experiment.handler.post(() -> {
                    emit("cancelled", "reason", "Host stream closed");
                    experiment.finish(130);
                });
            }, "ms912x-topology-lifetime");
            eof.setDaemon(true);
            eof.start();
            experiment.handler.post(() -> {
                try { experiment.start(args); }
                catch (Throwable error) { experiment.finish(failure(error)); }
            });
            Looper.loop();
        } catch (ClassNotFoundException | NoSuchMethodException unavailable) {
            emit("unsupported", "reason", unavailable.toString());
            System.exit(2);
        } catch (Throwable error) {
            System.exit(failure(error));
        }
    }

    private Object topology() throws Exception {
        Object value = getter.invoke(manager);
        if (value == null) throw new UnsupportedOperationException("Native display topology is disabled or unavailable on this build");
        return value;
    }

    @SuppressWarnings("unchecked")
    private Map<Integer, TopologyPlacement.Bounds> bounds(Object topology) throws Exception {
        SparseArray<RectF> rectangles = (SparseArray<RectF>) topologyType.getMethod("getAbsoluteBounds").invoke(topology);
        Map<Integer, TopologyPlacement.Bounds> result = new LinkedHashMap<>();
        for (int i = 0; i < rectangles.size(); i++) {
            RectF rectangle = rectangles.valueAt(i);
            result.put(rectangles.keyAt(i), new TopologyPlacement.Bounds(rectangle.left, rectangle.top,
                    rectangle.width(), rectangle.height()));
        }
        return result;
    }

    private void inventory(String event) throws Exception {
        JSONObject data = message(event);
        Object topology = topology();
        data.put("primaryDisplayId", topologyType.getMethod("getPrimaryDisplayId").invoke(topology));
        JSONArray nodes = new JSONArray();
        for (Map.Entry<Integer, TopologyPlacement.Bounds> entry : bounds(topology).entrySet()) {
            TopologyPlacement.Bounds b = entry.getValue();
            nodes.put(new JSONObject().put("id", entry.getKey()).put("leftDp", b.left).put("topDp", b.top)
                    .put("widthDp", b.width).put("heightDp", b.height));
        }
        data.put("topology", nodes);
        JSONArray displays = new JSONArray();
        int[] ids;
        try { ids = (int[]) managerType.getMethod("getDisplayIds").invoke(manager); }
        catch (NoSuchMethodException older) { ids = (int[]) managerType.getMethod("getDisplayIds", boolean.class).invoke(manager, false); }
        FrameworkDisplayCompat framework = new FrameworkDisplayCompat();
        Map<Integer, TopologyPlacement.Bounds> topologyBounds = bounds(topology);
        for (int id : ids) {
            DisplaySourceInfo source = framework.source(id);
            Object info = managerType.getMethod("getDisplayInfo", int.class).invoke(manager, id);
            if (source == null || info == null) continue;
            displays.put(new JSONObject().put("id", id).put("name", source.name)
                    .put("width", source.width).put("height", source.height)
                    .put("type", info.getClass().getField("type").getInt(info))
                    .put("inTopology", topologyBounds.containsKey(id)));
        }
        data.put("displays", displays);
        System.out.println(data);
    }

    private void start(String[] args) throws Exception {
        inventory("inventory");
        if (args.length == 1 && args[0].equals("inspect")) { finish(0); return; }
        if (args.length == 4 && args[0].equals("probe")) {
            int width = Integer.parseInt(args[1]), height = Integer.parseInt(args[2]), density = Integer.parseInt(args[3]);
            if (width < 1 || height < 1 || width > 3840 || height > 3840 || density < 72 || density > 640) {
                throw new IllegalArgumentException("Invalid probe size or density");
            }
            reader = ImageReader.newInstance(width, height, PixelFormat.RGBA_8888, 2);
            reader.setOnImageAvailableListener(r -> {
                try (Image image = r.acquireLatestImage()) { /* Drain without retaining captured pixels. */ }
            }, handler);
            // Use exactly the production helper's creation flags and shell attribution.
            desktop = new ShellDesktop("ms912x-topology-probe-" + Process.myPid(), width, height,
                    density, 60, reader.getSurface());
            emit("screen_created", "displayId", desktop.id());
            try { desktop.home("io.github.ms912x.desktop"); }
            catch (Exception unavailable) { emit("launcher_unavailable", "reason", detail(unavailable)); }
            probeDeadline = SystemClock.elapsedRealtime() + 5000;
            handler.postDelayed(this::checkProbe, 250);
            return;
        }
        if (args.length == 5 && args[0].equals("place")) {
            int screen = Integer.parseInt(args[1]), anchor = Integer.parseInt(args[2]);
            int seconds = Integer.parseInt(args[4]);
            if (seconds < 1 || seconds > 300) {
                throw new IllegalArgumentException("Temporary placement requires 1..300 seconds");
            }
            Object original = topology();
            Map<Integer, TopologyPlacement.Bounds> planned = TopologyPlacement.arrange(bounds(original), screen, anchor, args[3]);
            Object candidate = topologyType.getMethod("copy").invoke(original);
            Map<Integer, PointF> positions = new LinkedHashMap<>();
            for (Map.Entry<Integer, TopologyPlacement.Bounds> entry : planned.entrySet()) {
                positions.put(entry.getKey(), new PointF(entry.getValue().left, entry.getValue().top));
            }
            topologyType.getMethod("rearrange", Map.class).invoke(candidate, positions);
            if (!TopologyPlacement.matches(planned, bounds(candidate), anchor)) {
                throw new IllegalStateException("Android cannot represent this placement without moving other displays");
            }
            placement.apply(original, candidate);
            inventory("placement_active");
            emit("restore_after_seconds", "seconds", seconds);
            handler.postDelayed(() -> finish(0), seconds * 1000L);
            return;
        }
        throw new IllegalArgumentException("Expected inspect, probe WIDTH HEIGHT DENSITY, or place SCREEN ANCHOR SIDE SECONDS");
    }

    private void checkProbe() {
        try {
            boolean admitted = bounds(topology()).containsKey(desktop.id());
            if (!admitted && SystemClock.elapsedRealtime() < probeDeadline) {
                handler.postDelayed(this::checkProbe, 250);
                return;
            }
            inventory("probe_inventory");
            emit(admitted ? "screen_admitted" : "screen_excluded", "displayId", desktop.id());
            finish(admitted ? 0 : 2);
        } catch (Throwable error) { finish(failure(error)); }
    }

    private void finish(int result) {
        if (finished) return;
        finished = true;
        try {
            TopologyLease.Restoration restoration = placement.restore();
            if (restoration == TopologyLease.Restoration.RESTORED) emit("restored", "reason", "Original native topology restored");
            if (restoration == TopologyLease.Restoration.SKIPPED) emit("restore_skipped", "reason", "Topology changed elsewhere; preserving newer arrangement");
        } catch (Throwable error) { failure(error); result = 1; }
        finally {
            try { if (desktop != null) desktop.close(); }
            catch (Throwable error) { failure(error); result = 1; }
            try { if (reader != null) reader.close(); }
            catch (Throwable error) { failure(error); result = 1; }
        }
        emit("finished", "exitCode", result);
        System.exit(result);
    }

    private static JSONObject message(String event) throws Exception {
        return new JSONObject().put("event", event).put("api", Build.VERSION.SDK_INT).put("uid", Process.myUid());
    }
    private static void emit(String event, String key, Object value) {
        try { System.out.println(message(event).put(key, value)); }
        catch (Exception error) { throw new IllegalStateException(error); }
    }
    private static Throwable unwrap(Throwable error) {
        while (error instanceof InvocationTargetException && error.getCause() != null) error = error.getCause();
        return error;
    }
    private static String detail(Throwable error) { return unwrap(error).toString(); }
    private static int failure(Throwable error) {
        Throwable cause = unwrap(error);
        boolean unsupported = cause instanceof UnsupportedOperationException
                || cause instanceof ClassNotFoundException || cause instanceof NoSuchMethodException;
        emit(unsupported ? "unsupported" : "error", "reason", cause.toString());
        return unsupported ? 2 : 1;
    }
}
