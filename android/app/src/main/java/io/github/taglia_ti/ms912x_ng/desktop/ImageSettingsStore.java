package io.github.taglia_ti.ms912x_ng.desktop;

import android.content.Context;
import android.content.SharedPreferences;

/** Persists image matching independently from display inventory and ADB credentials. */
final class ImageSettingsStore {
    private final SharedPreferences preferences;
    ImageSettingsStore(Context context) {
        this(context.getSharedPreferences("image_settings", Context.MODE_PRIVATE));
    }

    ImageSettingsStore(SharedPreferences preferences) { this.preferences = preferences; }

    ImageOptions read() {
        try {
            int profile = preferences.getInt("profile", 3);
            if (profile == 0) profile = 3; // Retire the old Auto setting without changing its effective policy.
            int minimum = preferences.getInt("tolerance", -1);
            int maximum = preferences.getInt("tolerance_max", minimum);
            return new ImageOptions(profile, preferences.getInt("stride", 0), minimum, maximum,
                    preferences.getBoolean("scan_enabled", true));
        } catch (IllegalArgumentException | ClassCastException invalid) {
            return ImageOptions.DEFAULT;
        }
    }

    void save(ImageOptions options) {
        if (options.equals(read())) return;
        preferences.edit().putInt("profile", options.profile).putInt("stride", options.stride)
                .putInt("tolerance", options.tolerance).putInt("tolerance_max", options.toleranceMax)
                .putBoolean("scan_enabled", options.scanEnabled).apply();
    }

    void clear() { preferences.edit().clear().apply(); }
}
