package io.github.taglia_ti.ms912x_ng.desktop;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.Configuration;

/** Persists appearance choice and resolves the shared UI palette without restarting capture. */
final class AppAppearance {
    enum Mode { AUTO, LIGHT, DARK }

    private final SharedPreferences preferences;
    private Mode mode;
    private boolean dark;

    AppAppearance(Context context) {
        preferences = context.getSharedPreferences("appearance", Context.MODE_PRIVATE);
        try { mode = Mode.valueOf(preferences.getString("mode", Mode.AUTO.name())); }
        catch (IllegalArgumentException ignored) { mode = Mode.AUTO; }
        update(context.getResources().getConfiguration());
    }

    Mode mode() { return mode; }
    boolean isDark() { return dark; }
    int theme() { return dark ? R.style.AppTheme_Dark : R.style.AppTheme_Light; }

    void select(Mode next, Configuration configuration) {
        mode = next;
        preferences.edit().putString("mode", next.name()).apply();
        update(configuration);
    }

    void update(Configuration configuration) {
        dark = mode == Mode.DARK || (mode == Mode.AUTO
                && (configuration.uiMode & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES);
    }

    int color(int light) {
        if (!dark) return light;
        switch (light) {
            case 0xfff0f4f8: return 0xff101821; // Page
            case 0xffffffff: return 0xff192531; // Surface
            case 0xff14243b: return 0xffe5edf7; // Primary text
            case 0xff526477: return 0xffa8b7c8; // Secondary text
            case 0xffe6edf5: return 0xff192531; // Touchpad surface
            case 0xff91a4b8: return 0xff344454; // Touchpad grid
            case 0xff1264a3: return 0xff8cc8ff; // Accent
            case 0xffdde5ee: return 0xff344454; // Border / disabled primary button
            case 0xffe4edf7: return 0xff253d53; // Button
            case 0xffedf1f5: return 0xff1c2b38; // Disabled button
            default: return light;
        }
    }
}
