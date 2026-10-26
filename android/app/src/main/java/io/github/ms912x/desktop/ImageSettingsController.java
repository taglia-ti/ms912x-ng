package io.github.ms912x.desktop;

import android.content.Context;
import java.util.function.Consumer;

/** Persists user choices and forwards live changes to the active capture owner. */
final class ImageSettingsController {
    private final MainScreenView screen;
    private final ImageSettingsStore store;
    private final SpinnerSelectionListener profile, scan, stride, tolerance, toleranceMax;
    private final Consumer<ImageOptions> changed;

    ImageSettingsController(Context context, MainScreenView screen, Consumer<ImageOptions> changed) {
        this(screen, new ImageSettingsStore(context), changed);
    }

    ImageSettingsController(MainScreenView screen, ImageSettingsStore store, Consumer<ImageOptions> changed) {
        this.screen = screen;
        this.store = store;
        this.changed = changed;
        profile = new SpinnerSelectionListener(screen.profileSelector, () -> {
            ImageOptions options = new ImageOptions(screen.profileSelector.getSelectedItemPosition() + 1,
                    0, -1, screen.scanSelector.getSelectedItemPosition() == 0);
            store.save(options);
            render(options);
            changed.accept(options);
        });
        scan = new SpinnerSelectionListener(screen.scanSelector, this::save);
        stride = new SpinnerSelectionListener(screen.strideSelector, this::save);
        tolerance = new SpinnerSelectionListener(screen.toleranceSelector, () -> saveRange(true));
        toleranceMax = new SpinnerSelectionListener(screen.toleranceMaxSelector, () -> saveRange(false));
        render(store.read());
    }

    private void save() {
        saveRange(true);
    }

    private void saveRange(boolean minimumChanged) {
        int profileId = screen.profileSelector.getSelectedItemPosition() + 1;
        int minimum = ImageOptions.TOLERANCES[screen.toleranceSelector.getSelectedItemPosition()];
        int maximum = ImageOptions.TOLERANCES[screen.toleranceMaxSelector.getSelectedItemPosition()];
        int[] defaults = NativeCapture.profileDefaults(profileId);
        int resolvedMinimum = minimum < 0 ? defaults[2] : minimum;
        int resolvedMaximum = maximum < 0 ? defaults[3] : maximum;
        if (minimum < 0 && maximum >= 0) minimum = resolvedMinimum;
        if (maximum < 0 && minimum >= 0) maximum = resolvedMaximum;
        resolvedMinimum = minimum < 0 ? defaults[2] : minimum;
        resolvedMaximum = maximum < 0 ? defaults[3] : maximum;
        if (resolvedMinimum > resolvedMaximum) {
            if (minimumChanged) {
                maximum = minimum;
                screen.toleranceMaxSelector.setSelection(indexOf(maximum));
            } else {
                minimum = maximum;
                screen.toleranceSelector.setSelection(indexOf(minimum));
            }
        }
        ImageOptions options = new ImageOptions(profileId, screen.strideSelector.getSelectedItemPosition(),
                minimum, maximum, screen.scanSelector.getSelectedItemPosition() == 0);
        screen.toleranceSelector.setSelection(indexOf(minimum));
        screen.toleranceMaxSelector.setSelection(indexOf(maximum));
        tolerance.synchronize();
        toleranceMax.synchronize();
        store.save(options);
        screen.imageSettingsSummary.setText(options.summary());
        changed.accept(options);
    }

    private static int indexOf(int tolerance) {
        for (int i = 0; i < ImageOptions.TOLERANCES.length; i++)
            if (ImageOptions.TOLERANCES[i] == tolerance) return i;
        throw new IllegalArgumentException("Invalid tolerance bucket");
    }

    private void render(ImageOptions options) {
        screen.profileSelector.setSelection(options.profile - 1);
        screen.scanSelector.setSelection(options.scanEnabled ? 0 : 1);
        screen.strideSelector.setSelection(options.stride);
        screen.toleranceSelector.setSelection(indexOf(options.tolerance));
        screen.toleranceMaxSelector.setSelection(indexOf(options.toleranceMax));
        profile.synchronize();
        scan.synchronize();
        stride.synchronize();
        tolerance.synchronize();
        toleranceMax.synchronize();
        screen.imageSettingsSummary.setText(options.summary());
    }

    void clearSavedOptions() {
        store.clear();
        render(ImageOptions.DEFAULT);
        changed.accept(ImageOptions.DEFAULT);
    }
}
