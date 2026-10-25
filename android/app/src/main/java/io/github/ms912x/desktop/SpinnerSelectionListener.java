package io.github.ms912x.desktop;

import android.view.View;
import android.widget.AdapterView;
import android.widget.Spinner;

/** Acknowledge programmatic choices explicitly so deferred layout cannot save them as edits. */
final class SpinnerSelectionListener implements AdapterView.OnItemSelectedListener {
    private final Spinner spinner;
    private final Runnable action;
    private Object adapter;
    private int position;
    SpinnerSelectionListener(Spinner spinner,Runnable action){
        this.spinner=spinner;this.action=action;synchronize();spinner.setOnItemSelectedListener(this);
    }
    void synchronize(){adapter=spinner.getAdapter();position=spinner.getSelectedItemPosition();}
    @Override public void onItemSelected(AdapterView<?> parent,View view,int ignored,long id){
        if(adapter==spinner.getAdapter()&&position==spinner.getSelectedItemPosition())return;
        synchronize();action.run();
    }
    @Override public void onNothingSelected(AdapterView<?> parent){synchronize();}
}
