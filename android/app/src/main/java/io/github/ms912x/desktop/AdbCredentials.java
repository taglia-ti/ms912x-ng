package io.github.ms912x.desktop;

/** Persistent identity boundary called from the Rust client through JNI. */
interface AdbCredentials {
    byte[] loadHostCredential();
    void saveHostCredential(byte[] privateKeyPem);
    void savePairedDevice(String deviceId, String host, String fingerprint);
    String[] loadPairedDevice(String deviceId);
    boolean isLocalAddress(String host);
}
