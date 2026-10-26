package io.github.taglia_ti.ms912x_ng.desktop;

import android.content.*;
import java.io.File;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import static io.github.taglia_ti.ms912x_ng.desktop.AndroidFixtures.*;

final class CredentialFixtureTests {
    static void storage(Context base) throws Exception {
        File dir = new File(base.getCacheDir(), "credential-fixture-" + System.nanoTime());
        require(dir.mkdirs(), "Isolated storage directory");
        String prefs = "fixture-" + dir.getName();
        Context context = new ContextWrapper(base) {
            public Context getApplicationContext() { return this; }
            public File getNoBackupFilesDir() { return dir; }
            public SharedPreferences getSharedPreferences(String name, int mode) {
                return base.getSharedPreferences(prefs, mode);
            }
        };
        File encrypted = new File(dir, "adb-host-identity.enc");
        AdbCredentialStore store = new AdbCredentialStore(context);
        try {
            require(store.loadHostCredential() == null, "No saved identity");
            byte[] key = "fixture private credential, not a real ADB key".getBytes(StandardCharsets.UTF_8);
            store.saveHostCredential(key);
            require(Arrays.equals(key, store.loadHostCredential()), "Keystore round trip");
            byte[] first = Files.readAllBytes(encrypted.toPath());
            require(!Arrays.equals(first, key), "No plaintext storage");
            store.saveHostCredential(key);
            byte[] second = Files.readAllBytes(encrypted.toPath());
            require(!Arrays.equals(first, second), "Fresh GCM nonce");
            require(!new File(dir, "adb-host-identity.enc.tmp").exists(), "Atomic rename consumes temporary");
            second[second.length - 1] ^= 1; Files.write(encrypted.toPath(), second);
            expect(IllegalStateException.class, store::loadHostCredential);
            Files.write(encrypted.toPath(), new byte[12]);
            expect(IllegalStateException.class, store::loadHostCredential);
            Files.delete(encrypted.toPath());
            require(store.loadPairedDevice("missing") == null, "Missing paired metadata");
            store.savePairedDevice("DEVICE-A", "127.0.0.1", "fixture fingerprint");
            require(Arrays.equals(store.loadPairedDevice("device-a"),
                    new String[]{"DEVICE-A", "127.0.0.1", "fixture fingerprint"}), "Case-normalized metadata");
            context.getSharedPreferences("unused", 0).edit().putString("device:bad", "malformed").commit();
            require(store.loadPairedDevice("bad") == null, "Malformed metadata rejected");
            require(store.isLocalAddress("127.0.0.1"), "Loopback local address");
            require(!store.isLocalAddress("192.0.2.1"), "Non-local test address");
            require(!store.isLocalAddress("not a numeric address!"), "Invalid address");
            String local = store.firstLocalIpv4Address();
            require(local == null || store.isLocalAddress(local), "Enumerated IPv4 belongs to this device");
            // A missing parent forces the production filesystem failure path.
            Files.delete(dir.toPath());
            expect(IllegalStateException.class, () -> store.saveHostCredential(key));
        } finally {
            base.getSharedPreferences(prefs, 0).edit().clear().commit();
            if (encrypted.exists()) encrypted.delete();
            new File(dir, "adb-host-identity.enc.tmp").delete(); dir.delete();
        }
    }

    public static final class Credentials implements AdbCredentials {
        public byte[] loadHostCredential() { return null; }
        public void saveHostCredential(byte[] key) {}
        public void savePairedDevice(String id, String host, String fingerprint) {}
        public String[] loadPairedDevice(String id) { return null; }
        public boolean isLocalAddress(String host) {
            if (host.equals("throw")) throw new IllegalStateException("credential fixture exception");
            return host.equals("127.0.0.1") || host.equals("::1");
        }
    }

    static void bridge() {
        NativeAdbCall.Result unavailable = NativeAdbCall.run(
                () -> { throw new NoClassDefFoundError("fixture missing native library"); },
                () -> { throw new AssertionError("Version lookup must not run after operation failure"); },
                "Wireless ADB failed");
        require(unavailable.backendVersion.equals("ADB client unavailable"),
                "Unavailable native client has a safe backend label");
        require(unavailable.message.endsWith("NoClassDefFoundError"),
                "Native initialization failure becomes a status result");
        require(NativeAdbCall.backendVersionOrUnavailable().contains("9adeb625"),
                "Loaded backend version remains available");
        NativeAdbBridge.nativeCancelCurrent();
        Credentials store = new Credentials();
        require(!NativeAdbBridge.nativeIsLocalAddress(null, "127.0.0.1"), "Null credential callback rejected");
        require(NativeAdbBridge.nativePairAndVerify(null, "127.0.0.1", 1, "127.0.0.1", 2, "123456")
                .contains("credential store is required"), "Null credential store rejected");
        require(NativeAdbBridge.nativeBackendVersion().contains("9adeb625"), "Pinned backend loaded");
        require(NativeAdbBridge.nativeIsLocalAddress(store, "127.0.0.1"), "JNI local callback");
        require(!NativeAdbBridge.nativeIsLocalAddress(store, "192.0.2.1"), "JNI non-local callback");
        require(!NativeAdbBridge.nativeIsLocalAddress(store, "throw"), "JNI callback exception translated");
        require(NativeAdbBridge.nativePairAndVerify(store, "127.0.0.1", 0, "127.0.0.1", 1, "123456")
                .contains("port is invalid"), "Pairing port validation");
        require(NativeAdbBridge.nativePairAndVerify(store, "127.0.0.1", 1, "127.0.0.1", 65536, "123456")
                .contains("port is invalid"), "Connect port validation");
        require(NativeAdbBridge.nativePairAndVerify(store, "192.0.2.1", 1, "127.0.0.1", 2, "123456")
                .contains("not an address assigned"), "Remote endpoint rejected");
        require(NativeAdbBridge.nativePairAndVerify(store, "127.0.0.1", 1, "::1", 2, "123456")
                .contains("same local address"), "Mismatched addresses rejected");
        for (String code : new String[]{"12345", "1234567", "12a456", "１２３４５６"})
            require(NativeAdbBridge.nativePairAndVerify(store, "127.0.0.1", 1, "127.0.0.1", 2, code)
                    .contains("six digits"), "Pairing code validation");
        require(NativeAdbBridge.nativePairAndVerify(store, "throw", 1, "127.0.0.1", 2, "123456")
                .contains("Could not validate endpoint"), "JNI exception produces ordinary error string");
        require(NativeAdbBridge.nativePairAndVerify(store, null, 1, "127.0.0.1", 2, "123456")
                .contains("Invalid text"), "Null JNI text translated");
        require(NativeAdbBridge.nativeConnectLocalTcpAndVerify(store, "localhost", 5555)
                .contains("numeric IP"), "TCP requires numeric address");
        require(NativeAdbBridge.nativeConnectLocalTcpAndVerify(store, "127.0.0.1", 5556)
                .contains("port 5555"), "TCP debug port restriction");
        require(NativeAdbBridge.nativeConnectLocalTcpAndVerify(store, "192.0.2.1", 5555)
                .contains("not assigned"), "TCP rejects remote endpoint");
    }
}
