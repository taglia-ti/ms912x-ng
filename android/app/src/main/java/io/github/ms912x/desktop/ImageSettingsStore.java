package io.github.ms912x.desktop;

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
            return new ImageOptions(preferences.getInt("profile", 0), preferences.getInt("stride", 0),
                    preferences.getInt("tolerance", -1));
        } catch (IllegalArgumentException | ClassCastException invalid) {
            return ImageOptions.DEFAULT;
        }
    }

    void save(ImageOptions options) {
        if (options.equals(read())) return;
        preferences.edit().putInt("profile", options.profile).putInt("stride", options.stride)
                .putInt("tolerance", options.tolerance).apply();
    }

    void clear() { preferences.edit().clear().apply(); }
}
