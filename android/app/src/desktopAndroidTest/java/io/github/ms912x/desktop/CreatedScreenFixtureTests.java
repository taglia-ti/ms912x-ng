package io.github.ms912x.desktop;

import android.app.ActivityOptions;
import android.app.Instrumentation;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.SystemClock;
import java.nio.charset.StandardCharsets;
import org.json.JSONObject;
import static io.github.ms912x.desktop.AndroidFixtures.require;
import static io.github.ms912x.desktop.OutputFixtureTests.main;

/** Real shell-owned creation on API 30+, without a USB fixture or a host-created source. */
public final class CreatedScreenFixtureTests {
    public static void run(Instrumentation test,Bundle arguments) throws Exception {
        ActivityOptions phone=ActivityOptions.makeBasic();phone.setLaunchDisplayId(0);
        MainActivity activity=(MainActivity)test.startActivitySync(new Intent(test.getTargetContext(),MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),phone.toBundle());
        ActivityFixtureTests.awaitService(test,activity);
        SessionCoordinator owner=main(test,()->ActivityFixtureTests.screencast(activity).coordinator());
        int id=-1;
        try{
            main(test,()->{owner.prepareHelper();return null;});
            try(java.io.FileOutputStream file=test.getTargetContext().openFileOutput("created-screen-ready.json",0)){
                file.write("{\"ready\":true}".getBytes(StandardCharsets.US_ASCII));
            }
            long deadline=SystemClock.elapsedRealtime()+60_000;
            while(!main(test,()->owner.snapshot().helperConnected)){
                require(SystemClock.elapsedRealtime()<deadline,"Connect real shell helper for created-screen test");Thread.sleep(100);
            }
            main(test,()->{
                ActivityFixtureTests.select(activity,ScreenSource.CREATE_NEW);
                ActivityFixtureTests.screencast(activity).startSession(false);return null;
            });
            CaptureFixtureTests.waitFor(()->owner.snapshot().phase==SessionCoordinator.Phase.STREAMING||!owner.active(),"Created screen first native image");
            require(owner.snapshot().phase==SessionCoordinator.Phase.STREAMING,"Create failed: "+owner.snapshot().status);
            id=owner.snapshot().createdDisplayId;require(id>0,"New screen has an independent display ID");
            CaptureController capture=main(test,()->owner.source().capture());long before=capture.frameVersion();
            main(test,()->{owner.launchApp(test.getTargetContext().getPackageName()+".test/io.github.ms912x.desktop.DesktopGeometryFixtureActivity");return null;});
            CaptureFixtureTests.waitFor(()->owner.snapshot().launchStatus.contains("App launched"),"Typed app launch on the owned screen");
            CaptureFixtureTests.waitFor(()->capture.frameVersion()>before+1,"Created screen app continues producing native images");
            main(test,()->{owner.stop("Created-screen fixture stopped.");return null;});
            CaptureFixtureTests.waitFor(()->!owner.active(),"Created-screen reader and display cleanup");
            require(!capture.isOpen()&&test.getTargetContext().getSystemService(DisplayManager.class).getDisplay(id)==null,"Stop closes the reader and removes the created desktop");
            try(java.io.FileOutputStream file=test.getTargetContext().openFileOutput("created-screen-result.json",0)){
                file.write(new JSONObject().put("passed",true).put("created_display_id",id).put("frames",capture.frameVersion())
                        .toString(2).getBytes(StandardCharsets.UTF_8));
            }
        }finally{main(test,()->{if(owner.active())owner.stop("Created-screen fixture finished.");activity.finish();return null;});}
    }
}
