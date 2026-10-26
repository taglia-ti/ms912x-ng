package io.github.ms912x.helper;

import android.graphics.PointF;
import android.os.IBinder;
import java.lang.reflect.Method;

/** Native desktop cursor position and temporary display scaling, without creating another cursor. */
final class FrameworkMouseCompat {
    private final Object input;
    private final Method position,scaling;
    FrameworkMouseCompat() throws Exception {
        Object service=Class.forName("android.os.ServiceManager").getMethod("getService",String.class).invoke(null,"input");
        input=Class.forName("android.hardware.input.IInputManager$Stub").getMethod("asInterface",IBinder.class).invoke(null,service);
        Class<?> type=Class.forName("android.hardware.input.IInputManager");
        position=type.getMethod("getCursorPositionInLogicalDisplay",int.class);
        scaling=type.getMethod("setMouseScalingEnabled",boolean.class,int.class);
    }
    PointF position(int display) throws Exception {return (PointF)position.invoke(input,display);}
    void scaling(int display,boolean enabled) throws Exception {scaling.invoke(input,enabled,display);}
}
