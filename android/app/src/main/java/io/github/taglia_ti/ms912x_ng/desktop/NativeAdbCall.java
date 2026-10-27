package io.github.taglia_ti.ms912x_ng.desktop;

import java.util.concurrent.Callable;

final class NativeAdbCall {
    private static final Object lock = new Object();
    private static Object activeOwner;
    static final class Result {
        final String backendVersion;
        final String message;

        Result(String backendVersion, String message) {
            this.backendVersion = backendVersion;
            this.message = message;
        }
    }

    private NativeAdbCall() {}

    static Result run(Callable<String> operation, Callable<String> backendVersion,
                      String failurePrefix) {
        try {
            String message = operation.call();
            return new Result(backendVersion.call(), message);
        } catch (Exception | LinkageError failure) {
            return new Result("ADB client unavailable",
                    failurePrefix + ": " + failure.getClass().getSimpleName());
        }
    }

    static Result runOwned(Object owner, Callable<String> operation,
                           Callable<String> backendVersion, String failurePrefix) {
        synchronized (lock) {
            if (Thread.currentThread().isInterrupted())
                return new Result("ADB client", "ADB operation cancelled");
            if (activeOwner != null)
                return new Result("ADB client", "Another ADB operation is already running");
            activeOwner = owner;
        }
        try {
            return run(operation, backendVersion, failurePrefix);
        } finally {
            synchronized (lock) { activeOwner = null; }
        }
    }

    static String backendVersionOrUnavailable() {
        try {
            return NativeAdbBridge.nativeBackendVersion();
        } catch (LinkageError failure) {
            return "ADB client unavailable";
        }
    }

    static void cancelIfOwned(Object owner) {
        synchronized (lock) {
            if (activeOwner != owner) return;
            try {
                NativeAdbBridge.nativeCancelCurrent();
            } catch (LinkageError ignored) {
                // Mirror-only builds omit this optional native library.
            }
        }
    }
}
