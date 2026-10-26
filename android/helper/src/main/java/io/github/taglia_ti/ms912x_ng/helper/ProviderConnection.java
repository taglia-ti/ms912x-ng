package io.github.taglia_ti.ms912x_ng.helper;

import android.os.Binder;
import android.os.Build;
import android.os.Bundle;
import android.os.IBinder;

import java.lang.reflect.Method;

/** Narrow shell-to-bootstrap-provider compatibility for Android 11 and later. */
final class ProviderConnection implements AutoCloseable {
    private final Object manager;
    private final Object provider;
    private final String authority;
    private final IBinder token = new Binder();

    ProviderConnection(String authority) throws Exception {
        this.authority = authority;
        Class<?> activityManager = Class.forName("android.app.ActivityManager");
        manager = activityManager.getDeclaredMethod("getService").invoke(null);
        Method acquire = Class.forName("android.app.IActivityManager").getMethod(
                "getContentProviderExternal", String.class, int.class, IBinder.class, String.class);
        Object holder = acquire.invoke(manager, authority, 0, token, "ms912x-helper");
        if (holder == null) throw new IllegalStateException("Bootstrap provider is unavailable");
        provider = holder.getClass().getField("provider").get(holder);
        if (provider == null) throw new IllegalStateException("Bootstrap provider did not publish");
    }

    Bundle call(String operation, Bundle extras) throws Exception {
        Class<?> type = Class.forName("android.content.IContentProvider");
        if (Build.VERSION.SDK_INT >= 31) {
            Class<?> attribution = Class.forName("android.content.AttributionSource");
            Class<?> builderType = Class.forName("android.content.AttributionSource$Builder");
            Object builder = builderType.getConstructor(int.class).newInstance(2000);
            builderType.getMethod("setPackageName", String.class).invoke(builder, "com.android.shell");
            Object source = builderType.getMethod("build").invoke(builder);
            return (Bundle) type.getMethod("call", attribution, String.class, String.class,
                    String.class, Bundle.class).invoke(provider, source, authority, operation, null, extras);
        }
        return (Bundle) type.getMethod("call", String.class, String.class, String.class,
                String.class, String.class, Bundle.class).invoke(provider,
                        "com.android.shell", null, authority, operation, null, extras);
    }

    @Override public void close() {
        try {
            Class.forName("android.app.IActivityManager").getMethod(
                    "removeContentProviderExternal", String.class, IBinder.class)
                    .invoke(manager, authority, token);
        } catch (ReflectiveOperationException ignored) {
            // Process exit also releases the external provider reference.
        }
    }
}
