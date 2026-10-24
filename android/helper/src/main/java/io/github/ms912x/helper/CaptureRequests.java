package io.github.ms912x.helper;

/** Cross-thread cancellation fence. Resource creation/release stays on the serial control thread. */
final class CaptureRequests {
    private long lastRequest, lastAttachGeneration, cancelledThrough, lastCancelRequest, lastCancelGeneration;
    private boolean closed;

    synchronized boolean attach(long request, long generation) {
        if (closed || request <= lastRequest || generation <= lastAttachGeneration
                || generation <= cancelledThrough || request <= 0 || generation <= 0) return false;
        lastRequest = request;
        lastAttachGeneration = generation;
        return true;
    }

    synchronized boolean cancel(long request, long generation) {
        if (closed || request <= 0 || generation <= 0) return false;
        if (request == lastCancelRequest && generation == lastCancelGeneration) return true;
        if (request <= lastRequest) return false;
        lastRequest = lastCancelRequest = request;
        lastCancelGeneration = generation;
        cancelledThrough = Math.max(cancelledThrough, generation);
        return true;
    }

    synchronized boolean current(long generation) {
        return !closed && generation > cancelledThrough && generation == lastAttachGeneration;
    }

    synchronized void close() { closed = true; }

    interface Factory<T> { T create() throws Exception; }

    <T extends AutoCloseable> T create(long generation, AutoCloseable surface, Factory<T> factory) throws Exception {
        boolean transferred = false;
        try {
            if (!current(generation)) return null;
            T owned = factory.create();
            if (owned == null) throw new IllegalStateException("Capture factory returned no owner");
            transferred = true;
            if (!current(generation)) { owned.close(); return null; }
            return owned;
        } finally {
            if (!transferred) surface.close();
        }
    }
}
