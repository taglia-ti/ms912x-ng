package io.github.ms912x.helper;

/** Bounds running plus queued framework operations, including their Surface/Binder references. */
final class ControlQueue {
    interface Poster { boolean post(Runnable operation); }
    private final Poster poster;
    private final int capacity;
    private int pending;
    private boolean closed;

    ControlQueue(Poster poster, int capacity) { this.poster = poster; this.capacity = capacity; }

    synchronized boolean post(Runnable operation) {
        if (closed || pending >= capacity) return false;
        ++pending;
        boolean accepted;
        try {
            accepted = poster.post(() -> {
                try { operation.run(); }
                finally { synchronized (ControlQueue.this) { --pending; } }
            });
        } catch (RuntimeException failure) { --pending; throw failure; }
        if (!accepted) --pending;
        return accepted;
    }

    synchronized void close() { closed = true; }
}
