package io.github.taglia_ti.ms912x_ng.helper;

import android.graphics.PointF;
import android.os.Binder;
import android.os.IBinder;
import java.lang.reflect.Method;

/** Runtime-only API probe; no virtual input device API is linked into the app. */
final class VirtualMouseCompat implements AutoCloseable {
    private final Object input;
    private final Class<?> inputType = Class.forName("android.hardware.input.IInputManager");
    private final Class<?> mouseType = Class.forName("android.hardware.input.IVirtualMouse");
    private final Class<?> eventType = Class.forName("android.hardware.input.VirtualMouseRelativeEvent");
    private final Class<?> eventBuilder = Class.forName("android.hardware.input.VirtualMouseRelativeEvent$Builder");
    private final Method relativeX = eventBuilder.getMethod("setRelativeX", float.class);
    private final Method relativeY = eventBuilder.getMethod("setRelativeY", float.class);
    private final IBinder token = new Binder();
    private Object mouse;

    VirtualMouseCompat(int displayId) throws Exception {
        Object service = Class.forName("android.os.ServiceManager").getMethod("getService", String.class).invoke(null, "input");
        input = Class.forName("android.hardware.input.IInputManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, service);
        Class<?> builderType = Class.forName("android.hardware.input.VirtualMouseConfig$Builder");
        Object builder = builderType.getConstructor().newInstance();
        builderType.getMethod("setAssociatedDisplayId", int.class).invoke(builder, displayId);
        builderType.getMethod("setInputDeviceName", String.class).invoke(builder, "ms912x-virtual-mouse-probe");
        Object config = builderType.getMethod("build").invoke(builder);
        mouse = inputType.getMethod("createVirtualMouse", IBinder.class, config.getClass()).invoke(input, token, config);
        if (mouse == null) throw new IllegalStateException("Android returned no virtual mouse");
    }

    int deviceId() throws Exception { return (Integer) mouseType.getMethod("getInputDeviceId").invoke(mouse); }
    PointF position() throws Exception {
        return (PointF) mouseType.getMethod("getCursorPositionInLogicalDisplay").invoke(mouse);
    }
    PointF positionOnDisplay(int displayId) throws Exception {
        return (PointF) inputType.getMethod("getCursorPositionInLogicalDisplay", int.class).invoke(input, displayId);
    }
    boolean move(float dx, float dy) throws Exception {
        Object builder = eventBuilder.getConstructor().newInstance();
        relativeX.invoke(builder, dx);
        relativeY.invoke(builder, dy);
        return Boolean.TRUE.equals(mouseType.getMethod("sendMouseRelativeEvent", eventType)
                .invoke(mouse, eventBuilder.getMethod("build").invoke(builder)));
    }
    @Override public void close() throws Exception {
        if (mouse != null) {
            mouseType.getMethod("close").invoke(mouse);
            mouse = null;
        }
    }
}
