package io.github.ms912x.desktop;

import android.app.Activity;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.view.Display;
import android.view.View;
import android.widget.AdapterView;
import android.widget.Spinner;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.function.IntConsumer;

/** Owns display inventory, EDID mode selection and preservation of valid manual choices. */
final class DisplaySelectionController {
    private final Activity activity;
    private final MainScreenView screen;
    private final Runnable onChanged;
    private final IntConsumer onDisplayRemoved;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final List<ScreenSource> screens = new ArrayList<>();
    private List<DisplayModeOption> displayModes = new ArrayList<>();
    private AdapterCapabilities adapterCapabilities;
    private AdapterCapabilities capabilitiesBeforeProbe;
    private DisplayModeOption modeBeforeProbe;
    private String rateResolution;
    private boolean displayListenerRegistered;

    private final DisplayManager.DisplayListener displayListener =
            new DisplayManager.DisplayListener() {
                @Override public void onDisplayAdded(int id) { refreshDisplayInventory(); }
                @Override public void onDisplayChanged(int id) { refreshDisplayInventory(); }
                @Override public void onDisplayRemoved(int id) {
                    onDisplayRemoved.accept(id);
                    refreshDisplayInventory();
                }
            };

    DisplaySelectionController(Activity activity, MainScreenView screen, Runnable onChanged,
                               IntConsumer onDisplayRemoved) {
        this.activity = activity;
        this.screen = screen;
        this.onChanged = onChanged;
        this.onDisplayRemoved = onDisplayRemoved;
        onSelection(screen.sourceSelector, () -> {
            updateSourceDescription();
            onChanged.run();
        });
        onSelection(screen.modeSelector, this::rebuildRefreshRates);
        onSelection(screen.rateSelector, this::updateSourceDescription);
    }

    void start() {
        if (!displayListenerRegistered) {
            activity.getSystemService(DisplayManager.class).registerDisplayListener(displayListener, mainHandler);
            displayListenerRegistered = true;
        }
        refreshDisplayInventory();
    }

    void stop() {
        if (displayListenerRegistered) {
            activity.getSystemService(DisplayManager.class).unregisterDisplayListener(displayListener);
            displayListenerRegistered = false;
        }
    }

    boolean hasMonitor() { return adapterCapabilities != null; }

    int selectedRotation() {
        return Build.VERSION.SDK_INT >= 36 ? screen.rotationSelector.getSelectedItemPosition() : 0;
    }

    void prepareProbe() {
        capabilitiesBeforeProbe = adapterCapabilities;
        modeBeforeProbe = adapterCapabilities == null ? null : selectedMode();
    }

    void applyProbedCapabilities(AdapterCapabilities next) {
        applyCapabilities(next);
        if (next == null) return;
        if (capabilitiesBeforeProbe != null && modeBeforeProbe != null
                && java.util.Objects.equals(capabilitiesBeforeProbe.device, next.device)
                && capabilitiesBeforeProbe.monitorName.equals(next.monitorName)) {
            for (DisplayModeOption mode : next.modes) {
                if (mode.id != modeBeforeProbe.id) continue;
                for (int i = 0; i < screen.modeSelector.getCount(); ++i) {
                    if (mode.resolutionLabel().equals(screen.modeSelector.getItemAtPosition(i)))
                        screen.modeSelector.setSelection(i);
                }
                rebuildRefreshRates();
                for (int i = 0; i < screen.rateSelector.getCount(); ++i) {
                    if ((mode.refreshHz + " Hz").equals(screen.rateSelector.getItemAtPosition(i)))
                        screen.rateSelector.setSelection(i);
                }
                break;
            }
        }
        capabilitiesBeforeProbe = null;
        modeBeforeProbe = null;
    }

    void refreshDisplayInventory() {
        int selectedId = selectedSource() == null ? Display.DEFAULT_DISPLAY : selectedSource().id;
        screens.clear();
        for (Display display : activity.getSystemService(DisplayManager.class).getDisplays()) {
            if (!BuildConfig.MIRROR_ONLY || display.getDisplayId() == Display.DEFAULT_DISPLAY)
                screens.add(ScreenSource.fromDisplay(display));
        }
        if (Build.VERSION.SDK_INT >= 34) {
            screens.add(new ScreenSource(ScreenSource.APP_PICKER,
                    "App window · Android picker", 0, 0));
        }
        if (!BuildConfig.MIRROR_ONLY) screens.add(new ScreenSource(ScreenSource.CREATE_NEW, "Create a new screen", 0, 0));
        screen.setItems(screen.sourceSelector, new ArrayList<>(screens));
        int selectedIndex = 0;
        for (int i = 0; i < screens.size(); ++i) {
            if (screens.get(i).id == selectedId) selectedIndex = i;
        }
        screen.sourceSelector.setSelection(selectedIndex);
        updateSourceDescription();
        onChanged.run();
    }

    ScreenSource selectedSource() {
        return screen.sourceSelector == null ? null : (ScreenSource) screen.sourceSelector.getSelectedItem();
    }

    void applyCapabilities(AdapterCapabilities next) {
        adapterCapabilities = next;
        screen.monitorName.setText(next == null ? "Not connected" : next.monitorName);
        displayModes = next == null ? new ArrayList<>() : new ArrayList<>(next.modes);
        List<String> resolutions = new ArrayList<>();
        for (DisplayModeOption mode : displayModes) {
            String resolution = mode.resolutionLabel();
            if (!resolutions.contains(resolution)) resolutions.add(resolution);
        }
        if (resolutions.isEmpty()) resolutions.add("—");
        screen.setItems(screen.modeSelector, resolutions);
        DisplayModeOption initial = next == null ? null : next.initialMode;
        int preferred = initial == null ? 0 : resolutions.indexOf(initial.resolutionLabel());
        screen.modeSelector.setSelection(Math.max(0, preferred));
        rateResolution = null;
        rebuildRefreshRates();
        onChanged.run();
    }

    private void rebuildRefreshRates() {
        List<String> rates = new ArrayList<>();
        String resolution = String.valueOf(screen.modeSelector.getSelectedItem());
        String previousRate = resolution.equals(rateResolution)
                ? String.valueOf(screen.rateSelector.getSelectedItem()) : null;
        rateResolution = resolution;
        for (DisplayModeOption mode : displayModes) {
            if (mode.resolutionLabel().equals(resolution)) {
                String rate = mode.refreshHz + " Hz";
                if (!rates.contains(rate)) rates.add(rate);
            }
        }
        if (rates.isEmpty()) rates.add("—");
        screen.setItems(screen.rateSelector, rates);
        int selectedRate = rates.indexOf(previousRate);
        if (selectedRate < 0 && adapterCapabilities != null) {
            DisplayModeOption preferred = adapterCapabilities.preferredMode;
            if (preferred != null && resolution.equals(preferred.resolutionLabel())) {
                selectedRate = rates.indexOf(preferred.refreshHz + " Hz");
            }
        }
        screen.rateSelector.setSelection(Math.max(0, selectedRate));
        screen.modeDescription.setText(adapterCapabilities == null
                ? "Only modes confirmed by both the monitor EDID and adapter will be offered."
                : "Monitor refresh does not set capture FPS.");
        updateSourceDescription();
        onChanged.run();
    }

    DisplayModeOption selectedMode() {
        String resolution = String.valueOf(screen.modeSelector.getSelectedItem());
        String rate = String.valueOf(screen.rateSelector.getSelectedItem());
        for (DisplayModeOption mode : displayModes) {
            if (mode.resolutionLabel().equals(resolution)
                    && (mode.refreshHz + " Hz").equals(rate)) return mode;
        }
        for (DisplayModeOption mode : NativeAdapter.modeCatalog()) {
            if (mode.id == 0x40) return mode;
        }
        throw new IllegalStateException("Shared mode catalog is unavailable");
    }

    private void updateSourceDescription() {
        if (screen.sourceDescription == null || selectedSource() == null) return;
        ScreenSource source = selectedSource();
        if (source.id == ScreenSource.CREATE_NEW) {
            screen.sourceDescription.setText("Creates an app-owned screen with a moving color test scene. "
                    + "Opening other apps needs the desktop helper, which is not included yet. "
                    + (Build.VERSION.SDK_INT >= 34
                        ? "Android will be asked for the selected output refresh; it may round it."
                        : "Android controls the new screen's refresh rate on this version."));
        } else if (source.canStart()) {
            screen.sourceDescription.setText(source.id == ScreenSource.APP_PICKER
                    ? "Choose the app window in Android's consent dialog."
                    : String.format(Locale.US, "Capture this screen after Android consent. "
                        + "Source: %.1f Hz · rotation %d°. Source refresh stays Android-managed.",
                        source.refreshHz, source.rotation * 90));
        } else {
            screen.sourceDescription.setText("This screen is visible to Android, but selecting it for "
                    + "capture needs the desktop helper. It cannot be captured by this build.");
        }
    }

    private static void onSelection(Spinner spinner, Runnable action) {
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View view,
                                                 int position, long id) { action.run(); }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });
    }
}
