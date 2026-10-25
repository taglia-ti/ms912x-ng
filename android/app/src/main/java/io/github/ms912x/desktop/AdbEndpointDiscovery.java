package io.github.ms912x.desktop;

import android.content.Context;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;

import java.net.InetAddress;
import java.util.HashSet;
import java.util.ArrayDeque;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Predicate;

/** Discovers only the two Android wireless-debugging DNS-SD service types. */
final class AdbEndpointDiscovery implements AutoCloseable {
    static final String PAIRING_SERVICE = "_adb-tls-pairing._tcp.";
    static final String CONNECT_SERVICE = "_adb-tls-connect._tcp.";

    interface Listener {
        void onEndpoint(Endpoint endpoint);
        void onFailure(String serviceType, int errorCode);
        default void onEndpointLost(String serviceType,String serviceName) { }
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

    interface Services {
        void discover(String type, NsdManager.DiscoveryListener listener);
        void resolve(NsdServiceInfo info, NsdManager.ResolveListener listener);
        void stop(NsdManager.DiscoveryListener listener);
    }

    private final Services services;
    private final Listener listener;
    private final Predicate<InetAddress> allowed;
    private final ArrayDeque<Resolution> resolutions=new ArrayDeque<>();
    private Resolution resolving;
    private final AtomicBoolean closed = new AtomicBoolean();
    private final AtomicBoolean started = new AtomicBoolean();
    private final Set<NsdManager.DiscoveryListener> discoveries = new HashSet<>();

    AdbEndpointDiscovery(Context context, Listener listener) {
        this(context,listener,address->true);
    }
    AdbEndpointDiscovery(Context context,Listener listener,Predicate<InetAddress> allowed){
        this(frameworkServices(context),listener,allowed);
    }

    AdbEndpointDiscovery(Services services, Listener listener) {
        this(services,listener,address->true);
    }
    AdbEndpointDiscovery(Services services,Listener listener,Predicate<InetAddress> allowed){
        if (services == null || listener == null || allowed == null) throw new IllegalArgumentException("Discovery dependencies required");
        this.services = services;
        this.listener = listener;
        this.allowed=allowed;
    }

    @SuppressWarnings("deprecation")
    private static Services frameworkServices(Context context) {
        NsdManager manager = (NsdManager) context.getSystemService(Context.NSD_SERVICE);
        if (manager == null) throw new IllegalStateException("Android NSD service is unavailable");
        return new Services() {
            public void discover(String type, NsdManager.DiscoveryListener listener) {
                manager.discoverServices(type, NsdManager.PROTOCOL_DNS_SD, listener);
            }
            public void resolve(NsdServiceInfo info, NsdManager.ResolveListener listener) {
                manager.resolveService(info, listener);
            }
            public void stop(NsdManager.DiscoveryListener listener) { manager.stopServiceDiscovery(listener); }
        };
    }

    void start() {
        if(closed.get()||!started.compareAndSet(false,true))return;
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
                if(closed.get())return;
                String name=serviceInfo.getServiceName();
                synchronized(resolutions){
                    resolutions.removeIf(request->request.matches(serviceType,name));
                    if(resolving!=null&&resolving.matches(serviceType,name))resolving.lost=true;
                }
                listener.onEndpointLost(serviceType,name);
            }

            @Override
            public void onDiscoveryStopped(String type) {
            }

            @Override
            public void onStartDiscoveryFailed(String type, int errorCode) {
                if (!closed.get()) listener.onFailure(serviceType, errorCode);
                stopServiceDiscovery(this);
            }

            @Override
            public void onStopDiscoveryFailed(String type, int errorCode) {
                if (!closed.get()) listener.onFailure(serviceType, errorCode);
                stopServiceDiscovery(this);
            }
        };
        synchronized (discoveries) {
            if (closed.get()) {
                return;
            }
            discoveries.add(discovery);
        }
        try {
            services.discover(serviceType, discovery);
        } catch (RuntimeException failure) {
            stopServiceDiscovery(discovery);
            throw failure;
        }
    }

    private void resolve(String serviceType, NsdServiceInfo unresolved) {
        synchronized(resolutions){
            if(closed.get())return;
            String name=unresolved.getServiceName();
            if(resolving!=null&&!resolving.lost&&resolving.matches(serviceType,name))return;
            for(Resolution request:resolutions)if(request.matches(serviceType,name))return;
            resolutions.addLast(new Resolution(serviceType,unresolved));
        }
        resolveNext();
    }

    private static final class Resolution {
        final String type,name;
        final NsdServiceInfo info;
        boolean lost;
        Resolution(String type,NsdServiceInfo info){this.type=type;this.info=info;name=info.getServiceName();}
        boolean matches(String type,String name){return this.type.equals(type)&&java.util.Objects.equals(this.name,name);}
    }

    @SuppressWarnings("deprecation")
    private void resolveNext(){
        final Resolution request;
        synchronized(resolutions){
            if(closed.get()||resolving!=null||resolutions.isEmpty())return;
            request=resolutions.removeFirst();resolving=request;
        }
        NsdManager.ResolveListener result=new NsdManager.ResolveListener() {
            @Override
            public void onResolveFailed(NsdServiceInfo serviceInfo, int errorCode) {
                finishResolution(request,null,errorCode);
            }

            @Override
            public void onServiceResolved(NsdServiceInfo serviceInfo) {
                finishResolution(request,serviceInfo,0);
            }
        };
        try{services.resolve(request.info,result);}
        catch(RuntimeException failure){finishResolution(request,null,NsdManager.FAILURE_INTERNAL_ERROR);}
    }

    private void finishResolution(Resolution request,NsdServiceInfo info,int failure){
        boolean publish;
        synchronized(resolutions){
            if(resolving!=request)return;
            resolving=null;publish=!closed.get()&&!request.lost;
        }
        try{
            if(!publish)return;
            if(info==null){listener.onFailure(request.type,failure);return;}
            InetAddress address=info.getHost();int port=info.getPort();
            if(address!=null&&port>0&&port<=65535&&allowed.test(address))
                listener.onEndpoint(new Endpoint(request.type,request.name,address,port));
        }finally{resolveNext();}
    }

    private void stopServiceDiscovery(NsdManager.DiscoveryListener discovery) {
        synchronized (discoveries) {
            if (!discoveries.remove(discovery)) {
                return;
            }
        }
        try {
            services.stop(discovery);
        } catch (IllegalArgumentException ignored) {
            // Android may already have stopped a listener after a discovery error.
        }
    }

    @Override
    public void close() {
        if (!closed.compareAndSet(false, true)) {
            return;
        }
        synchronized(resolutions){resolutions.clear();resolving=null;}
        NsdManager.DiscoveryListener[] active;
        synchronized (discoveries) {
            active = discoveries.toArray(new NsdManager.DiscoveryListener[0]);
        }
        for (NsdManager.DiscoveryListener discovery : active) {
            stopServiceDiscovery(discovery);
        }
    }
}
