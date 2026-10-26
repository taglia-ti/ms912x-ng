package io.github.ms912x.helper;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.HashSet;
import java.util.Set;
import java.util.LinkedHashMap;
import java.util.Map;

/** Preserve the original display-scoped scaling state while the router supplies exact coordinates. */
final class MouseScalingLease implements AutoCloseable {
    private final FrameworkMouseCompat mouse;
    private final Set<Integer> changed=new HashSet<>();
    private final Map<Integer,String> identities=new LinkedHashMap<>();
    private MouseScalingRecovery recovery;
    MouseScalingLease(FrameworkMouseCompat mouse){this.mouse=mouse;}
    void disable(int... displays) throws Exception {
        Set<Integer> disabled=disabledDisplays();
        FrameworkDisplayCompat framework=new FrameworkDisplayCompat();
        for(int display:displays)if(!disabled.contains(display)){
            // Track before Binder dispatch: a lost reply can follow a successful update.
            if(recovery==null)recovery=new MouseScalingRecovery();
            identities.put(display,framework.source(display).identity);recovery.write(identities);
            changed.add(display);mouse.scaling(display,false);
        }
    }
    private static Set<Integer> disabledDisplays() throws Exception {
        Process process=new ProcessBuilder("/system/bin/dumpsys","input").start();
        try(BufferedReader reader=new BufferedReader(new InputStreamReader(process.getInputStream()))){
            for(int count=0;count<100;count++){
                String line=reader.readLine();if(line==null)break;
                if(line.trim().startsWith("Display with Mouse Scaling Disabled:")){
                    Set<Integer> result=new HashSet<>();
                    String values=line.substring(line.indexOf('{')+1,line.indexOf('}')).trim();
                    if(!values.isEmpty())for(String value:values.split(","))result.add(Integer.parseInt(value.trim()));
                    return result;
                }
            }
            throw new UnsupportedOperationException("Cannot preserve Android mouse scaling state");
        }finally{process.destroy();}
    }
    @Override public void close() throws Exception {
        Exception failure=null;
        for(int display:changed)try{mouse.scaling(display,true);}catch(Exception error){failure=error;}
        changed.clear();if(failure!=null)throw failure;
        if(recovery!=null){recovery.clear();recovery=null;}
    }
}
