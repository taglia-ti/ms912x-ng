package io.github.taglia_ti.ms912x_ng.desktop;

import android.os.Bundle;

/** A separate APK activity launched by the production helper, with no host display placement. */
public final class DesktopGeometryFixtureActivity extends ExternalDisplayFixtureActivity {
    @Override public void onCreate(Bundle state){getIntent().putExtra("geometry",true);super.onCreate(state);}
}
