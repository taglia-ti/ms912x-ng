package io.github.ms912x.desktop;

import android.app.Instrumentation;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicInteger;
import static io.github.ms912x.desktop.AndroidFixtures.*;
import static io.github.ms912x.desktop.OutputFixtureTests.main;

final class ImageSettingsFixtureTests {
    static void run(Instrumentation test) throws Exception {
        SharedPreferences storage=test.getTargetContext().getSharedPreferences("fixture.image-settings",Context.MODE_PRIVATE);
        storage.edit().clear().commit();ImageSettingsStore store=new ImageSettingsStore(storage);
        store.save(new ImageOptions(2,7,15));
        MainActivity activity=(MainActivity)test.startActivitySync(new Intent(test.getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        MainScreenView[] screen=new MainScreenView[1];ImageSettingsController[] controller=new ImageSettingsController[1];
        AtomicInteger edits=new AtomicInteger();ImageOptions[] last=new ImageOptions[1];
        Runnable rebuild=()->{
            screen[0]=new MainScreenView(activity,new AppAppearance(activity),activity);
            controller[0]=new ImageSettingsController(screen[0],store,options->{last[0]=options;edits.incrementAndGet();});
            activity.setContentView(screen[0].root);
            screen[0].drawerSettingsButton.performClick();
        };
        try{
            main(test,()->{ActivityFixtureTests.selection(activity).stop();ActivityFixtureTests.screencast(activity).close();rebuild.run();return null;});
            test.waitForIdleSync();
            main(test,()->{
                require(edits.get()==0&&store.read().equals(new ImageOptions(2,7,15)),"Restoration cannot overwrite saved image settings");
                require(screen[0].profileSelector.getSelectedItemPosition()==2&&screen[0].strideSelector.getSelectedItemPosition()==7
                        &&screen[0].toleranceSelector.getSelectedItemPosition()==5,"Saved profile/stride/tolerance are displayed");
                screen[0].profileSelector.setSelection(3);return null;
            });
            test.waitForIdleSync();
            main(test,()->{
                require(store.read().equals(new ImageOptions(3,0,-1))&&last[0].equals(store.read()),"Changing profile applies its defaults live");
                screen[0].strideSelector.setSelection(4);return null;
            });test.waitForIdleSync();
            main(test,()->{screen[0].toleranceSelector.setSelection(4);return null;});test.waitForIdleSync();
            main(test,()->{
                require(last[0].equals(new ImageOptions(3,4,7))&&store.read().equals(last[0]),"Manual overrides persist and are forwarded immediately");
                require(screen[0].imageSettingsSummary.getText().toString().contains("stride 4"),"Effective setting summary updates");
                rebuild.run();return null;
            });test.waitForIdleSync();
            main(test,()->{
                require(edits.get()==3&&screen[0].strideSelector.getSelectedItemPosition()==4&&screen[0].toleranceSelector.getSelectedItemPosition()==4,
                        "New controller restores overrides without replaying edits");
                int[][] expected={{3,1,0,0},{1,5,0,0},{2,2,3,15},{3,1,0,0}};
                for(int i=0;i<4;i++)require(Arrays.equals(NativeCapture.profileDefaults(i),expected[i]),"JNI profile defaults reuse Linux policy "+i);
                try(CaptureController capture=new CaptureController(64,4,1,null,store.read())){
                    require(capture.imageOptions().equals(store.read()),"Capture is created with the saved native options");
                    capture.setImageOptions(new ImageOptions(2,1,0));
                    require(capture.imageOptions().equals(new ImageOptions(2,1,0)),"Native options can change on the same reader");
                }
                controller[0].clearSavedOptions();return null;
            });test.waitForIdleSync();
            main(test,()->{
                require(storage.getAll().isEmpty()&&last[0].equals(ImageOptions.DEFAULT),"Clear removes image choices and applies defaults live");
                storage.edit().putString("profile","corrupt").commit();
                require(store.read().equals(ImageOptions.DEFAULT),"Invalid stored setting types fall back safely");return null;
            });
        }finally{main(test,()->{activity.finish();return null;});storage.edit().clear().commit();}
    }
}
