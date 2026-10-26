package io.github.taglia_ti.ms912x_ng.helper;

import android.os.Process;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Map;
import org.json.JSONObject;

/** Reapply original scaling after a crashed helper, without touching another live helper's lease. */
final class MouseScalingRecovery {
    private static final File DIRECTORY=new File("/data/local/tmp");
    private static final String PREFIX="ms912x-mouse-scaling-";
    private final File record;
    MouseScalingRecovery() throws Exception {
        record=Files.createTempFile(DIRECTORY.toPath(),PREFIX,".json").toFile();
    }
    void write(Map<Integer,String> displays) throws Exception {
        JSONObject values=new JSONObject();for(Map.Entry<Integer,String> entry:displays.entrySet())values.put(Integer.toString(entry.getKey()),entry.getValue());
        JSONObject data=new JSONObject().put("pid",Process.myPid()).put("start",startToken(Process.myPid())).put("displays",values);
        File temporary=Files.createTempFile(DIRECTORY.toPath(),PREFIX,".tmp").toFile();
        try{
            Files.write(temporary.toPath(),data.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));
            Files.move(temporary.toPath(),record.toPath(),StandardCopyOption.REPLACE_EXISTING,StandardCopyOption.ATOMIC_MOVE);
        }finally{temporary.delete();}
    }
    void clear(){record.delete();}
    static void recover() {
        File[] records=DIRECTORY.listFiles((directory,name)->name.startsWith(PREFIX)&&name.endsWith(".json"));
        if(records==null)return;
        FrameworkDisplayCompat framework;
        try{framework=new FrameworkDisplayCompat();}catch(Exception unavailable){return;}
        for(File record:records){
            try{
                if(record.length()>8192||!Files.isRegularFile(record.toPath(),java.nio.file.LinkOption.NOFOLLOW_LINKS))continue;
                String text=new String(Files.readAllBytes(record.toPath()),java.nio.charset.StandardCharsets.UTF_8);
                if(text.isEmpty()){record.delete();continue;}
                JSONObject data=new JSONObject(text);int pid=data.getInt("pid");
                if(alive(pid,data.getString("start")))continue;
                JSONObject displays=data.getJSONObject("displays");java.util.Iterator<String> keys=displays.keys();
                while(keys.hasNext()){
                    String key=keys.next();int id=Integer.parseInt(key);
                    io.github.taglia_ti.ms912x_ng.ipc.DisplaySourceInfo info=framework.source(id);
                    if(info!=null&&info.identity.equals(displays.getString(key)))new FrameworkMouseCompat().scaling(id,true);
                }
                record.delete();
            }catch(Exception failure){android.util.Log.w("MS912xHelper","Deferred pointer-scaling recovery",failure);}
        }
    }
    private static boolean alive(int pid,String token) throws Exception {
        try{Os.kill(pid,0);}catch(ErrnoException failure){if(failure.errno==OsConstants.ESRCH)return false;return true;}
        return token.equals(startToken(pid));
    }
    private static String startToken(int pid) throws Exception {
        String stat=new String(Files.readAllBytes(new File("/proc/"+pid+"/stat").toPath()),java.nio.charset.StandardCharsets.US_ASCII);
        return stat.substring(stat.lastIndexOf(')')+2).split(" ")[19];
    }
}
