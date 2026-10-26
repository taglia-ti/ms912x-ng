package io.github.taglia_ti.ms912x_ng.desktop;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Initial content when Android has no compatible secondary launcher. */
public final class DesktopStartActivity extends Activity {
    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        LinearLayout page=new LinearLayout(this);page.setOrientation(LinearLayout.VERTICAL);
        page.setGravity(Gravity.CENTER);page.setBackgroundColor(Color.rgb(16,28,40));
        TextView title=new TextView(this);title.setText("Independent screen ready");title.setTextColor(Color.WHITE);title.setTextSize(28);
        page.addView(title);
        TextView hint=new TextView(this);hint.setText("Use Open app on new screen in MS912x on your phone.");hint.setTextColor(Color.LTGRAY);hint.setTextSize(18);
        hint.setPadding(24,24,24,24);page.addView(hint);setContentView(page);
    }
}
