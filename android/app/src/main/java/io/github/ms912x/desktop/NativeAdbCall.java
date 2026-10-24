package io.github.ms912x.desktop;

import java.util.concurrent.Callable;

final class NativeAdbCall {
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

    static String backendVersionOrUnavailable() {
        try {
            return NativeAdbBridge.nativeBackendVersion();
        } catch (LinkageError failure) {
            return "ADB client unavailable";
        }
    }

    static void cancelCurrentIfAvailable() {
        try {
            NativeAdbBridge.nativeCancelCurrent();
        } catch (LinkageError ignored) {
            // Mirror-only builds omit this optional native library.
        }
    }
}
