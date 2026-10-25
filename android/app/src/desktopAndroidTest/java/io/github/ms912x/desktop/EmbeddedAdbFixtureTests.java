package io.github.ms912x.desktop;

import android.app.Instrumentation;
import android.app.ActivityOptions;
import android.content.Intent;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.accessibility.AccessibilityNodeInfo;
import java.util.ArrayList;
import java.util.List;
import static io.github.ms912x.desktop.AndroidFixtures.require;
import static io.github.ms912x.desktop.OutputFixtureTests.main;

/** Optional actual same-phone pairing. Pairing secrets never leave this process. */
public final class EmbeddedAdbFixtureTests {
    private static void collect(AccessibilityNodeInfo node,List<AccessibilityNodeInfo> nodes){
        if(node==null)return;nodes.add(node);
        for(int i=0;i<node.getChildCount();++i)collect(node.getChild(i),nodes);
    }
    private static List<AccessibilityNodeInfo> nodes(Instrumentation test){
        List<AccessibilityNodeInfo> nodes=new ArrayList<>();collect(test.getUiAutomation().getRootInActiveWindow(),nodes);return nodes;
    }
    private static boolean click(AccessibilityNodeInfo node){
        while(node!=null&&!node.isClickable())node=node.getParent();
        return node!=null&&node.performAction(AccessibilityNodeInfo.ACTION_CLICK);
    }
    public static void run(Instrumentation test,Bundle args) throws Exception {
        String host=args.getString("connectHost");int port=Integer.parseInt(args.getString("connectPort","0"));
        require(host!=null&&port>0,"Actual local TLS endpoint required");
        long deadline;
        AdbCredentialStore store=new AdbCredentialStore(test.getTargetContext());
        require(store.isLocalAddress(host),"Endpoint belongs to this phone");
        if(!args.getString("reconnectOnly","false").equals("true")){
        // Host navigation may stop at Wireless debugging; only this process opens/reads the secret dialog.
        if(!args.getString("wirelessPageReady","false").equals("true")){
        ActivityOptions settingsDisplay=ActivityOptions.makeBasic();settingsDisplay.setLaunchDisplayId(0);
        test.getTargetContext().startActivity(new Intent(android.provider.Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),settingsDisplay.toBundle());
        Thread.sleep(500);
        for(int step=0;step<25;++step){
            boolean moved=false;
            List<AccessibilityNodeInfo> scrollNodes=nodes(test);java.util.Collections.reverse(scrollNodes);
            for(AccessibilityNodeInfo node:scrollNodes)if(node.isScrollable()){
                moved=node.performAction(AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD);break;
            }
            if(!moved)break;Thread.sleep(80);
        }
        boolean opened=false;
        for(int step=0;step<20&&!opened;++step){
            Thread.sleep(400);
            for(AccessibilityNodeInfo node:nodes(test)){
                String text=String.valueOf(node.getText());
                if(text.equals("Depuração sem fio")||text.equals("Depuração por Wi-Fi")||text.equals("Wireless debugging")){opened=click(node);break;}
            }
            if(!opened){
                List<AccessibilityNodeInfo> scrollNodes=nodes(test);java.util.Collections.reverse(scrollNodes);
                for(AccessibilityNodeInfo node:scrollNodes)if(node.isScrollable()){
                    node.performAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD);break;
                }
            }
        }
        require(opened,"Wireless debugging Settings entry");
        }
        Thread.sleep(600);
        boolean pairing=false;int candidates=0,visible=0;
        for(int attempt=0;attempt<15&&!pairing;++attempt){
            List<AccessibilityNodeInfo> page=nodes(test);visible=page.size();
            for(AccessibilityNodeInfo node:page){
                String text=String.valueOf(node.getText()).toLowerCase(java.util.Locale.ROOT);
                if((text.contains("código")&&text.contains("pare"))||text.contains("pairing code")){
                    ++candidates;pairing=click(node);if(pairing)break;
                }
            }
            if(!pairing)Thread.sleep(200);
        }
        require(pairing,"Pairing-code Settings action; nodes="+visible+", candidates="+candidates);
        String code=null;int pairingPort=0;deadline=SystemClock.elapsedRealtime()+10_000;
        while((code==null||pairingPort==0)&&SystemClock.elapsedRealtime()<deadline){
            Thread.sleep(200);
            for(AccessibilityNodeInfo node:nodes(test)){
                String text=String.valueOf(node.getText());
                if(text.matches("[0-9]{6}|[0-9]{3} [0-9]{3}"))code=text.replace(" ","");
                if(text.startsWith(host+":")){
                    try{pairingPort=Integer.parseInt(text.substring(host.length()+1).trim());}catch(NumberFormatException ignored){}
                }
            }
        }
        require(code!=null&&pairingPort>0,"Transient pairing dialog fields available");
        String result=NativeAdbBridge.nativePairAndVerify(store,host,pairingPort,host,port,code);
        code=null;
        require(result.startsWith("Paired device ")&&result.contains("uid=2000"),"Same-phone pairing/TLS verification succeeded: "+result);
        }
        require(store.pairedDeviceFor(host)!=null,"Paired identity persisted for reconnect");
        ActivityOptions phone=ActivityOptions.makeBasic();phone.setLaunchDisplayId(0);
        MainActivity activity=(MainActivity)test.startActivitySync(new Intent(test.getTargetContext(),MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),phone.toBundle());
        ActivityFixtureTests.awaitService(test,activity);
        SessionCoordinator owner=main(test,()->ActivityFixtureTests.screencast(activity).coordinator());
        try{
            main(test,()->{owner.connectHelper(host,port);return null;});
            deadline=SystemClock.elapsedRealtime()+60_000;
            while(!main(test,()->owner.snapshot().helperConnected)){
                require(SystemClock.elapsedRealtime()<deadline,"Embedded helper bootstrap: "+main(test,()->owner.snapshot().helperStatus));Thread.sleep(100);
            }
            require(main(test,()->!owner.snapshot().sources.isEmpty()),"Embedded helper enumerated source displays");
            main(test,()->{owner.disconnectHelper();return null;});
            CaptureFixtureTests.waitFor(()->!owner.snapshot().helperConnected&&!owner.snapshot().helperBusy,"Embedded helper process and stream retire");
            main(test,()->{activity.startDiscovery();return null;});
            deadline=SystemClock.elapsedRealtime()+20_000;
            while(!main(test,()->{
                AdbToolsController tools=(AdbToolsController)ActivityFixtureTests.field(activity,"adbToolsController");
                for(AdbEndpointDiscovery.Endpoint endpoint:tools.connectEndpoints)
                    if(host.equals(endpoint.address.getHostAddress())&&port==endpoint.port)return true;
                return false;
            })){
                require(SystemClock.elapsedRealtime()<deadline,"Actual NSD discovers this phone's TLS endpoint");Thread.sleep(100);
            }
            main(test,()->{require(ActivityFixtureTests.screen(activity).connectHelperButton.performClick(),"Helper Connect action");return null;});
            boolean selected=false;deadline=SystemClock.elapsedRealtime()+5000;
            while(!selected&&SystemClock.elapsedRealtime()<deadline){
                for(AccessibilityNodeInfo node:nodes(test))if((host+":"+port).equals(String.valueOf(node.getText()))){selected=click(node);if(selected)break;}
                if(!selected)Thread.sleep(100);
            }
            require(selected,"Discovered endpoint picker selection");

            deadline=SystemClock.elapsedRealtime()+60_000;
            while(!main(test,()->owner.snapshot().helperConnected)){
                require(SystemClock.elapsedRealtime()<deadline,"Stored-credential reconnect/bootstrap");Thread.sleep(100);
            }
            main(test,()->{owner.disconnectHelper();return null;});
            CaptureFixtureTests.waitFor(()->!owner.snapshot().helperConnected&&!owner.snapshot().helperBusy,"Reconnected helper cleanup");
        }finally{main(test,()->{owner.disconnectHelper();activity.finish();return null;});}
    }
}
