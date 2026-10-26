package io.github.ms912x.desktop;

import android.content.Context;
import android.content.SharedPreferences;
import android.view.View;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** View binding and saved choices only; the service/helper own the running router. */
final class MouseSharingController {
    interface Commands {void refresh();void configure(int side,String identity);}
    private final MainScreenView screen;
    private final SharedPreferences saved;
    private final Commands commands;
    private final SpinnerSelectionListener position,device;
    private MouseSharingInfo state=MouseSharingInfo.empty();
    private String[] identities=new String[0];
    private String selected;
    private int side,displayId=-1;

    MouseSharingController(Context context,MainScreenView screen,Commands commands){
        this(screen,context.getSharedPreferences("mouse_sharing",Context.MODE_PRIVATE),commands);
    }
    MouseSharingController(MainScreenView screen,SharedPreferences saved,Commands commands){
        this.screen=screen;this.commands=commands;this.saved=saved;
        try{side=Math.max(1,Math.min(4,saved.getInt("side",2)));selected=saved.getString("device","");}
        catch(ClassCastException corrupt){side=2;selected="";}
        position=new SpinnerSelectionListener(screen.mousePositionSelector,()->{
            side=screen.mousePositionSelector.getSelectedItemPosition()+1;save();
            if(state.active)configure();
        });
        device=new SpinnerSelectionListener(screen.mouseDeviceSelector,()->{
            int index=screen.mouseDeviceSelector.getSelectedItemPosition();
            if(index>=0&&index<identities.length){selected=identities[index];save();if(state.active)configure();}
        });
        screen.mousePositionSelector.setSelection(side-1);position.synchronize();
        screen.mouseRefreshButton.setOnClickListener(view->commands.refresh());
        screen.mouseSharingButton.setOnClickListener(view->{if(state.active)commands.configure(0,"");else configure();});
    }
    private void save(){saved.edit().putInt("side",side).putString("device",selected).apply();}
    private void configure(){
        int index=screen.mouseDeviceSelector.getSelectedItemPosition();
        if(index<0||index>=identities.length)return;
        selected=identities[index];save();
        screen.mouseSharingStatus.setText("Updating mouse sharing…");commands.configure(side,selected);
    }
    void render(int createdDisplay,MouseSharingInfo next){
        boolean entered=createdDisplay>0&&createdDisplay!=displayId;
        displayId=createdDisplay;state=next;
        screen.mouseSharingCard.setVisibility(!BuildConfig.MIRROR_ONLY&&displayId>0?View.VISIBLE:View.GONE);
        if(!Arrays.equals(identities,next.devices)){
            identities=next.devices.clone();List<String> labels=new ArrayList<>();
            int index=0;
            for(int i=0;i<identities.length;i++){labels.add(MouseSharingInfo.label(identities[i]));if(identities[i].equals(selected))index=i;}
            if(labels.isEmpty())labels.add("No mouse detected");
            screen.setItems(screen.mouseDeviceSelector,labels);screen.mouseDeviceSelector.setSelection(index);device.synchronize();
        }
        screen.mouseSharingButton.setText(next.active?"Stop mouse sharing":"Start mouse sharing");
        screen.mouseSharingButton.setEnabled(next.active||next.available&&identities.length>0);
        screen.mouseDeviceSelector.setEnabled(identities.length>0);
        screen.mouseSharingStatus.setText(next.message);
        if(entered)commands.refresh();
    }
    void clearSavedOptions(){
        saved.edit().clear().apply();
        if(!state.active){side=2;selected="";screen.mousePositionSelector.setSelection(1);position.synchronize();}
    }
}
