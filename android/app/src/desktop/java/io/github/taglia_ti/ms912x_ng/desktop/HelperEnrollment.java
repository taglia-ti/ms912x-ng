package io.github.taglia_ti.ms912x_ng.desktop;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Process;
import android.os.SystemClock;

import java.security.MessageDigest;
import java.security.SecureRandom;
import java.nio.charset.StandardCharsets;

import io.github.taglia_ti.ms912x_ng.ipc.HelperProtocol;

/** One outstanding, expiring, in-memory bootstrap attempt owned by the visible Debug screen. */
final class HelperEnrollment {
    interface Listener { void enrolled(long sessionId, IBinder helper, IBinder owner); }
    private static Pending pending;
    private static final SecureRandom RANDOM = new SecureRandom();

    static synchronized long prepare(Context context, String digest, Listener listener) {
        if ((context.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0) {
            throw new SecurityException("PC enrollment is available only in a debug APK");
        }
        return prepare(digest,listener,true);
    }

    static synchronized long prepareEmbedded(String digest,Listener listener) {
        return prepare(digest,listener,false);
    }

    private static long prepare(String digest,Listener listener,boolean debug) {
        long id = RANDOM.nextLong() & Long.MAX_VALUE;
        byte[] secret = new byte[32];
        RANDOM.nextBytes(secret);
        pending = new Pending(Math.max(1, id), hex(secret), digest, listener, debug);
        return pending.id;
    }

    static synchronized byte[] takeEnvelope(Context context,long id) {
        if(pending==null||pending.id!=id||pending.debug||pending.claimed||SystemClock.elapsedRealtime()>pending.deadline)throw new IllegalStateException("No embedded bootstrap pending");
        pending.claimed=true;
        String envelope="MS912X_BOOTSTRAP_1\n"+id+"\n"+Process.myUid()+"\n"+HelperProtocol.VERSION+"\n"
                +context.getPackageName()+".helper\n"+pending.digest+"\n"+pending.token+"\n";
        return envelope.getBytes(StandardCharsets.US_ASCII);
    }

    static synchronized void cancel(long id) {
        if (pending != null && pending.id == id) pending = null;
    }

    static synchronized Bundle call(Context context, int callerUid, String operation, Bundle data) {
        if (callerUid != HelperProtocol.SHELL_UID) throw new SecurityException("Shell UID required");
        Pending attempt = pending;
        if (attempt == null || SystemClock.elapsedRealtime() > attempt.deadline) {
            pending = null;
            throw new SecurityException("No pending helper enrollment");
        }
        if ("claimDebug".equals(operation)) {
            if ((context.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0 || !attempt.debug || attempt.claimed) {
                throw new SecurityException("Debug enrollment cannot be claimed");
            }
            attempt.claimed = true;
            Bundle result = new Bundle();
            result.putLong("session", attempt.id);
            result.putInt("appUid", Process.myUid());
            result.putInt("protocol", HelperProtocol.VERSION);
            result.putString("digest", attempt.digest);
            result.putString("token", attempt.token);
            return result;
        }
        if (!"enroll".equals(operation) || data == null || !attempt.claimed
                || data.getLong("session") != attempt.id || data.getInt("protocol") != HelperProtocol.VERSION
                || !attempt.digest.equals(data.getString("digest"))
                || !sameSecret(attempt.token, data.getString("token"))) {
            throw new SecurityException("Helper enrollment rejected");
        }
        IBinder helper = data.getBinder("helper");
        if (helper == null || !helper.isBinderAlive()) throw new SecurityException("Missing helper Binder");
        pending = null;
        Bundle result = new Bundle();
        result.putBinder("owner", attempt.owner);
        // The owner Binder remains held by the app-side client after this callback.
        attempt.listener.enrolled(attempt.id, helper, attempt.owner);
        return result;
    }

    private static boolean sameSecret(String expected, String actual) {
        return actual != null && actual.length() == 64 && MessageDigest.isEqual(
                expected.getBytes(StandardCharsets.US_ASCII), actual.getBytes(StandardCharsets.US_ASCII));
    }

    private static String hex(byte[] data) {
        StringBuilder out = new StringBuilder(data.length * 2);
        for (byte b : data) out.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
        return out.toString();
    }

    private static final class Pending {
        final long id;
        final String token, digest;
        final Listener listener;
        final IBinder owner = new Binder();
        final long deadline = SystemClock.elapsedRealtime() + 60_000;
        boolean claimed;
        final boolean debug;
        Pending(long id, String token, String digest, Listener listener, boolean debug) {
            this.id = id; this.token = token; this.digest = digest; this.listener = listener; this.debug=debug;
        }
    }
    private HelperEnrollment() {}
}
