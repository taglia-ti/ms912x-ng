package io.github.ms912x.desktop;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;

import java.io.File;
import java.net.InetAddress;
import java.net.Inet4Address;
import java.net.NetworkInterface;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.KeyStore;
import java.util.Enumeration;
import java.util.Locale;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/** Stores the reusable ADB identity encrypted with a key held by Android Keystore. */
final class AdbCredentialStore implements AdbCredentials {
    private static final String KEY_ALIAS = "ms912x-adb-host-identity-v1";
    private static final String PREFS_NAME = "adb-paired-devices";
    private static final String DEVICE_PREFIX = "device:";
    private final Context context;
    private final SharedPreferences pairedDevices;

    AdbCredentialStore(Context context) {
        this.context = context.getApplicationContext();
        pairedDevices = this.context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
    }

    public synchronized byte[] loadHostCredential() {
        File file = credentialFile();
        if (!file.isFile()) {
            return null;
        }
        try {
            byte[] encrypted = Files.readAllBytes(file.toPath());
            if (encrypted.length <= 12) {
                throw new IllegalStateException("Stored ADB identity is invalid");
            }
            byte[] nonce = new byte[12];
            System.arraycopy(encrypted, 0, nonce, 0, nonce.length);
            byte[] ciphertext = new byte[encrypted.length - nonce.length];
            System.arraycopy(encrypted, nonce.length, ciphertext, 0, ciphertext.length);
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, getOrCreateKey(), new GCMParameterSpec(128, nonce));
            return cipher.doFinal(ciphertext);
        } catch (Exception failure) {
            throw new IllegalStateException("Could not read encrypted ADB identity", failure);
        }
    }

    public synchronized void saveHostCredential(byte[] privateKeyPem) {
        File destination = credentialFile();
        File temporary = new File(destination.getParentFile(), destination.getName() + ".tmp");
        try {
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, getOrCreateKey());
            byte[] nonce = cipher.getIV();
            byte[] ciphertext = cipher.doFinal(privateKeyPem);
            byte[] encrypted = new byte[nonce.length + ciphertext.length];
            System.arraycopy(nonce, 0, encrypted, 0, nonce.length);
            System.arraycopy(ciphertext, 0, encrypted, nonce.length, ciphertext.length);
            Files.write(temporary.toPath(), encrypted);
            try {
                Files.move(temporary.toPath(), destination.toPath(),
                        StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            } catch (java.nio.file.AtomicMoveNotSupportedException unsupported) {
                Files.move(temporary.toPath(), destination.toPath(),
                        StandardCopyOption.REPLACE_EXISTING);
            }
        } catch (Exception failure) {
            temporary.delete();
            throw new IllegalStateException("Could not save encrypted ADB identity", failure);
        }
    }

    public synchronized void savePairedDevice(String deviceId, String host,
                                               String certificateFingerprint) {
        boolean saved = pairedDevices.edit()
                .putString(deviceKey(deviceId), deviceId + "\n" + host + "\n"
                        + certificateFingerprint)
                .commit();
        if (!saved) {
            throw new IllegalStateException("Could not save paired-device metadata");
        }
    }

    public synchronized String[] loadPairedDevice(String deviceId) {
        String value = pairedDevices.getString(deviceKey(deviceId), null);
        if (value == null) {
            return null;
        }
        String[] fields = value.split("\\n", -1);
        return fields.length == 3 ? fields : null;
    }

    public boolean isLocalAddress(String host) {
        try {
            InetAddress requested = InetAddress.getByName(host);
            Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
            while (interfaces != null && interfaces.hasMoreElements()) {
                NetworkInterface network = interfaces.nextElement();
                Enumeration<InetAddress> addresses = network.getInetAddresses();
                while (addresses.hasMoreElements()) {
                    InetAddress address = addresses.nextElement();
                    if (address.equals(requested)) {
                        return true;
                    }
                }
            }
        } catch (Exception ignored) {
            return false;
        }
        return false;
    }

    String firstLocalIpv4Address() {
        try {
            Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
            while (interfaces != null && interfaces.hasMoreElements()) {
                NetworkInterface network = interfaces.nextElement();
                if (!network.isUp() || network.isLoopback()) {
                    continue;
                }
                Enumeration<InetAddress> addresses = network.getInetAddresses();
                while (addresses.hasMoreElements()) {
                    InetAddress address = addresses.nextElement();
                    if (address instanceof Inet4Address && !address.isLoopbackAddress()
                            && !address.isLinkLocalAddress()) {
                        return address.getHostAddress();
                    }
                }
            }
        } catch (Exception ignored) {
            return null;
        }
        return null;
    }

    private File credentialFile() {
        return new File(context.getNoBackupFilesDir(), "adb-host-identity.enc");
    }

    private SecretKey getOrCreateKey() throws Exception {
        KeyStore keyStore = KeyStore.getInstance("AndroidKeyStore");
        keyStore.load(null);
        java.security.Key key = keyStore.getKey(KEY_ALIAS, null);
        if (key instanceof SecretKey) {
            return (SecretKey) key;
        }
        KeyGenerator generator = KeyGenerator.getInstance(
                KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
        generator.init(new KeyGenParameterSpec.Builder(KEY_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build());
        return generator.generateKey();
    }

    private static String deviceKey(String deviceId) {
        return DEVICE_PREFIX + deviceId.toLowerCase(Locale.ROOT);
    }
}
