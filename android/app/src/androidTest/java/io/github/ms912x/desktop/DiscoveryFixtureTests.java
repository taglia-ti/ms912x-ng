package io.github.ms912x.desktop;

import android.net.nsd.*;
import java.net.InetAddress;
import java.util.*;
import static io.github.ms912x.desktop.AndroidFixtures.*;

final class DiscoveryFixtureTests {
    static final class Services implements AdbEndpointDiscovery.Services {
        final Map<String, NsdManager.DiscoveryListener> active = new LinkedHashMap<>();
        final List<NsdManager.ResolveListener> resolves = new ArrayList<>();
        int stops,starts;
        boolean throwStart, throwStop, throwResolve;
        public void discover(String type, NsdManager.DiscoveryListener listener) {
            starts++;
            active.put(type, listener);
            if (throwStart) throw new IllegalArgumentException("start fixture");
            listener.onDiscoveryStarted(type);
        }
        public void resolve(NsdServiceInfo info, NsdManager.ResolveListener listener) {
            if(throwResolve)throw new IllegalArgumentException("resolve fixture");
            resolves.add(listener);
        }
        public void stop(NsdManager.DiscoveryListener listener) {
            ++stops;
            if (throwStop) throw new IllegalArgumentException("already stopped fixture");
            listener.onDiscoveryStopped("fixture");
        }
    }

    private static NsdServiceInfo info(String type,String name,String host,int port) throws Exception {
        NsdServiceInfo info=new NsdServiceInfo();info.setServiceType(type);info.setServiceName(name);
        if(host!=null)info.setHost(InetAddress.getByName(host));info.setPort(port);return info;
    }

    static void run() throws Exception {
        Services services=new Services();List<String> failures=new ArrayList<>(),lost=new ArrayList<>();
        List<AdbEndpointDiscovery.Endpoint> endpoints=new ArrayList<>();
        AdbEndpointDiscovery.Listener listener=new AdbEndpointDiscovery.Listener(){
            public void onEndpoint(AdbEndpointDiscovery.Endpoint e){endpoints.add(e);}
            public void onFailure(String type,int code){failures.add(type+code);}
            public void onEndpointLost(String type,String name){lost.add(type+name);}
        };
        AdbEndpointDiscovery discovery=new AdbEndpointDiscovery(services,listener,InetAddress::isLoopbackAddress);
        discovery.start();discovery.start();require(services.active.size()==2&&services.starts==2,"Idempotent pairing and TLS discovery registration");
        NsdManager.DiscoveryListener pairing=services.active.get(AdbEndpointDiscovery.PAIRING_SERVICE);
        NsdManager.DiscoveryListener connect=services.active.get(AdbEndpointDiscovery.CONNECT_SERVICE);
        pairing.onServiceFound(info("_unrelated._tcp.","other",null,0));
        require(services.resolves.isEmpty(),"Unrelated services are ignored");
        NsdServiceInfo missing=info(AdbEndpointDiscovery.PAIRING_SERVICE,"missing",null,1);
        pairing.onServiceFound(missing);services.resolves.get(0).onServiceResolved(missing);
        require(endpoints.isEmpty(),"Missing host ignored");
        NsdServiceInfo good=info(AdbEndpointDiscovery.PAIRING_SERVICE,"pair","127.0.0.1",37123);
        NsdServiceInfo tls=info(AdbEndpointDiscovery.CONNECT_SERVICE,"tls","127.0.0.1",40000);
        pairing.onServiceFound(good);connect.onServiceFound(tls);connect.onServiceFound(tls);
        require(services.resolves.size()==2,"Legacy NSD resolves are serialized and deduplicated");
        services.resolves.get(1).onServiceResolved(good);
        require(endpoints.size()==1&&endpoints.get(0).serviceName.equals("pair")&&endpoints.get(0).port==37123,
                "Resolved endpoint retains service identity");
        require(services.resolves.size()==3,"Next resolve starts after completion");
        services.resolves.get(2).onResolveFailed(tls,3);
        require(failures.size()==1,"Resolve failure is reported");
        NsdServiceInfo remote=info(AdbEndpointDiscovery.CONNECT_SERVICE,"remote","192.0.2.1",40000);
        connect.onServiceFound(remote);services.resolves.get(3).onServiceResolved(remote);
        require(endpoints.size()==1,"Endpoints outside the phone address policy are excluded");
        NsdServiceInfo invalid=info(AdbEndpointDiscovery.PAIRING_SERVICE,"invalid","127.0.0.1",0);
        pairing.onServiceFound(invalid);services.resolves.get(4).onServiceResolved(invalid);
        require(endpoints.size()==1,"Invalid port ignored");
        NsdServiceInfo vanished=info(AdbEndpointDiscovery.PAIRING_SERVICE,"vanished","127.0.0.1",1234);
        pairing.onServiceFound(vanished);connect.onServiceFound(tls);
        pairing.onServiceLost(vanished);connect.onServiceLost(tls);
        services.resolves.get(5).onServiceResolved(vanished);
        require(endpoints.size()==1&&services.resolves.size()==6&&lost.size()==2,
                "Lost active and queued services cannot return stale endpoints");
        services.throwResolve=true;pairing.onServiceFound(good);services.throwResolve=false;
        connect.onServiceFound(tls);require(services.resolves.size()==7,"Throwing resolve does not stall the queue");
        discovery.close();discovery.close();discovery.start();
        services.resolves.get(6).onServiceResolved(tls);pairing.onServiceFound(good);
        require(endpoints.size()==1&&services.stops==2&&services.resolves.size()==7,"Closed discovery ignores late callbacks and restart");
        Services stopped=new Services();stopped.throwStop=true;
        AdbEndpointDiscovery closing=new AdbEndpointDiscovery(stopped,listener);closing.start();closing.close();closing.close();
        require(stopped.stops==2,"Already-stopped cleanup tolerated");
        Services failed=new Services();failed.throwStart=true;
        AdbEndpointDiscovery starting=new AdbEndpointDiscovery(failed,listener);
        expect(IllegalArgumentException.class,starting::start);starting.close();
        require(failed.stops==1,"Throwing registration cleaned up");
        Services cycling=new Services();List<AdbEndpointDiscovery.Endpoint> refreshed=new ArrayList<>();
        AdbEndpointDiscovery again=new AdbEndpointDiscovery(cycling,new AdbEndpointDiscovery.Listener(){
            public void onEndpoint(AdbEndpointDiscovery.Endpoint endpoint){refreshed.add(endpoint);}
            public void onFailure(String type,int error){throw new AssertionError("Unexpected rediscovery failure");}
        });
        again.start();NsdManager.DiscoveryListener found=cycling.active.get(AdbEndpointDiscovery.CONNECT_SERVICE);
        NsdServiceInfo changed=info(AdbEndpointDiscovery.CONNECT_SERVICE,"tls","127.0.0.1",40001);
        found.onServiceFound(tls);found.onServiceLost(tls);found.onServiceFound(changed);
        cycling.resolves.get(0).onServiceResolved(tls);cycling.resolves.get(1).onServiceResolved(changed);
        require(refreshed.size()==1&&refreshed.get(0).port==40001,"Lost/refound service resolves its new port after the stale result");
        again.close();
    }
}
