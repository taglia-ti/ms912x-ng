package io.github.ms912x.desktop;

import java.util.List;

/** Service-owned helper connection plus the selected capture producer. */
interface HelperBackend extends CaptureSource {
    void prepare();
    void refreshInventory();
    boolean connected();
    boolean isActive();
    List<SourceCapability> sources();
    void setObserver(Runnable observer);
    void shutdown(Runnable completed);
    void close();
}
