package io.github.ms912x.helper;

public final class TopologyLeaseTest {
    private static class State implements TopologyLease.Backend<String> {
        String value = "original";
        boolean failBeforeSet, failAfterSet;
        int writes;
        @Override public String get() { return value; }
        @Override public void set(String next) throws Exception {
            writes++;
            if (failBeforeSet) throw new Exception("Setter rejected");
            value = next;
            if (failAfterSet) throw new Exception("Reply lost");
        }
    }
    private static void check(boolean value, String detail) {
        if (!value) throw new AssertionError(detail);
    }
    public static void main(String[] args) throws Exception {
        State state = new State();
        TopologyLease<String> lease = new TopologyLease<>(state);
        check(lease.restore() == TopologyLease.Restoration.NONE && state.writes == 0, "Idle lease wrote topology");
        lease.apply("original", "placed");
        check(state.value.equals("placed"), "Placement not applied");
        check(lease.restore() == TopologyLease.Restoration.RESTORED && state.value.equals("original"), "Restoration failed");
        check(lease.restore() == TopologyLease.Restoration.NONE && state.writes == 2, "Repeated cleanup wrote topology");

        state = new State(); lease = new TopologyLease<>(state);
        state.value = "newer";
        try { lease.apply("original", "placed"); throw new AssertionError("Stale preparation accepted"); }
        catch (IllegalStateException expected) { check(state.writes == 0, "Stale preparation wrote topology"); }

        state = new State(); lease = new TopologyLease<>(state);
        lease.apply("original", "placed"); state.value = "newer";
        check(lease.restore() == TopologyLease.Restoration.SKIPPED && state.writes == 1
                && state.value.equals("newer"), "Cleanup replaced a newer topology");
        state.value = "placed";
        check(lease.restore() == TopologyLease.Restoration.NONE && state.writes == 1,
                "Cleanup regained ownership after a newer arrangement");

        for (boolean appliedBeforeFailure : new boolean[]{false, true}) {
            state = new State(); lease = new TopologyLease<>(state);
            state.failBeforeSet = !appliedBeforeFailure; state.failAfterSet = appliedBeforeFailure;
            try { lease.apply("original", "placed"); throw new AssertionError("Lost failure"); }
            catch (Exception expected) { check(expected.getMessage().equals(appliedBeforeFailure ? "Reply lost" : "Setter rejected"), "Unexpected failure"); }
            state.failBeforeSet = state.failAfterSet = false;
            check(lease.restore() == (appliedBeforeFailure ? TopologyLease.Restoration.RESTORED
                    : TopologyLease.Restoration.SKIPPED), "Partial Binder failure cleanup mismatch");
            check(state.value.equals("original"), "Original arrangement lost");
        }
        State altered = new State() {
            @Override public void set(String value) { writes++; this.value = "other arrangement"; }
        };
        lease = new TopologyLease<>(altered);
        try { lease.apply("original", "placed"); throw new AssertionError("Unexpected read-back accepted"); }
        catch (IllegalStateException expected) { /* Preserve the newer state on cleanup. */ }
        check(lease.restore() == TopologyLease.Restoration.SKIPPED && altered.writes == 1, "Unexpected read-back was overwritten");
        System.out.println("PASS: temporary topology restoration, stale state, newer layouts and partial Binder failures");
    }
}
