package io.github.ms912x.helper;

import java.io.File;
import java.io.InputStream;
import java.nio.file.Files;
import java.util.zip.ZipFile;

/** Linux evdev/uinput ownership remains inside the shell process. */
final class NativeMouse implements AutoCloseable {
    private static boolean loaded;
    private long handle;
    static synchronized void load() throws Exception {
        if(loaded)return;
        if(android.os.Process.myUid()!=2000)throw new SecurityException("Mouse capture requires shell UID 2000");
        String arch=System.getProperty("os.arch");
        String abi=arch.equals("aarch64")?"arm64-v8a":arch.equals("x86_64")?"x86_64":null;
        if(abi==null)throw new UnsupportedOperationException("Unsupported helper ABI: "+arch);
        File directory=Files.createTempDirectory(new File("/data/local/tmp").toPath(),"ms912x-input-").toFile();
        File library=new File(directory,"input.so");
        try(ZipFile jar=new ZipFile(System.getenv("CLASSPATH"))){
            java.util.zip.ZipEntry entry=jar.getEntry("native/"+abi+"/libms912x_input.so");
            if(entry==null)throw new UnsupportedOperationException("Helper has no native mouse bridge");
            try(InputStream source=jar.getInputStream(entry)){Files.copy(source,library.toPath());}
            System.load(library.getAbsolutePath());loaded=true;
        }finally{library.delete();directory.delete();}
    }
    static String[] devices() throws Exception {load();return listNative();}
    NativeMouse(String identity) throws Exception {load();handle=openNative(identity);}
    /** One SYN_REPORT: dx, dy, button mask, horizontal wheel, vertical wheel; null on timeout. */
    synchronized int[] read() throws Exception {if(handle==0)return null;return readNative(handle);}
    synchronized void beginReports() throws Exception {if(handle!=0)beginNative(handle);}
    synchronized void emit(int dx,int dy,int buttons,int horizontal,int vertical) throws Exception {
        if(handle==0)throw new IllegalStateException("Mouse capture closed");
        emitNative(handle,dx,dy,buttons,horizontal,vertical);
    }
    @Override public synchronized void close(){if(handle!=0){closeNative(handle);handle=0;}}
    private static native String[] listNative();
    private static native long openNative(String identity);
    private static native int[] readNative(long handle);
    private static native void beginNative(long handle);
    private static native void emitNative(long handle,int dx,int dy,int buttons,int horizontal,int vertical);
    private static native void closeNative(long handle);
}
