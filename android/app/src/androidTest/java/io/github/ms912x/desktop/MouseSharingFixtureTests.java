package io.github.ms912x.desktop;

import android.app.Instrumentation;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.view.View;
import java.util.concurrent.atomic.AtomicInteger;
import static io.github.ms912x.desktop.AndroidFixtures.require;
import static io.github.ms912x.desktop.OutputFixtureTests.main;

final class MouseSharingFixtureTests {
    static void run(Instrumentation test) throws Exception {
        if(BuildConfig.MIRROR_ONLY)return;
        SharedPreferences storage=test.getTargetContext().getSharedPreferences("fixture.mouse-sharing",Context.MODE_PRIVATE);
        storage.edit().clear().putInt("side",3).putString("device","/dev/input/event9\t3:10:20:1:9:Preferred mouse").commit();
        MainActivity activity=(MainActivity)test.startActivitySync(new Intent(test.getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        MainScreenView[] screen=new MainScreenView[1];MouseSharingController[] controller=new MouseSharingController[1];
        String[] devices={"/dev/input/event1\t3:10:21:1:1:Other mouse","/dev/input/event9\t3:10:20:1:9:Preferred mouse"};
        AtomicInteger edits=new AtomicInteger(),refreshes=new AtomicInteger();int[] requested={0};
        try{
            main(test,()->{
                ActivityFixtureTests.selection(activity).stop();ActivityFixtureTests.screencast(activity).close();
                screen[0]=new MainScreenView(activity,new AppAppearance(activity),activity);activity.setContentView(screen[0].root);
                screen[0].drawerSettingsButton.performClick();
                controller[0]=new MouseSharingController(screen[0],storage,new MouseSharingController.Commands(){
                    public void refresh(){refreshes.incrementAndGet();}
                    public void configure(int side,String device){edits.incrementAndGet();requested[0]=side;}
                });
                controller[0].render(-1,MouseSharingInfo.empty());
                require(screen[0].mouseSharingCard.getVisibility()==View.GONE,"Sharing controls require an ADB-created screen");
                controller[0].render(27,new MouseSharingInfo(false,true,0,"Ready",devices));return null;
            });test.waitForIdleSync();
            main(test,()->{
                require(edits.get()==0&&refreshes.get()==1,"Restoration and inventory refresh cannot start capture");
                require(screen[0].mousePositionSelector.getSelectedItemPosition()==2&&screen[0].mouseDeviceSelector.getSelectedItemPosition()==1,"Saved side and mouse restored");
                screen[0].mouseSharingButton.performClick();
                require(requested[0]==3&&edits.get()==1,"Start uses saved position");
                controller[0].render(27,new MouseSharingInfo(true,true,3,"Active",devices));
                screen[0].mousePositionSelector.setSelection(0);return null;
            });test.waitForIdleSync();
            main(test,()->{
                require(requested[0]==1&&storage.getInt("side",0)==1&&edits.get()==2,"Changing position applies live and persists: side="+requested[0]+" saved="+storage.getInt("side",0)+" edits="+edits.get());
                screen[0].mouseSharingButton.performClick();require(requested[0]==0,"Stop sends an explicit release");
                controller[0].render(27,new MouseSharingInfo(false,false,0,"Unsupported",new String[0]));
                require(!screen[0].mouseSharingButton.isEnabled(),"Unsupported devices cannot start mouse capture");
                controller[0].clearSavedOptions();require(storage.getAll().isEmpty(),"Clear removes saved mouse choices");return null;
            });test.waitForIdleSync();
            require(storage.getAll().isEmpty(),"Deferred spinner selection cannot recreate cleared mouse choices");
        }finally{main(test,()->{activity.finish();return null;});storage.edit().clear().commit();}
    }
}
