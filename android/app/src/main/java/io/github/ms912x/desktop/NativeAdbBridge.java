package io.github.ms912x.desktop;

/** JNI access to the in-process Android wireless ADB client. */
final class NativeAdbBridge {
    static {
        System.loadLibrary("ms912x_adb_bridge");
    }

    private NativeAdbBridge() {
    }

    static native String nativeBackendVersion();

    static native void nativeCancelCurrent();

    static native long nativeStartHelper(AdbCredentials store,String deviceId,String host,int port,
                                         byte[] helper,String digest,byte[] envelope);
    static native String nativePollHelper(long handle);
    static native void nativeCloseHelper(long handle);
    static native boolean nativeReleaseHelper(long handle);

    static native String nativePairAndVerify(AdbCredentials store,
                                             String pairingHost, int pairingPort,
                                             String connectHost, int connectPort,
                                             String pairingCode);

    static native String nativeConnectLocalTcpAndVerify(AdbCredentials store,
                                                        String host, int port);

    static native boolean nativeIsLocalAddress(AdbCredentials store, String host);
}
