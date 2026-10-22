package io.github.ms912x.desktop;

import android.content.Context;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;

import java.net.InetAddress;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;

/** Discovers only the two Android wireless-debugging DNS-SD service types. */
final class AdbEndpointDiscovery implements AutoCloseable {
    static final String PAIRING_SERVICE = "_adb-tls-pairing._tcp.";
    static final String CONNECT_SERVICE = "_adb-tls-connect._tcp.";

    interface Listener {
        void onEndpoint(Endpoint endpoint);
        void onFailure(String serviceType, int errorCode);
    }

    static final class Endpoint {
        final String serviceType;
        final String serviceName;
        final InetAddress address;
        final int port;

        Endpoint(String serviceType, String serviceName,
                 InetAddress address, int port) {
            this.serviceType = serviceType;
            this.serviceName = serviceName;
            this.address = address;
            this.port = port;
        }

        @Override
        public String toString() {
            return serviceType + " " + address.getHostAddress() + ":" + port;
        }
    }

    private final NsdManager nsdManager;
    private final Listener listener;
    private final AtomicBoolean closed = new AtomicBoolean();
    private final Set<NsdManager.DiscoveryListener> discoveries = new HashSet<>();

    AdbEndpointDiscovery(Context context, Listener listener) {
        nsdManager = (NsdManager) context.getSystemService(Context.NSD_SERVICE);
        if (nsdManager == null) {
            throw new IllegalStateException("Android NSD service is unavailable");
        }
        this.listener = listener;
    }

    void start() {
        startServiceType(PAIRING_SERVICE);
        startServiceType(CONNECT_SERVICE);
    }

    private void startServiceType(String serviceType) {
        NsdManager.DiscoveryListener discovery = new NsdManager.DiscoveryListener() {
            @Override
            public void onDiscoveryStarted(String type) {
            }

            @Override
            public void onServiceFound(NsdServiceInfo serviceInfo) {
                if (!closed.get() && serviceType.equals(serviceInfo.getServiceType())) {
                    resolve(serviceType, serviceInfo);
                }
            }

            @Override
            public void onServiceLost(NsdServiceInfo serviceInfo) {
            }

            @Override
            public void onDiscoveryStopped(String type) {
            }

            @Override
            public void onStartDiscoveryFailed(String type, int errorCode) {
                listener.onFailure(serviceType, errorCode);
                stopServiceDiscovery(this);
            }

            @Override
            public void onStopDiscoveryFailed(String type, int errorCode) {
                listener.onFailure(serviceType, errorCode);
                stopServiceDiscovery(this);
            }
        };
        synchronized (discoveries) {
            if (closed.get()) {
                return;
            }
            discoveries.add(discovery);
        }
        nsdManager.discoverServices(serviceType, NsdManager.PROTOCOL_DNS_SD, discovery);
    }

    @SuppressWarnings("deprecation")
    private void resolve(String serviceType, NsdServiceInfo unresolved) {
        nsdManager.resolveService(unresolved, new NsdManager.ResolveListener() {
            @Override
            public void onResolveFailed(NsdServiceInfo serviceInfo, int errorCode) {
                if (!closed.get()) {
                    listener.onFailure(serviceType, errorCode);
                }
            }

            @Override
            public void onServiceResolved(NsdServiceInfo serviceInfo) {
                if (closed.get()) {
                    return;
                }
                InetAddress address = serviceInfo.getHost();
                int port = serviceInfo.getPort();
                if (address != null && port > 0 && port <= 65535) {
                    listener.onEndpoint(new Endpoint(serviceType,
                            serviceInfo.getServiceName(), address, port));
                }
            }
        });
    }

    private void stopServiceDiscovery(NsdManager.DiscoveryListener discovery) {
        synchronized (discoveries) {
            if (!discoveries.remove(discovery)) {
                return;
            }
        }
        try {
            nsdManager.stopServiceDiscovery(discovery);
        } catch (IllegalArgumentException ignored) {
            // Android may already have stopped a listener after a discovery error.
        }
    }

    @Override
    public void close() {
        if (!closed.compareAndSet(false, true)) {
            return;
        }
        NsdManager.DiscoveryListener[] active;
        synchronized (discoveries) {
            active = discoveries.toArray(new NsdManager.DiscoveryListener[0]);
        }
        for (NsdManager.DiscoveryListener discovery : active) {
            stopServiceDiscovery(discovery);
        }
    }
}
