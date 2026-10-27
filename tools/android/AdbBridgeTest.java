package io.github.taglia_ti.ms912x_ng.desktop;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Exercises the actual C JNI library on a host VM with JNI checking enabled. */
public final class AdbBridgeTest {
    private static void check(boolean value) { if (!value) throw new AssertionError(); }
    public static class Store implements AdbCredentials {
        int localCalls;
        public byte[] loadHostCredential() { return null; }
        public void saveHostCredential(byte[] key) { throw new IllegalStateException("private key must not escape"); }
        public void savePairedDevice(String id,String host,String fingerprint) { throw new AssertionError(); }
        public String[] loadPairedDevice(String id) { return null; }
        public boolean isLocalAddress(String host) {
            localCalls++;
            if (host.equals("throw") || host.equals("127.0.0.2")) throw new IllegalStateException("private callback message");
            return host.equals("127.0.0.1") || host.equals("::1");
        }
    }
    private static void throwsState(Runnable operation) {
        try { operation.run(); throw new AssertionError("Expected IllegalStateException"); }
        catch (IllegalStateException expected) { check(!expected.getMessage().contains("private")); }
    }
    public static void main(String[] args) throws Exception {
        Store store=new Store();
        check(NativeAdbBridge.nativeBackendVersion().contains("ms912x C ADB"));
        check(!NativeAdbBridge.nativeIsLocalAddress(null,"127.0.0.1"));
        check(!NativeAdbBridge.nativeIsLocalAddress(store,null));
        check(NativeAdbBridge.nativeIsLocalAddress(store,"127.0.0.1"));
        check(!NativeAdbBridge.nativeIsLocalAddress(store,"192.0.2.1"));
        check(!NativeAdbBridge.nativeIsLocalAddress(store,"throw"));
        Store unicode=new Store() {
            public boolean isLocalAddress(String host) { return host.equals("address-😀"); }
        };
        check(NativeAdbBridge.nativeIsLocalAddress(unicode,"address-😀"));
        check(NativeAdbBridge.nativePairAndVerify(null,"127.0.0.1",1,"127.0.0.1",2,"123456").contains("store is required"));
        check(NativeAdbBridge.nativePairAndVerify(store,null,1,"127.0.0.1",2,"123456").contains("Invalid text"));
        check(NativeAdbBridge.nativePairAndVerify(store,"127.0.0.2",1,"127.0.0.1",2,"123456").contains("Could not validate"));
        int calls=store.localCalls;
        check(NativeAdbBridge.nativePairAndVerify(store,"localhost",1,"localhost",2,"123456").contains("numeric IP"));
        check(store.localCalls==calls);
        for (int port:new int[]{-1,0,65536,Integer.MAX_VALUE})
            check(NativeAdbBridge.nativePairAndVerify(store,"127.0.0.1",port,"127.0.0.1",2,"123456").contains("port is invalid"));
        check(NativeAdbBridge.nativePairAndVerify(store,"192.0.2.1",1,"127.0.0.1",2,"123456").contains("not an address assigned"));
        check(NativeAdbBridge.nativePairAndVerify(store,"127.0.0.1",1,"::1",2,"123456").contains("same local address"));
        for (String code:new String[]{null,"12345","1234567","12a456","１２３４５６","123456\u0000extra"})
            check(!NativeAdbBridge.nativePairAndVerify(store,"127.0.0.1",1,"127.0.0.1",2,code).startsWith("Paired device"));
        check(NativeAdbBridge.nativeConnectLocalTcpAndVerify(store,"localhost",5555).contains("numeric IP"));
        check(NativeAdbBridge.nativeConnectLocalTcpAndVerify(store,"127.0.0.1",5556).contains("port 5555"));
        check(NativeAdbBridge.nativeConnectLocalTcpAndVerify(store,"192.0.2.1",5555).contains("not assigned"));
        byte[] helper="fixture helper".getBytes(StandardCharsets.US_ASCII), envelope="sensitive envelope".getBytes(StandardCharsets.US_ASCII);
        StringBuilder digest=new StringBuilder();
        for (byte b:MessageDigest.getInstance("SHA-256").digest(helper)) digest.append(String.format("%02x",b&255));
        String hash=digest.toString();
        throwsState(()->NativeAdbBridge.nativeStartHelper(store,"device","127.0.0.1",1,helper,"../../invalid",envelope));
        throwsState(()->NativeAdbBridge.nativeStartHelper(store,"device","127.0.0.1",1,new byte[1],hash,envelope));
        throwsState(()->NativeAdbBridge.nativeStartHelper(store,"device","127.0.0.1",1,helper,hash,new byte[2049]));
        throwsState(()->NativeAdbBridge.nativeStartHelper(store,"device\n","127.0.0.1",1,helper,hash,envelope));
        long previous=0;
        for (int pass=0;pass<50;pass++) {
            CountDownLatch entered=new CountDownLatch(1), release=new CountDownLatch(1);
            Store blocking=new Store() {
                public String[] loadPairedDevice(String id) {
                    entered.countDown();
                    try { check(release.await(5,TimeUnit.SECONDS)); }
                    catch (InterruptedException e) { throw new AssertionError(e); }
                    return null;
                }
            };
            long id=NativeAdbBridge.nativeStartHelper(blocking,pass==0 ? "unpaired-😀" : "unpaired","127.0.0.1",1,helper,hash,envelope);
            check(id>previous); previous=id;
            check(entered.await(5,TimeUnit.SECONDS));
            check(!NativeAdbBridge.nativeReleaseHelper(id));
            throwsState(()->NativeAdbBridge.nativeStartHelper(store,"device","127.0.0.1",1,helper,hash,envelope));
            NativeAdbBridge.nativeCloseHelper(id); NativeAdbBridge.nativeCloseHelper(id);
            release.countDown();
            long deadline=System.nanoTime()+TimeUnit.SECONDS.toNanos(5);
            String events="";
            while (!events.contains("SESSION_CLOSED")) {
                check(System.nanoTime()<deadline);
                events+=NativeAdbBridge.nativePollHelper(id); Thread.sleep(1);
            }
            check(events.length()<4096 && !events.contains("sensitive"));
            check(NativeAdbBridge.nativeReleaseHelper(id));
            check(!NativeAdbBridge.nativeReleaseHelper(id));
            check(NativeAdbBridge.nativePollHelper(id)==null);
            NativeAdbBridge.nativeCloseHelper(id);
        }
        NativeAdbBridge.nativeCancelCurrent();
        System.out.println("C ADB JNI: validation, exception translation, 50 worker cancel/join/release cycles passed");
    }
}
