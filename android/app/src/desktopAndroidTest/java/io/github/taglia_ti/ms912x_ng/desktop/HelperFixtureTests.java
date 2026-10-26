package io.github.taglia_ti.ms912x_ng.desktop;

import android.app.Instrumentation;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.pm.ApplicationInfo;
import android.os.Binder;
import android.os.Bundle;
import android.os.Parcel;

import java.security.MessageDigest;
import java.util.concurrent.atomic.AtomicReference;

import io.github.taglia_ti.ms912x_ng.ipc.CaptureSpec;
import io.github.taglia_ti.ms912x_ng.ipc.CaptureResult;
import io.github.taglia_ti.ms912x_ng.ipc.DisplaySourceInfo;
import io.github.taglia_ti.ms912x_ng.ipc.HelperProtocol;
import io.github.taglia_ti.ms912x_ng.ipc.SourceListResult;

import static io.github.taglia_ti.ms912x_ng.desktop.AndroidFixtures.*;

/** Auth boundaries are tested without manufacturing a shell UID in the APK. */
public final class HelperFixtureTests {
    public static void run(Instrumentation test) throws Exception {
        Context context = test.getTargetContext();
        String digest;
        try (java.io.InputStream jar = context.getAssets().open("helper.jar")) {
            MessageDigest sha = MessageDigest.getInstance("SHA-256");
            byte[] bytes = new byte[8192]; int count;
            while ((count = jar.read(bytes)) != -1) sha.update(bytes, 0, count);
            StringBuilder hex = new StringBuilder();
            for (byte b : sha.digest()) hex.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
            digest = hex.toString();
        }
        try (java.io.InputStream hash = context.getAssets().open("helper.sha256")) {
            byte[] bytes = new byte[65];
            int count = hash.read(bytes);
            require(digest.equals(new String(bytes, 0, count, java.nio.charset.StandardCharsets.US_ASCII).trim()),
                    "Bundled helper digest matches the generated DEX JAR");
        }
        expect(SecurityException.class, () -> HelperEnrollment.call(context, 2000, "claimDebug", null));
        AtomicReference<android.os.IBinder> enrolled = new AtomicReference<>();
        long id = HelperEnrollment.prepare(context, digest, (session, helper, owner) -> enrolled.set(helper));
        expect(SecurityException.class, () -> HelperEnrollment.call(context, 0, "claimDebug", null));
        expect(SecurityException.class, () -> HelperEnrollment.call(context, android.os.Process.myUid(), "claimDebug", null));
        Bundle valid = HelperEnrollment.call(context, 2000, "claimDebug", null);
        require(valid.getLong("session") == id && valid.getInt("protocol") == HelperProtocol.VERSION,
                "Claim is bound to the current protocol and session");
        expect(SecurityException.class, () -> HelperEnrollment.call(context, 2000, "claimDebug", null));
        Binder binder = new Binder();
        valid.putBinder("helper", binder);
        Bundle wrong = new Bundle(valid); wrong.putString("token", new String(new char[64]).replace('\0', '0'));
        expect(SecurityException.class, () -> HelperEnrollment.call(context, 2000, "enroll", wrong));
        Bundle wrongSession = new Bundle(valid); wrongSession.putLong("session", id + 1);
        expect(SecurityException.class, () -> HelperEnrollment.call(context, 2000, "enroll", wrongSession));
        Bundle wrongBuild = new Bundle(valid); wrongBuild.putString("digest", new String(new char[64]).replace('\0', 'f'));
        expect(SecurityException.class, () -> HelperEnrollment.call(context, 2000, "enroll", wrongBuild));
        Bundle wrongProtocol = new Bundle(valid); wrongProtocol.putInt("protocol", HelperProtocol.VERSION + 1);
        expect(SecurityException.class, () -> HelperEnrollment.call(context, 2000, "enroll", wrongProtocol));
        Bundle result = HelperEnrollment.call(context, 2000, "enroll", valid);
        require(enrolled.get() == binder && result.getBinder("owner").isBinderAlive(), "Valid one-use enrollment publishes both death-link endpoints");
        expect(SecurityException.class, () -> HelperEnrollment.call(context, 2000, "enroll", valid));
        long cancelled = HelperEnrollment.prepare(context, digest, (session, helper, owner) -> {});
        HelperEnrollment.cancel(cancelled);
        expect(SecurityException.class, () -> HelperEnrollment.call(context, 2000, "claimDebug", null));
        Context release = new ContextWrapper(context) {
            @Override public ApplicationInfo getApplicationInfo() {
                ApplicationInfo info = new ApplicationInfo(super.getApplicationInfo());
                info.flags &= ~ApplicationInfo.FLAG_DEBUGGABLE;
                return info;
            }
        };
        expect(SecurityException.class, () -> HelperEnrollment.prepare(release, digest, (session, helper, owner) -> {}));
        long embedded=HelperEnrollment.prepareEmbedded(digest,(session,helper,owner)->{});
        expect(SecurityException.class,()->HelperEnrollment.call(context,2000,"claimDebug",null));
        byte[] envelope=HelperEnrollment.takeEnvelope(context,embedded);
        String[] fields=new String(envelope,java.nio.charset.StandardCharsets.US_ASCII).split("\\n",-1);
        java.util.Arrays.fill(envelope,(byte)0);
        require(fields.length==8&&fields[0].equals("MS912X_BOOTSTRAP_1")&&fields[4].equals(context.getPackageName()+".helper"),"Embedded envelope is bounded and names the owner");
        expect(IllegalStateException.class,()->HelperEnrollment.takeEnvelope(context,embedded));
        Bundle registration=new Bundle();registration.putLong("session",embedded);registration.putInt("protocol",HelperProtocol.VERSION);
        registration.putString("digest",digest);registration.putString("token",fields[6]);registration.putBinder("helper",new Binder());
        require(HelperEnrollment.call(release,2000,"enroll",registration).getBinder("owner")!=null,"Embedded enrollment is supported without debug claims");
        expect(SecurityException.class,()->HelperEnrollment.call(context,2000,"enroll",registration));
        Parcel parcel = Parcel.obtain();
        try {
            CaptureSpec spec = new CaptureSpec(9, 11, 13, 2, "source-identity", 1920, 1080, 0x40, 0);
            spec.writeToParcel(parcel, 0); parcel.setDataPosition(0);
            CaptureSpec read = CaptureSpec.CREATOR.createFromParcel(parcel);
            require(read.sessionId == 9 && read.requestId == 11 && read.generation == 13
                            && read.sourceId == 2 && read.sourceIdentity.equals("source-identity")
                            && read.outputWidth == 1920 && read.outputHeight == 1080 && read.modeId == 0x40 && read.densityDpi==160 && read.refreshHz==60,
                    "Capture parcel preserves identity and fixed output geometry");
            parcel.setDataSize(0); parcel.setDataPosition(0);
            new DisplaySourceInfo(2, "source", "Fixture", 1280, 720, 0, 60, 7).writeToParcel(parcel, 0);
            parcel.setDataPosition(0);
            DisplaySourceInfo source = DisplaySourceInfo.CREATOR.createFromParcel(parcel);
            require(source.layerStack == 7 && source.width == 1280 && source.height == 720,
                    "Source parcel retains the compositor layer stack independently of display ID");
            parcel.setDataSize(0); parcel.setDataPosition(0);
            new CaptureResult(9, 11, 13, HelperProtocol.GEOMETRY_CHANGED, 27, "Fixture", "Updated", source,
                    new android.graphics.Rect(0, 135, 1920, 945), 3).writeToParcel(parcel, 0);
            parcel.setDataPosition(0);
            CaptureResult geometry = CaptureResult.CREATOR.createFromParcel(parcel);
            require(geometry.source.identity.equals("source") && geometry.source.layerStack == 7
                            && geometry.contentBounds.equals(new android.graphics.Rect(0, 135, 1920, 945))
                            && geometry.outputRotation == 3 && geometry.generation == 13,
                    "Geometry callback preserves source identity, effective content bounds and output rotation");
            parcel.setDataSize(0); parcel.setDataPosition(0);
            DisplaySourceInfo[] inventory = {source};
            SourceListResult response = new SourceListResult(9, 77, HelperProtocol.READY, "", inventory);
            inventory[0] = null;
            response.writeToParcel(parcel, 0); parcel.setDataPosition(0);
            SourceListResult returned = SourceListResult.CREATOR.createFromParcel(parcel);
            require(returned.sessionId == 9 && returned.requestId == 77 && returned.sources.length == 1
                            && returned.sources[0].identity.equals("source"),
                    "Asynchronous inventory retains request identity and does not borrow a mutable array");
        } finally { parcel.recycle(); }
    }
}
