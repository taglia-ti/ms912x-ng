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
        store.save(new ImageOptions(2,7,15,false));
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
                require(edits.get()==0&&store.read().equals(new ImageOptions(2,7,15,false)),"Restoration cannot overwrite saved image settings");
                require(screen[0].profileSelector.getSelectedItemPosition()==1&&screen[0].strideSelector.getSelectedItemPosition()==7
                        &&screen[0].toleranceSelector.getSelectedItemPosition()==5&&screen[0].toleranceMaxSelector.getSelectedItemPosition()==5
                        &&screen[0].scanSelector.getSelectedItemPosition()==1,"Saved profile/stride/tolerance/scan are displayed without Auto");
                screen[0].profileSelector.setSelection(2);return null;
            });
            test.waitForIdleSync();
            main(test,()->{
                require(store.read().equals(new ImageOptions(3,0,-1,false))&&last[0].equals(store.read()),"Changing profile applies its defaults live");
                screen[0].strideSelector.setSelection(4);return null;
            });test.waitForIdleSync();
            main(test,()->{screen[0].toleranceSelector.setSelection(4);return null;});test.waitForIdleSync();
            main(test,()->{
                require(last[0].equals(new ImageOptions(3,4,7,false))&&store.read().equals(last[0]),"Manual overrides persist and are forwarded immediately");
                require(screen[0].imageSettingsSummary.getText().toString().contains("scan Off"),"Disabled-scan summary explains full frames");
                rebuild.run();return null;
            });test.waitForIdleSync();
            main(test,()->{
                require(edits.get()==3&&screen[0].strideSelector.getSelectedItemPosition()==4&&screen[0].toleranceSelector.getSelectedItemPosition()==4&&screen[0].scanSelector.getSelectedItemPosition()==1,
                        "New controller restores overrides without replaying edits");
                int[][] expected={{0},{1,5,0,0},{2,2,3,15},{3,1,0,0}};
                require(screen[0].profileSelector.getCount()==3,"Only the three explicit profiles are offered");
                require(screen[0].toleranceSelector.getCount()==9&&screen[0].toleranceMaxSelector.getCount()==9,
                        "Minimum and maximum tolerance include the 63 and 127 buckets");
                for(int i=1;i<4;i++)require(Arrays.equals(NativeCapture.profileDefaults(i),expected[i]),"JNI profile defaults reuse Linux policy "+i);
                try(CaptureController capture=new CaptureController(64,4,1,null,store.read())){
                    require(capture.imageOptions().equals(store.read()),"Capture is created with the saved native options");
                    capture.setImageOptions(new ImageOptions(2,1,0));
                    require(capture.imageOptions().equals(new ImageOptions(2,1,0)),"Native options can change on the same reader");
                }
                ImageOptions adaptive = new ImageOptions(2, 2, 63, 127);
                store.save(adaptive);
                require(store.read().equals(adaptive) && adaptive.summary().contains("63–127 (adaptive)"),
                        "Adaptive tolerance bounds persist and are identified in the summary");
                try(CaptureController capture=new CaptureController(64,4,2,null,adaptive)){
                    require(capture.imageOptions().equals(adaptive),"Native capture accepts a live tolerance range");
                    capture.setImageOptions(new ImageOptions(2,2,127,127));
                    require(capture.imageOptions().equals(new ImageOptions(2,2,127,127)),"High tolerance bounds update on the same native capture");
                }
                store.save(last[0]);
                screen[0].scanSelector.setSelection(0);return null;
            });test.waitForIdleSync();
            main(test,()->{
                require(last[0].equals(new ImageOptions(3,4,7))&&store.read().scanEnabled,"Scan On persists and applies immediately");
                require(screen[0].imageSettingsSummary.getText().toString().contains("stride 4"),"Reenabled scan restores its saved overrides");
                return null;
            });
            int[] rangeEdits={edits.get()};
            // Each user choice must publish once, including automatic adjustment of the other bound.
            int[][] choices={
                    {0,1,2,0,-1,-1}, // Latency restores its adaptive defaults.
                    {2,2,2,0,1,1},   // Maximum below minimum lowers the minimum.
                    {1,5,2,0,15,15}, // Minimum above maximum raises the maximum.
                    {2,6,2,0,15,31},
                    {1,0,2,0,3,31},  // A single default resolves against the explicit other bound.
                    {2,0,2,0,3,15},
                    {0,2,3,0,-1,-1},
                    {2,4,3,0,0,7},
                    {2,7,3,0,0,63},
                    {1,8,3,0,127,127},
                    {1,7,3,0,63,127}
            };
            for(int[] choice:choices){
                main(test,()->{
                    (choice[0]==0?screen[0].profileSelector:choice[0]==1?screen[0].toleranceSelector:screen[0].toleranceMaxSelector)
                            .setSelection(choice[1]);return null;
                });
                test.waitForIdleSync();
                main(test,()->{
                    ImageOptions expectedRange=new ImageOptions(choice[2],choice[3],choice[4],choice[5]);
                    require(last[0].equals(expectedRange)&&store.read().equals(expectedRange),"Tolerance dropdowns persist and forward "+expectedRange.summary());
                    require(edits.get()==++rangeEdits[0],"Adjusting both bounds publishes exactly one live update");
                    require(ImageOptions.TOLERANCES[screen[0].toleranceSelector.getSelectedItemPosition()]==choice[4]
                            &&ImageOptions.TOLERANCES[screen[0].toleranceMaxSelector.getSelectedItemPosition()]==choice[5],
                            "Both dropdowns show the normalized range");
                    return null;
                });
            }
            main(test,()->{rebuild.run();return null;});test.waitForIdleSync();
            main(test,()->{
                require(edits.get()==rangeEdits[0]&&store.read().equals(new ImageOptions(3,0,63,127))
                        &&screen[0].toleranceSelector.getSelectedItemPosition()==7&&screen[0].toleranceMaxSelector.getSelectedItemPosition()==8,
                        "Recreation restores both bounds without publishing another update");
                controller[0].clearSavedOptions();return null;
            });test.waitForIdleSync();
            main(test,()->{
                require(storage.getAll().isEmpty()&&last[0].equals(ImageOptions.DEFAULT),"Clear removes image choices and applies defaults live");
                storage.edit().putInt("profile",0).putInt("stride",7).putInt("tolerance",15).putBoolean("scan_enabled",false).commit();
                require(store.read().equals(new ImageOptions(3,7,15,false)),"Retired Auto migrates to Quality without losing overrides");
                storage.edit().clear().putInt("profile",1).commit();
                require(store.read().equals(new ImageOptions(1,0,-1)),"Existing explicit profile IDs remain stable and legacy scan defaults On");
                storage.edit().putString("profile","corrupt").commit();
                require(store.read().equals(ImageOptions.DEFAULT),"Invalid stored setting types fall back safely");
                storage.edit().clear().putInt("profile",2).putInt("tolerance",7).commit();
                require(store.read().equals(new ImageOptions(2,0,7,7)),"Legacy single tolerance migrates to a fixed range");
                storage.edit().putInt("tolerance_max",3).commit();
                require(store.read().equals(ImageOptions.DEFAULT),"An inverted cached range falls back safely");
                storage.edit().putString("tolerance_max","corrupt").commit();
                require(store.read().equals(ImageOptions.DEFAULT),"Invalid cached maximum type falls back safely");return null;
            });
        }finally{main(test,()->{activity.finish();return null;});storage.edit().clear().commit();}
        MouseSharingFixtureTests.run(test);
    }
}
