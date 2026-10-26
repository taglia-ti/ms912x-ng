package io.github.taglia_ti.ms912x_ng.desktop;

import android.app.Instrumentation;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import static io.github.taglia_ti.ms912x_ng.desktop.AndroidFixtures.*;

/** Real persistent runtime/JNI boundaries; no paired endpoint or privilege is simulated. */
public final class EmbeddedBridgeFixtureTests {
    private static byte[] read(java.io.InputStream input) throws java.io.IOException {
        java.io.ByteArrayOutputStream bytes=new java.io.ByteArrayOutputStream();byte[] chunk=new byte[8192];int count;
        while((count=input.read(chunk))!=-1)bytes.write(chunk,0,count);return bytes.toByteArray();
    }
    public static void run(Instrumentation test) throws Exception {
        byte[] helper;
        try(java.io.InputStream input=test.getTargetContext().getAssets().open("helper.jar")){helper=read(input);}
        String digest;
        try(java.io.InputStream input=test.getTargetContext().getAssets().open("helper.sha256")){digest=new String(read(input),StandardCharsets.US_ASCII).trim();}
        byte[] envelope="in-memory-fixture\n".getBytes(StandardCharsets.US_ASCII);
        AdbCredentials unpaired=new AdbCredentials(){
            public byte[] loadHostCredential(){return null;}
            public void saveHostCredential(byte[] key){throw new AssertionError("No pairing in fixture");}
            public void savePairedDevice(String a,String b,String c){throw new AssertionError("No pairing in fixture");}
            public String[] loadPairedDevice(String device){return null;}
            public boolean isLocalAddress(String host){return host.equals("127.0.0.1");}
        };
        final String hash=digest;
        expect(IllegalStateException.class,()->NativeAdbBridge.nativeStartHelper(unpaired,"fixture","192.0.2.1",12345,helper,hash,envelope));
        expect(IllegalStateException.class,()->NativeAdbBridge.nativeStartHelper(unpaired,"fixture","127.0.0.1",0,helper,hash,envelope));
        expect(IllegalStateException.class,()->NativeAdbBridge.nativeStartHelper(unpaired,"fixture","127.0.0.1",12345,new byte[1],hash,envelope));
        for(int pass=0;pass<2;++pass){
            long handle=NativeAdbBridge.nativeStartHelper(unpaired,"unpaired-fixture","127.0.0.1",12345,helper,digest,envelope);
            require(handle>0,"Opaque persistent session handle");
            expect(IllegalStateException.class,()->NativeAdbBridge.nativeStartHelper(unpaired,"fixture","127.0.0.1",12345,helper,hash,envelope));
            if(pass==1){NativeAdbBridge.nativeCloseHelper(handle);NativeAdbBridge.nativeCloseHelper(handle);}
            long deadline=android.os.SystemClock.elapsedRealtime()+10_000;String events="";
            while(!events.contains("SESSION_CLOSED")){
                require(android.os.SystemClock.elapsedRealtime()<deadline,"Unpaired/cancelled session terminates");
                String next=NativeAdbBridge.nativePollHelper(handle);if(next!=null)events+=next;Thread.sleep(20);
            }
            require(events.length()<4096,"Bounded event delivery");
            require(NativeAdbBridge.nativeReleaseHelper(handle),"Completed session released");
            require(NativeAdbBridge.nativePollHelper(handle)==null&&!NativeAdbBridge.nativeReleaseHelper(handle),"Released handle cannot be reused");
        }
        Arrays.fill(envelope,(byte)0);
    }
}
