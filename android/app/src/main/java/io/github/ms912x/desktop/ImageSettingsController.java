package io.github.ms912x.desktop;

import android.content.Context;
import java.util.function.Consumer;

/** Persists user choices and forwards live changes to the active capture owner. */
final class ImageSettingsController {
    private final MainScreenView screen;
    private final ImageSettingsStore store;
    private final SpinnerSelectionListener profile, scan, stride, tolerance;
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
        tolerance = new SpinnerSelectionListener(screen.toleranceSelector, this::save);
        render(store.read());
    }

    private void save() {
        ImageOptions options = new ImageOptions(screen.profileSelector.getSelectedItemPosition() + 1,
                screen.strideSelector.getSelectedItemPosition(),
                ImageOptions.TOLERANCES[screen.toleranceSelector.getSelectedItemPosition()],
                screen.scanSelector.getSelectedItemPosition() == 0);
        store.save(options);
        screen.imageSettingsSummary.setText(options.summary());
        changed.accept(options);
    }

    private void render(ImageOptions options) {
        screen.profileSelector.setSelection(options.profile - 1);
        screen.scanSelector.setSelection(options.scanEnabled ? 0 : 1);
        screen.strideSelector.setSelection(options.stride);
        for (int i = 0; i < ImageOptions.TOLERANCES.length; i++)
            if (ImageOptions.TOLERANCES[i] == options.tolerance) screen.toleranceSelector.setSelection(i);
        profile.synchronize();
        scan.synchronize();
        stride.synchronize();
        tolerance.synchronize();
        screen.imageSettingsSummary.setText(options.summary());
    }

    void clearSavedOptions() {
        store.clear();
        render(ImageOptions.DEFAULT);
        changed.accept(ImageOptions.DEFAULT);
    }
}
