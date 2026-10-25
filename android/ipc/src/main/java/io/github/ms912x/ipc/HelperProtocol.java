package io.github.ms912x.ipc;

public final class HelperProtocol {
    public static final int VERSION = 4;
    public static final int SHELL_UID = 2000;
    public static final int READY = 0, RELEASED = 1, FAILED = 2, SOURCE_REMOVED = 3;
    public static final int CREATE_SCREEN = -1;
    public static final int APP_LAUNCHED = 7, APP_FAILED = 8;
    public static final int GEOMETRY_CHANGED = 4;
    public static final int CANCELLED = 5, BUSY = 6;
    public static final long HEARTBEAT_MS = 2000, WATCHDOG_MS = 10000;
    public static final long STOP_GRACE_MS = 2000, REQUEST_TIMEOUT_MS = 5000;
    private HelperProtocol() {}
}
