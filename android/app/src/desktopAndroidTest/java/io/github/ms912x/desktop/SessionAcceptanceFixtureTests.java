package io.github.ms912x.desktop;

import android.app.Activity;
import android.app.ActivityOptions;
import android.app.Instrumentation;
import android.app.NotificationManager;
import android.content.Intent;
import android.os.Bundle;
import android.service.notification.StatusBarNotification;
import android.widget.Button;
import android.widget.Spinner;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicReference;
import org.json.JSONObject;
import static io.github.ms912x.desktop.AndroidFixtures.require;
import static io.github.ms912x.desktop.OutputFixtureTests.main;

/** Optional hardware gate: real USB, shell helper, service and Activity recreation. */
public final class SessionAcceptanceFixtureTests {
    private static Instrumentation test;
    private static File control;
    private static void phase(String name,SessionCoordinator owner) throws Exception {
        SessionCoordinator.Snapshot state=main(test,owner::snapshot);
        CaptureController capture=main(test,()->owner.source()==null?null:owner.source().capture());
        JSONObject report=new JSONObject().put("phase",name).put("state",state.phase.toString())
                .put("generation",state.generation).put("source_id",state.sourceId).put("created_display_id",state.createdDisplayId)
                .put("mode",String.valueOf(state.mode)).put("output",state.output)
                .put("capture_frames",capture==null?0:capture.frameVersion()).put("usb_status",state.outputStatus)
                .put("monitor",state.capabilities==null?"":state.capabilities.monitorName)
                .put("modes",state.capabilities==null?"":state.capabilities.modes.toString());
        try(java.io.FileOutputStream file=test.getTargetContext().openFileOutput("session-acceptance.json",0)){
            file.write(report.toString(2).getBytes(StandardCharsets.UTF_8));
        }
    }
    private static void host(String stage,SessionCoordinator owner) throws Exception {
        if(control.exists())require(control.delete(),"Remove previous host acknowledgement");
        phase(stage,owner);
        long deadline=android.os.SystemClock.elapsedRealtime()+90_000;
        while(!control.exists()){
            require(android.os.SystemClock.elapsedRealtime()<deadline,"Host acceptance acknowledgement timed out: "+stage);
            require(main(test,owner::active),"Session stopped during hardware acceptance: "+owner.snapshot().status);
            Thread.sleep(50);
        }
    }
    private static void advancing(SessionCoordinator owner,CaptureController capture,long before) throws Exception {
        CaptureFixtureTests.waitFor(()->capture.frameVersion()>before,"Capture advances outside the Activity");
        require(main(test,()->owner.snapshot().phase==SessionCoordinator.Phase.STREAMING&&owner.source().capture()==capture),"Same service-owned reader remains streaming");
    }
    private static void chooseApp(Instrumentation test,String packageName) throws Exception {
        android.accessibilityservice.AccessibilityServiceInfo info=test.getUiAutomation().getServiceInfo();
        info.flags|=android.accessibilityservice.AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS;
        test.getUiAutomation().setServiceInfo(info);
        for(int attempt=0;attempt<35;++attempt){
            java.util.List<android.view.accessibility.AccessibilityWindowInfo> windows=test.getUiAutomation().getWindowsOnAllDisplays().get(0);
            if(windows!=null)for(android.view.accessibility.AccessibilityWindowInfo window:windows){
                android.view.accessibility.AccessibilityNodeInfo root=window.getRoot();if(root==null)continue;
                for(android.view.accessibility.AccessibilityNodeInfo node:root.findAccessibilityNodeInfosByText(packageName)){
                    while(node!=null&&!node.isClickable())node=node.getParent();
                    if(node!=null&&node.isEnabled()&&node.performAction(android.view.accessibility.AccessibilityNodeInfo.ACTION_CLICK))return;
                }
                java.util.ArrayDeque<android.view.accessibility.AccessibilityNodeInfo> pending=new java.util.ArrayDeque<>();pending.add(root);
                while(!pending.isEmpty()){
                    android.view.accessibility.AccessibilityNodeInfo node=pending.remove();
                    if(node.isScrollable()&&node.performAction(android.view.accessibility.AccessibilityNodeInfo.ACTION_SCROLL_FORWARD))break;
                    for(int i=0;i<node.getChildCount();++i){android.view.accessibility.AccessibilityNodeInfo child=node.getChild(i);if(child!=null)pending.add(child);}
                }
            }
            Thread.sleep(200);
        }
        throw new AssertionError("Installed app is absent from the created-screen picker: "+packageName);
    }

    public static void run(Instrumentation instrumentation,Bundle arguments) throws Exception {
        test=instrumentation;control=new File(test.getTargetContext().getFilesDir(),"session-acceptance.continue");
        if(control.exists())require(control.delete(),"Remove stale host acknowledgement");
        final boolean phoneMirror=arguments.getString("sourceKind","helper").equals("phone");
        final boolean createDesktop=arguments.getString("sourceKind","helper").equals("desktop");
        final int sourceId=Integer.parseInt(arguments.getString("sourceId","-1"));
        final int width=Integer.parseInt(arguments.getString("outputWidth","1920"));
        final int height=Integer.parseInt(arguments.getString("outputHeight","1080"));
        ActivityOptions phone=ActivityOptions.makeBasic();phone.setLaunchDisplayId(0);
        AtomicReference<MainActivity> current=new AtomicReference<>((MainActivity)test.startActivitySync(
                new Intent(test.getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),phone.toBundle()));
        MainActivity activity=current.get();ActivityFixtureTests.awaitService(test,activity);
        SessionCoordinator owner=main(test,()->ActivityFixtureTests.screencast(activity).coordinator());
        try{
            CaptureFixtureTests.waitFor(()->!owner.usb().isBusy(),"Actual USB permission and EDID probe");
            require(main(test,()->owner.snapshot().capabilities!=null&&owner.usb().hasGrantedAdapter()),"Real adapter permission and EDID modes required");
            if(phoneMirror){phase("projection_consent",owner);}
            else{
                String bootstrapHost=arguments.getString("bootstrapHost");
                if(bootstrapHost==null){
                    main(test,()->{ActivityFixtureTests.screencast(activity).prepareHelper();return null;});
                    phase("prepare_helper",owner);
                }else{
                    phase("prepare_embedded",owner);
                    int bootstrapPort=Integer.parseInt(arguments.getString("bootstrapPort","0"));
                    main(test,()->{ActivityFixtureTests.screencast(activity).connectHelper(bootstrapHost,bootstrapPort);return null;});
                }
                long deadline=android.os.SystemClock.elapsedRealtime()+60_000;
                while(!main(test,()->owner.snapshot().helperConnected)){
                    require(android.os.SystemClock.elapsedRealtime()<deadline,"PC helper connection timeout");Thread.sleep(50);
                }
            }
            main(test,()->{
                ActivityFixtureTests.selection(activity).refreshDisplayInventory();
                ActivityFixtureTests.select(activity,sourceId);
                Spinner resolutions=ActivityFixtureTests.screen(activity).modeSelector;
                boolean found=false;
                for(int i=0;i<resolutions.getCount();++i)if(resolutions.getItemAtPosition(i).toString().startsWith(width+"×"+height+" ")){resolutions.setSelection(i);found=true;}
                require(found,"Requested EDID output resolution");
                ActivityFixtureTests.call(ActivityFixtureTests.selection(activity),"rebuildRefreshRates");
                Spinner rates=ActivityFixtureTests.screen(activity).rateSelector;
                for(int i=0;i<rates.getCount();++i)if("60 Hz".equals(rates.getItemAtPosition(i).toString())){rates.setSelection(i);}
                // Prefer 60 Hz when advertised; never force a mode outside the connected EDID.
                Button start=ActivityFixtureTests.screen(activity).screencastButton;
                require(start.isEnabled()&&start.performClick(),"Normal main-screen Start action");return null;
            });
            CaptureFixtureTests.waitFor(()->owner.snapshot().phase==SessionCoordinator.Phase.STREAMING||!owner.active(),"Actual first completed USB batch");
            require(owner.snapshot().phase==SessionCoordinator.Phase.STREAMING,"Desktop startup: "+owner.snapshot().status+" / "+owner.snapshot().helperStatus);
            int ownedDisplay=owner.snapshot().createdDisplayId;
            if(createDesktop){
                require(ownedDisplay>0,"Helper created a new independent display");
                main(test,()->{owner.launchApp(test.getTargetContext().getPackageName()+".test/io.github.ms912x.desktop.DesktopGeometryFixtureActivity");return null;});
                CaptureFixtureTests.waitFor(()->owner.snapshot().launchStatus.contains("App launched"),"Helper launches a separate application on its owned display");
            }
            CaptureController capture=main(test,()->owner.source().capture());
            host("streaming",owner);
            long before=capture.frameVersion();
            main(test,()->{activity.onBackPressed();return null;});
            advancing(owner,capture,before);host("background",owner);
            main(test,()->{activity.startActivity(new Intent(activity,MainActivity.class)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));return null;});
            CaptureFixtureTests.waitFor(()->activity.hasWindowFocus(),"Return to Activity");
            Instrumentation.ActivityMonitor monitor=test.addMonitor(MainActivity.class.getName(),null,false);
            try{
                before=capture.frameVersion();
                main(test,()->{activity.recreate();return null;});
                MainActivity replacement=(MainActivity)monitor.waitForActivityWithTimeout(10_000);
                require(replacement!=null&&replacement!=activity,"Activity recreated");current.set(replacement);
                ActivityFixtureTests.awaitService(test,replacement);
                require(main(test,()->ActivityFixtureTests.screencast(replacement).coordinator()==owner),"Rebound to the same session owner");
                require(main(test,()->ActivityFixtureTests.screen(replacement).sourceSelector.getSelectedItem() instanceof ScreenSource
                        &&((ScreenSource)ActivityFixtureTests.screen(replacement).sourceSelector.getSelectedItem()).id==sourceId),"Active source restored after recreation");
                if(createDesktop)require(owner.snapshot().createdDisplayId==ownedDisplay,"Created display survives Activity recreation");
                advancing(owner,capture,before);host("recreated",owner);
            }finally{test.removeMonitor(monitor);}
            String launchPackage=arguments.getString("launchPackage");
            if(createDesktop&&launchPackage!=null){
                main(test,()->{require(ActivityFixtureTests.screen(current.get()).openAppButton.performClick(),"Open-app action on the created screen");return null;});
                chooseApp(test,launchPackage);
                CaptureFixtureTests.waitFor(()->owner.snapshot().launchStatus.contains("App launched"),"Selected app starts on the independent screen");
                host("app_launched",owner);
            }
            if(arguments.getString("dropStream","false").equals("true")){
                main(test,()->{
                    Object backend=ActivityFixtureTests.field(owner,"helper");
                    Object embedded=ActivityFixtureTests.field(backend,"embedded");
                    long handle=(long)ActivityFixtureTests.field(embedded,"handle");
                    require(handle>0,"Actual retained embedded ADB stream");
                    NativeAdbBridge.nativeCloseHelper(handle);return null;
                });
            }else main(test,()->{
                StatusBarNotification[] notifications=test.getTargetContext().getSystemService(NotificationManager.class).getActiveNotifications();
                boolean stopped=false;
                for(StatusBarNotification notification:notifications)if(notification.getId()==42){
                    require(notification.getNotification().actions!=null&&notification.getNotification().actions.length==1,"Session Stop notification action");
                    notification.getNotification().actions[0].actionIntent.send();stopped=true;
                }
                require(stopped,"Real connected-device foreground notification is present");return null;
            });
            CaptureFixtureTests.waitFor(()->!owner.active(),"Notification Stop drains USB, releases mirror/reader and retires helper");
            require(!capture.isOpen()&&!main(test,()->owner.snapshot().helperConnected||owner.snapshot().helperBusy),"Reader closed and helper retired");
            if(createDesktop)require(test.getTargetContext().getSystemService(android.hardware.display.DisplayManager.class).getDisplay(ownedDisplay)==null,"Owned desktop removed after Stop");
            phase("passed",owner);
        }finally{
            main(test,()->{if(owner.active())owner.stop("Hardware fixture ended.");if(current.get()!=null)current.get().finish();return null;});
        }
    }
}
