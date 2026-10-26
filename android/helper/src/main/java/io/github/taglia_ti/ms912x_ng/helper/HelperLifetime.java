package io.github.taglia_ti.ms912x_ng.helper;

/** Monotonic watchdog state, independent of framework operations and incoming images. */
final class HelperLifetime {
    enum Action { NONE, STOP, EXIT }
    private final long heartbeatTimeout, stopGrace;
    private long heartbeat, stoppingAt = -1;

    HelperLifetime(long now, long heartbeatTimeout, long stopGrace) {
        heartbeat = now; this.heartbeatTimeout = heartbeatTimeout; this.stopGrace = stopGrace;
    }
    synchronized void heartbeat(long now) { if (stoppingAt < 0) heartbeat = now; }
    synchronized boolean stop(long now) {
        if (stoppingAt >= 0) return false;
        stoppingAt = now;
        return true;
    }
    synchronized Action check(long now) {
        if (stoppingAt >= 0) return now - stoppingAt >= stopGrace ? Action.EXIT : Action.NONE;
        return now - heartbeat >= heartbeatTimeout ? Action.STOP : Action.NONE;
    }
}
