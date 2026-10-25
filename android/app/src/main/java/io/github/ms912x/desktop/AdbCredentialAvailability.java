package io.github.ms912x.desktop;

import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.Executor;
import java.util.function.Consumer;
import java.util.function.Predicate;

/** UI-owned memoization; file/Keystore work runs only on the supplied worker. */
final class AdbCredentialAvailability implements AutoCloseable {
    private static final class Entry { boolean available; }
    private final Map<String,Entry> entries=new HashMap<>();
    private final Executor worker;
    private final Consumer<Runnable> ui;
    private final Predicate<String> lookup;
    private final Runnable changed;
    private boolean closed;

    AdbCredentialAvailability(Executor worker,Consumer<Runnable> ui,Predicate<String> lookup,Runnable changed){
        this.worker=worker;this.ui=ui;this.lookup=lookup;this.changed=changed;
    }

    boolean hasFor(String host){
        if(closed||host==null)return false;
        Entry known=entries.get(host);
        if(known!=null)return known.available;
        Entry entry=new Entry();entries.put(host,entry);
        worker.execute(()->{
            boolean found;
            try{found=lookup.test(host);}catch(RuntimeException failure){found=false;}
            final boolean available=found;
            ui.accept(()->{
                if(closed||entries.get(host)!=entry)return;
                entry.available=available;changed.run();
            });
        });
        return false;
    }

    void invalidate(String host){entries.remove(host);}
    @Override public void close(){closed=true;entries.clear();}
}
