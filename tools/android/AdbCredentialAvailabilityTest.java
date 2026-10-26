package io.github.taglia_ti.ms912x_ng.desktop;

import java.util.ArrayDeque;
import java.util.concurrent.atomic.AtomicInteger;

public final class AdbCredentialAvailabilityTest {
    private static void require(boolean value,String detail){if(!value)throw new AssertionError(detail);}
    public static void main(String[] args){
        ArrayDeque<Runnable> worker=new ArrayDeque<>(),ui=new ArrayDeque<>();
        AtomicInteger reads=new AtomicInteger(),updates=new AtomicInteger();
        AdbCredentialAvailability cache=new AdbCredentialAvailability(worker::add,ui::add,host->{reads.incrementAndGet();return true;},updates::incrementAndGet);
        for(int i=0;i<100;i++)require(!cache.hasFor("local"),"Pending checks are nonblocking");
        require(worker.size()==1&&reads.get()==0,"Repeated UI renders coalesce into one worker lookup");
        worker.remove().run();require(!cache.hasFor("local")&&reads.get()==1,"Worker completion waits for UI publication");
        ui.remove().run();require(cache.hasFor("local")&&updates.get()==1,"Published credential availability is memoized");
        cache.invalidate("local");cache.hasFor("local");worker.remove().run();
        cache.invalidate("local");cache.hasFor("local");worker.remove().run();
        ui.remove().run();require(!cache.hasFor("local")&&updates.get()==1,"Stale pre-pairing result cannot overwrite a new lookup");
        ui.remove().run();require(cache.hasFor("local")&&updates.get()==2,"New pairing lookup publishes normally");
        cache.hasFor("other");worker.remove().run();cache.close();ui.remove().run();
        require(!cache.hasFor("local")&&worker.isEmpty()&&updates.get()==2,"Closed UI ignores late results and cannot restart work");
        AdbCredentialAvailability failing=new AdbCredentialAvailability(worker::add,ui::add,host->{throw new IllegalStateException("Keystore unavailable");},updates::incrementAndGet);
        failing.hasFor("local");worker.remove().run();ui.remove().run();
        require(!failing.hasFor("local")&&worker.isEmpty(),"Unavailable storage is cached without a render/retry loop");
        failing.close();
        System.out.println("PASS: credential lookup coalescing, worker isolation, pairing invalidation and shutdown");
    }
}
