package io.github.ms912x.helper;

import java.util.ArrayDeque;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/** Tests production queue/cancellation/lifetime code without Android or a fabricated shell UID. */
public final class HelperControlTests {
    private static void check(boolean value, String message) { if (!value) throw new AssertionError(message); }
    private static final class SurfaceOwner implements AutoCloseable {
        final AtomicInteger closed = new AtomicInteger();
        @Override public void close() { check(closed.incrementAndGet() == 1, "Surface released twice"); }
    }
    private static final class Mirror implements AutoCloseable {
        final SurfaceOwner surface;
        final AtomicInteger closed = new AtomicInteger();
        boolean fail;
        Mirror(SurfaceOwner surface) { this.surface = surface; }
        @Override public void close() {
            check(closed.incrementAndGet() == 1, "Mirror released twice");
            surface.close();
            if (fail) throw new IllegalStateException("Injected release failure");
        }
    }
    private static void queueBounds() {
        ArrayDeque<Runnable> pending = new ArrayDeque<>();
        ControlQueue queue = new ControlQueue(pending::offer, 64);
        for (int i=0;i<64;++i) check(queue.post(() -> {}), "Queue rejected below capacity");
        check(!queue.post(() -> {}), "Queue retained more than 64 operations");
        pending.remove().run();
        check(queue.post(() -> { throw new IllegalStateException("Injected operation failure"); }), "Slot was not returned");
        while (!pending.isEmpty()) { try { pending.remove().run(); } catch (IllegalStateException expected) { } }
        check(queue.post(() -> {}), "Thrown operation leaked its slot");
        queue.close();
        check(!queue.post(() -> {}), "Closed queue accepted work");
        AtomicInteger calls = new AtomicInteger();
        ControlQueue stoppedPoster = new ControlQueue(job -> calls.incrementAndGet()>1, 1);
        check(!stoppedPoster.post(() -> {}), "Dead dispatcher was accepted");
        check(stoppedPoster.post(() -> {}), "Rejected post leaked a queue slot");
    }
    private static void cancellationBeforeCreate() throws Exception {
        CaptureRequests requests = new CaptureRequests();
        check(requests.attach(1,1), "Initial attach");
        check(requests.cancel(2,1), "Cancel pending attach");
        check(requests.cancel(2,1), "Duplicate cancellation must be idempotent");
        SurfaceOwner surface = new SurfaceOwner();
        check(requests.create(1,surface,() -> { throw new AssertionError("Cancelled factory ran"); }) == null, "Cancelled capture published");
        check(surface.closed.get()==1, "Cancelled Surface leaked");
        check(!requests.attach(3,1), "Retired generation was reused");
        check(requests.attach(3,2), "New generation rejected");
        check(!requests.cancel(2,2), "Stale cancellation accepted");
        check(requests.cancel(2,1) && requests.current(2), "Old duplicate cancellation retired the newer generation");
    }
    private static void cancellationDuringCreate(boolean shutdown, boolean releaseFails) throws Exception {
        CaptureRequests requests = new CaptureRequests(); requests.attach(1,1);
        SurfaceOwner surface = new SurfaceOwner(); Mirror mirror = new Mirror(surface); mirror.fail=releaseFails;
        CountDownLatch entered = new CountDownLatch(1), unblock = new CountDownLatch(1);
        AtomicReference<Mirror> published = new AtomicReference<>();
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread producer = new Thread(() -> {
            try {
                published.set(requests.create(1,surface,() -> {
                    entered.countDown();
                    check(unblock.await(3,TimeUnit.SECONDS), "Test factory did not unblock");
                    return mirror;
                }));
            } catch (Throwable error) { failure.set(error); }
        });
        producer.start(); check(entered.await(3,TimeUnit.SECONDS), "Test factory did not start");
        if(shutdown) requests.close(); else requests.cancel(2,1);
        // A blocked framework worker cannot defer the independent termination deadline.
        HelperLifetime lifetime = new HelperLifetime(0,10000,2000);
        lifetime.stop(100); lifetime.heartbeat(2000);
        check(lifetime.check(2100)==HelperLifetime.Action.EXIT && producer.isAlive(), "Watchdog depended on framework completion");
        unblock.countDown(); producer.join(3000);
        check(!producer.isAlive() && published.get()==null, "Late factory completion published a cancelled mirror");
        check(mirror.closed.get()==1 && surface.closed.get()==1, "Late completion leaked or double-released resources");
        check(releaseFails ? failure.get() instanceof IllegalStateException : failure.get()==null, "Release failure was hidden or fabricated");
    }
    private static void factoryFailure() throws Exception {
        CaptureRequests requests = new CaptureRequests(); requests.attach(1,1);
        SurfaceOwner surface = new SurfaceOwner();
        try { requests.create(1,surface,() -> { throw new IllegalArgumentException("Injected construction failure"); });
            throw new AssertionError("Construction failure was swallowed");
        } catch (IllegalArgumentException expected) { }
        check(surface.closed.get()==1, "Failed construction leaked its Surface");
        check(requests.attach(2,2), "Failure prevented a new generation");
        SurfaceOwner next = new SurfaceOwner();
        Mirror mirror = requests.create(2,next,() -> new Mirror(next));
        check(mirror!=null && next.closed.get()==0, "Usable mirror ownership was discarded");
        mirror.close();
    }
    private static void supersededAndClosedQueue() throws Exception {
        CaptureRequests requests = new CaptureRequests();
        requests.attach(1,1); requests.attach(2,2);
        SurfaceOwner old = new SurfaceOwner();
        check(requests.create(1,old,() -> { throw new AssertionError("Superseded factory ran"); })==null, "Superseded generation published");
        ArrayDeque<Runnable> pending = new ArrayDeque<>();
        ControlQueue queue = new ControlQueue(pending::offer,64);
        SurfaceOwner next = new SurfaceOwner();
        queue.post(() -> {
            try { check(requests.create(2,next,() -> { throw new AssertionError("Closed factory ran"); })==null,"Closed capture published"); }
            catch(Exception failure){throw new AssertionError(failure);}
        });
        requests.close(); queue.close(); pending.remove().run();
        check(old.closed.get()==1 && next.closed.get()==1, "Queued cancellation leaked Surfaces");
    }
    private static void lifetime() {
        HelperLifetime lifetime = new HelperLifetime(0,10000,2000);
        check(lifetime.check(9999)==HelperLifetime.Action.NONE,"Premature watchdog expiry");
        lifetime.heartbeat(5000);
        check(lifetime.check(14999)==HelperLifetime.Action.NONE,"Heartbeat did not extend liveness");
        check(lifetime.check(15000)==HelperLifetime.Action.STOP,"Missed heartbeat expiry");
        check(lifetime.stop(15000) && !lifetime.stop(16000),"Repeated shutdown extended its deadline");
        lifetime.heartbeat(16999);
        check(lifetime.check(16999)==HelperLifetime.Action.NONE && lifetime.check(17000)==HelperLifetime.Action.EXIT,
                "Heartbeats during shutdown prevented forced exit");
    }
    public static void main(String[] args) throws Exception {
        queueBounds(); cancellationBeforeCreate(); cancellationDuringCreate(false,false);
        cancellationDuringCreate(true,false); cancellationDuringCreate(false,true);
        factoryFailure(); supersededAndClosedQueue(); lifetime();
        System.out.println("PASS: 8 helper control groups (bounded queue, cancellation races, ownership faults, watchdog)");
    }
}
