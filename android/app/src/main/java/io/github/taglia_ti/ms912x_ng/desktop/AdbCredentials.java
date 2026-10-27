package io.github.taglia_ti.ms912x_ng.desktop;

/** Persistent identity boundary called from the native C client through JNI. */
interface AdbCredentials {
    byte[] loadHostCredential();
    void saveHostCredential(byte[] privateKeyPem);
    void savePairedDevice(String deviceId, String host, String fingerprint);
    String[] loadPairedDevice(String deviceId);
    boolean isLocalAddress(String host);
}
