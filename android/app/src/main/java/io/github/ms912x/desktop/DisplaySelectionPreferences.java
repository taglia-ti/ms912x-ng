package io.github.ms912x.desktop;

import android.content.Context;
import android.content.SharedPreferences;

/** Stores user choices independently of display discovery and spinner callbacks. */
final class DisplaySelectionPreferences {
    private final SharedPreferences preferences;

    DisplaySelectionPreferences(Context context) {
        this(context.getSharedPreferences("main_screen_options", Context.MODE_PRIVATE));
    }

    DisplaySelectionPreferences(SharedPreferences preferences) { this.preferences = preferences; }

    String source() { return preferences.getString("source_key", null); }
    String resolution() { return preferences.getString("resolution", null); }
    String refresh() { return preferences.getString("refresh", null); }
    int rotation() { return preferences.getInt("rotation", 0); }

    void save(ScreenSource source, int rotation, DisplayModeOption mode) {
        String key = sourceKey(source);
        if (java.util.Objects.equals(key, source()) && rotation == rotation()
                && (mode == null || (mode.resolutionLabel().equals(resolution())
                && (mode.refreshHz + " Hz").equals(refresh())))) return;
        SharedPreferences.Editor edit = preferences.edit().putString("source_key", key).putInt("rotation", rotation);
        if (mode != null) edit.putString("resolution", mode.resolutionLabel()).putString("refresh", mode.refreshHz + " Hz");
        edit.apply();
    }

    void clear() { preferences.edit().clear().apply(); }

    static String sourceKey(ScreenSource source) {
        if (source == null) return null;
        if (source.id == ScreenSource.CREATE_NEW) return "create-new";
        if (source.id == ScreenSource.APP_PICKER) return "app-picker";
        if (source.id == ScreenSource.TEST_SCREEN) return "test-screen";
        if (!source.identity.isEmpty()) return "display:" + source.identity;
        return "display-id:" + source.id;
    }
}
