package io.github.ms912x.helper;

/** Conditional restoration for a temporary experiment; callers serialize access. */
final class TopologyLease<T> {
    interface Backend<T> {
        T get() throws Exception;
        void set(T value) throws Exception;
    }
    enum Restoration { NONE, RESTORED, SKIPPED }
    private final Backend<T> backend;
    private T original, proposed;

    TopologyLease(Backend<T> backend) { this.backend = backend; }

    void apply(T expected, T candidate) throws Exception {
        if (proposed != null) throw new IllegalStateException("Placement already attempted");
        if (!expected.equals(backend.get())) throw new IllegalStateException("Topology changed during preparation; retry");
        original = expected;
        // A Binder call can fail after the service applied the request.
        proposed = candidate;
        backend.set(candidate);
        if (!candidate.equals(backend.get())) {
            throw new IllegalStateException("Topology changed during placement; leaving newer state intact");
        }
    }

    Restoration restore() throws Exception {
        if (proposed == null) return Restoration.NONE;
        if (!proposed.equals(backend.get())) {
            proposed = null;
            original = null;
            return Restoration.SKIPPED;
        }
        backend.set(original);
        if (!original.equals(backend.get())) throw new IllegalStateException("Restoration could not be confirmed");
        proposed = null;
        original = null;
        return Restoration.RESTORED;
    }
}
