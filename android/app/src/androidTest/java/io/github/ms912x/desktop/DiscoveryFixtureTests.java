package io.github.ms912x.desktop;

import android.net.nsd.*;
import java.net.InetAddress;
import java.util.*;
import static io.github.ms912x.desktop.AndroidFixtures.*;

final class DiscoveryFixtureTests {
    static final class Services implements AdbEndpointDiscovery.Services {
        final Map<String, NsdManager.DiscoveryListener> active = new LinkedHashMap<>();
        final List<NsdManager.ResolveListener> resolves = new ArrayList<>();
        int stops;
        boolean throwStart, throwStop;
        public void discover(String type, NsdManager.DiscoveryListener listener) {
            active.put(type, listener);
            if (throwStart) throw new IllegalArgumentException("start fixture");
            listener.onDiscoveryStarted(type);
        }
        public void resolve(NsdServiceInfo info, NsdManager.ResolveListener listener) { resolves.add(listener); }
        public void stop(NsdManager.DiscoveryListener listener) {
            ++stops;
            if (throwStop) throw new IllegalArgumentException("already stopped fixture");
            listener.onDiscoveryStopped("fixture");
        }
    }

    static void run() throws Exception {
        Services services = new Services(); List<String> failures = new ArrayList<>();
        List<AdbEndpointDiscovery.Endpoint> endpoints = new ArrayList<>();
        AdbEndpointDiscovery.Listener listener = new AdbEndpointDiscovery.Listener() {
            public void onEndpoint(AdbEndpointDiscovery.Endpoint e) { endpoints.add(e); }
            public void onFailure(String type, int code) { failures.add(type + code); }
        };
        AdbEndpointDiscovery discovery = new AdbEndpointDiscovery(services, listener);
        discovery.start();
        require(services.active.size() == 2, "Pairing and TLS service types");
        NsdManager.DiscoveryListener pairing = services.active.get(AdbEndpointDiscovery.PAIRING_SERVICE);
        NsdServiceInfo info = new NsdServiceInfo();
        info.setServiceType("_unrelated._tcp."); pairing.onServiceFound(info);
        require(services.resolves.isEmpty(), "Ignore unrelated service type");
        info.setServiceType(AdbEndpointDiscovery.PAIRING_SERVICE); info.setServiceName("fixture");
        pairing.onServiceFound(info); require(services.resolves.size() == 1, "Resolve matching service");
        NsdManager.ResolveListener resolve = services.resolves.get(0);
        resolve.onServiceResolved(info); require(endpoints.isEmpty(), "Missing host ignored");
        info.setHost(InetAddress.getByName("127.0.0.1"));
        info.setPort(0); resolve.onServiceResolved(info); require(endpoints.isEmpty(), "Invalid port ignored");
        info.setPort(37123); resolve.onServiceResolved(info);
        require(endpoints.size() == 1 && endpoints.get(0).port == 37123
                && endpoints.get(0).serviceName.equals("fixture")
                && endpoints.get(0).toString().contains("127.0.0.1:37123"), "Resolved endpoint fields");
        resolve.onResolveFailed(info, 3); require(failures.size() == 1, "Resolve failure");
        pairing.onServiceLost(info);
        pairing.onStartDiscoveryFailed("fixture", 4);
        require(failures.size() == 2 && services.stops == 1, "Start failure removes listener");
        NsdManager.DiscoveryListener connect = services.active.get(AdbEndpointDiscovery.CONNECT_SERVICE);
        connect.onStopDiscoveryFailed("fixture", 5);
        require(failures.size() == 3 && services.stops == 2, "Stop failure removes listener once");
        discovery.close(); discovery.close(); discovery.start();
        resolve.onServiceResolved(info); resolve.onResolveFailed(info, 6); pairing.onServiceFound(info);
        pairing.onStartDiscoveryFailed("fixture", 7); connect.onStopDiscoveryFailed("fixture", 8);
        require(endpoints.size() == 1 && failures.size() == 3 && services.stops == 2, "No callbacks/restarts after close");
        Services interrupted = new Services(); interrupted.throwStop = true;
        AdbEndpointDiscovery closing = new AdbEndpointDiscovery(interrupted, listener);
        closing.start(); closing.close(); closing.close();
        require(interrupted.stops == 2, "Already-stopped cleanup tolerated");
        Services failed = new Services(); failed.throwStart = true;
        AdbEndpointDiscovery starting = new AdbEndpointDiscovery(failed, listener);
        expect(IllegalArgumentException.class, starting::start); starting.close();
        require(failed.stops == 1, "Throwing registration cleaned up");
    }
}
