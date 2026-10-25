package io.github.ms912x.desktop;

import android.app.Instrumentation;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;
import java.util.Collections;
import static io.github.ms912x.desktop.AndroidFixtures.*;
import static io.github.ms912x.desktop.OutputFixtureTests.main;

/** Exercises delayed framework spinner callbacks against isolated preferences. */
final class SelectionFixtureTests {
    static void run(Instrumentation test) throws Exception {
        SharedPreferences storage=test.getTargetContext().getSharedPreferences("fixture.selection",Context.MODE_PRIVATE);
        storage.edit().clear().commit();
        DisplaySelectionPreferences preferences=new DisplaySelectionPreferences(storage);
        MainActivity activity=(MainActivity)test.startActivitySync(new Intent(test.getTargetContext(),MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        MainScreenView[] view=new MainScreenView[1];
        DisplaySelectionController[] controller=new DisplaySelectionController[1];
        DisplayModeOption fifty=NativeAdapter.modeCatalog().stream()
                .filter(mode->mode.width==1920&&mode.height==1080&&mode.refreshHz==50).findFirst().get();
        int source=BuildConfig.MIRROR_ONLY?0:ScreenSource.CREATE_NEW;
        int rotation=Build.VERSION.SDK_INT>=36||!BuildConfig.MIRROR_ONLY?3:0;
        try {
            main(test,()->{
                ActivityFixtureTests.selection(activity).stop();ActivityFixtureTests.screencast(activity).close();
                preferences.save(source==0?new ScreenSource(0,"Phone",0,0,"local:default",false,true,"")
                        :new ScreenSource(source,"New",0,0),rotation,fifty);
                view[0]=new MainScreenView(activity,new AppAppearance(activity),activity);
                controller[0]=new DisplaySelectionController(activity,view[0],()->{},id->{},preferences);
                controller[0].applyCapabilities(null);controller[0].refreshDisplayInventory();
                controller[0].applyCapabilities(new AdapterCapabilities(null,"Fixture",NativeAdapter.modeCatalog()));
                activity.setContentView(view[0].root);return null;
            });
            test.waitForIdleSync();
            main(test,()->{
                require(controller[0].selectedSource().id==source,"Cached source survives initial layout");
                require(controller[0].selectedMode().id==fifty.id,"Cached 50 Hz survives deferred spinner callbacks");
                require(controller[0].selectedRotation()==rotation,"Cached rotation follows the restored source controls");
                view[0].rateSelector.setSelection(0);return null;
            });
            test.waitForIdleSync();
            main(test,()->{
                require("60 Hz".equals(preferences.refresh()),"User refresh change is persisted");
                controller[0].prepareProbe();controller[0].applyProbedCapabilities(new AdapterCapabilities(null,"Fixture",NativeAdapter.modeCatalog()));
                return null;
            });
            test.waitForIdleSync();
            main(test,()->{
                require(controller[0].selectedMode().refreshHz==60,"Reprobe preserves the new refresh choice");
                controller[0].clearSavedOptions(false);
                require(controller[0].selectedMode().refreshHz==60&&controller[0].selectedSource().id==source,
                        "Clearing preferences during output preserves displayed session choices");
                require(storage.getAll().isEmpty(),"Active-session cache clear removes all choices");
                controller[0].clearSavedOptions();return null;
            });
            test.waitForIdleSync();
            main(test,()->{
                require(storage.getAll().isEmpty(),"Clear stays empty after delayed layout callbacks");
                require(controller[0].selectedSource().id==0&&controller[0].selectedRotation()==0,"Idle clear resets source and rotation");
                preferences.save(controller[0].selectedSource(),0,fifty);
                controller[0].applyCapabilities(new AdapterCapabilities(null,"Small monitor",Collections.singletonList(mode640())));
                return null;
            });
            test.waitForIdleSync();
            main(test,()->{
                require(controller[0].selectedMode().id==mode640().id,"Unavailable cached output falls back to advertised modes");
                require(preferences.resolution().equals(fifty.resolutionLabel()),"Automatic fallback does not overwrite user preferences");
                return null;
            });
        } finally {
            main(test,()->{if(controller[0]!=null)controller[0].stop();activity.finish();return null;});
            storage.edit().clear().commit();
        }
    }
}
