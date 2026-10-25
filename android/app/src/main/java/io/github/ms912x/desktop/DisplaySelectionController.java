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
    private final DisplaySelectionPreferences preferences;
    private final SelectionListener sourceSelection, modeSelection, rateSelection, rotationSelection;
    private final List<ScreenSource> screens = new ArrayList<>();
    private List<DisplayModeOption> displayModes = new ArrayList<>();
    private AdapterCapabilities adapterCapabilities;
    private AdapterCapabilities capabilitiesBeforeProbe;
    private DisplayModeOption modeBeforeProbe;
    private String rateResolution;
    private boolean displayListenerRegistered;
    private List<SourceCapability> helperSources=java.util.Collections.emptyList();
    private boolean helperConnected;
    private boolean inventoryRestored;

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
        this(activity, screen, onChanged, onDisplayRemoved, new DisplaySelectionPreferences(activity));
    }

    DisplaySelectionController(Activity activity, MainScreenView screen, Runnable onChanged,
                               IntConsumer onDisplayRemoved, DisplaySelectionPreferences preferences) {
        this.activity = activity;
        this.screen = screen;
        this.onChanged = onChanged;
        this.onDisplayRemoved = onDisplayRemoved;
        this.preferences = preferences;
        sourceSelection = new SelectionListener(screen.sourceSelector, () -> {
            updateSourceDescription();
            saveOptions();
            onChanged.run();
        });
        modeSelection = new SelectionListener(screen.modeSelector, () -> { rebuildRefreshRates(); saveOptions(); });
        rateSelection = new SelectionListener(screen.rateSelector, () -> { updateSourceDescription(); saveOptions(); });
        rotationSelection = new SelectionListener(screen.rotationSelector, this::saveOptions);
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
        return Build.VERSION.SDK_INT >= 36 || (selectedSource()!=null&&selectedSource().helper)
                ? screen.rotationSelector.getSelectedItemPosition() : 0;
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
        synchronizeSelections();
    }

    void helperCapabilities(boolean connected,List<SourceCapability> sources) {
        if(helperConnected==connected&&helperSources.equals(sources))return;
        helperConnected=connected;helperSources=sources;refreshDisplayInventory();
    }

    SourceCapability helperSource(int id) {
        if(helperConnected)for(SourceCapability source:helperSources)if(source.id==id)return source;
        return null;
    }

    void restoreSession(int sourceId,DisplayModeOption mode,int rotation) {
        for(int i=0;i<screens.size();++i)if(screens.get(i).id==sourceId)screen.sourceSelector.setSelection(i);
        updateSourceDescription();
        if(mode!=null){
            for(int i=0;i<screen.modeSelector.getCount();++i)if(mode.resolutionLabel().equals(screen.modeSelector.getItemAtPosition(i)))screen.modeSelector.setSelection(i);
            rebuildRefreshRates();
            for(int i=0;i<screen.rateSelector.getCount();++i)if((mode.refreshHz+" Hz").equals(screen.rateSelector.getItemAtPosition(i)))screen.rateSelector.setSelection(i);
        }
        if(rotation>=0&&rotation<screen.rotationSelector.getCount())screen.rotationSelector.setSelection(rotation);
        synchronizeSelections();
    }

    void refreshDisplayInventory() {
        int selectedId = selectedSource() == null ? Display.DEFAULT_DISPLAY : selectedSource().id;
        screens.clear();
        for (Display display : activity.getSystemService(DisplayManager.class).getDisplays()) {
            if(display.getName().startsWith("ms912x-"))continue;
            if (!BuildConfig.MIRROR_ONLY || display.getDisplayId() == Display.DEFAULT_DISPLAY) {
                ScreenSource basic=ScreenSource.fromDisplay(display);
                SourceCapability capability=helperSource(basic.id);
                screens.add(basic.id==0?basic:new ScreenSource(basic.id,basic.label,basic.refreshHz,basic.rotation,
                        capability==null?basic.identity:capability.identity,true,capability!=null&&capability.capture,
                        capability==null?"ADB setup required for this screen.":capability.reason));
            }
        }
        if(!BuildConfig.MIRROR_ONLY&&helperConnected)for(SourceCapability option:helperSources){
            boolean present=false;for(ScreenSource existing:screens)if(existing.id==option.id)present=true;
            if(!present&&!option.name.startsWith("ms912x-"))screens.add(new ScreenSource(option.id,
                    option.name+" (#"+option.id+") · "+option.width+"×"+option.height+" ("+DisplayModeOption.aspectRatio(option.width,option.height)+")",
                    0,option.rotation,option.identity,true,option.capture,option.reason));
        }
        if (Build.VERSION.SDK_INT >= 34) {
            screens.add(new ScreenSource(ScreenSource.APP_PICKER,
                    "App window · Android picker", 0, 0));
        }
        if (!BuildConfig.MIRROR_ONLY) {
            screens.add(new ScreenSource(ScreenSource.CREATE_NEW,"Create a new screen",0,0,"",true,true,
                    "Start will prepare the adapter and guide you through Wireless debugging setup."));
        }
        screen.setItems(screen.sourceSelector, new ArrayList<>(screens));
        int selectedIndex = 0;
        boolean restoredSource=false;
        if(!inventoryRestored&&preferences.source()!=null){
            for(int i=0;i<screens.size();i++)if(preferences.source().equals(DisplaySelectionPreferences.sourceKey(screens.get(i)))){selectedIndex=i;restoredSource=true;break;}
        }
        if(!restoredSource){
            for (int i = 0; i < screens.size(); ++i)if (screens.get(i).id == selectedId) selectedIndex = i;
        }
        screen.sourceSelector.setSelection(selectedIndex);
        updateSourceDescription();
        if(!inventoryRestored){
            int rotation = preferences.rotation();
            if(rotation>=0&&rotation<screen.rotationSelector.getCount())screen.rotationSelector.setSelection(rotation);
            inventoryRestored = true;
        }
        synchronizeSelections();
        onChanged.run();
    }

    ScreenSource selectedSource() {
        return screen.sourceSelector == null ? null : (ScreenSource) screen.sourceSelector.getSelectedItem();
    }

    void applyCapabilities(AdapterCapabilities next) {
        adapterCapabilities = next;
        screen.monitorName.setText(next == null ? "Not connected" : next.monitorName);
        // AdapterCapabilities already drops modes rejected by the helper's
        // VirtualDisplay limits, so unsupported sizes never enter this list.
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
        if(next!=null&&preferences.resolution()!=null){
            int cached=resolutions.indexOf(preferences.resolution());if(cached>=0)preferred=cached;
        }
        screen.modeSelector.setSelection(Math.max(0, preferred));
        rateResolution = null;
        rebuildRefreshRates();
        if(next!=null&&preferences.resolution()!=null&&preferences.resolution().equals(screen.modeSelector.getSelectedItem())){
            selectItem(screen.rateSelector, preferences.refresh());
        }
        synchronizeSelections();
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
        rateSelection.synchronize();
        screen.modeDescription.setText(adapterCapabilities == null
                ? "Only modes confirmed by both the monitor EDID and adapter will be offered."
                : "Monitor refresh does not set capture FPS.");
        updateSourceDescription();
        onChanged.run();
    }

    void clearSavedOptions(){
        clearSavedOptions(true);
    }

    void clearSavedOptions(boolean resetSelection){
        preferences.clear();
        if(!resetSelection)return;
        for(int i=0;i<screens.size();i++)if(screens.get(i).id==Display.DEFAULT_DISPLAY){screen.sourceSelector.setSelection(i);break;}
        screen.rotationSelector.setSelection(0);
        applyCapabilities(adapterCapabilities);
        updateSourceDescription();
        synchronizeSelections();
    }

    private void saveOptions(){
        DisplayModeOption mode=null;
        String resolution=String.valueOf(screen.modeSelector.getSelectedItem());
        String refresh=String.valueOf(screen.rateSelector.getSelectedItem());
        for(DisplayModeOption candidate:displayModes)
            if(candidate.resolutionLabel().equals(resolution)&&(candidate.refreshHz+" Hz").equals(refresh)){mode=candidate;break;}
        preferences.save(selectedSource(), selectedRotation(), mode);
    }

    private static void selectItem(Spinner spinner, String value){
        for(int i=0;i<spinner.getCount();i++)if(java.util.Objects.equals(value,spinner.getItemAtPosition(i))){spinner.setSelection(i);break;}
    }

    private void synchronizeSelections(){
        sourceSelection.synchronize();modeSelection.synchronize();
        rateSelection.synchronize();rotationSelection.synchronize();
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
        if(Build.VERSION.SDK_INT<36){
            int count=source.helper?4:1;
            if(screen.rotationSelector.getCount()!=count)screen.setItems(screen.rotationSelector,source.helper
                    ?java.util.Arrays.asList("0°","90°","180°","270°"):java.util.Arrays.asList("Auto"));
            rotationSelection.synchronize();
        }
        if(source.id==ScreenSource.CREATE_NEW){
            screen.sourceDescription.setText(helperConnected
                    ?"Creates an independent Android screen through the helper and sends it to HDMI. Use Open app after starting."
                    :"Start will check the adapter, find Wireless debugging automatically and guide any pairing steps needed.");
        } else if (source.id == ScreenSource.TEST_SCREEN) {
            screen.sourceDescription.setText("Creates an app-owned screen with a moving color test scene. "
                    + "Select Create a new screen to open other apps through the ADB helper. "
                    + (Build.VERSION.SDK_INT >= 34
                        ? "Android will be asked for the selected output refresh; it may round it."
                        : "Android controls the new screen's refresh rate on this version."));
        } else if (source.canStart()) {
            screen.sourceDescription.setText(source.helper
                    ? "Capture through the connected ADB helper; the original screen remains owned by its creator."
                    : source.id == ScreenSource.APP_PICKER
                    ? "Choose the app window in Android's consent dialog."
                    : String.format(Locale.US, "Capture this screen after Android consent. "
                        + "Source: %.1f Hz · rotation %d°. Source refresh stays Android-managed.",
                        source.refreshHz, source.rotation * 90));
        } else {
            screen.sourceDescription.setText(source.reason);
        }
    }

    /** Adapter rebuilds can dispatch delayed callbacks; acknowledge their final
     * selection explicitly instead of guessing when layout will finish. */
    private static final class SelectionListener implements AdapterView.OnItemSelectedListener {
        private final Spinner spinner;
        private final Runnable action;
        private Object adapter;
        private int position;
        SelectionListener(Spinner spinner,Runnable action){
            this.spinner=spinner;this.action=action;synchronize();spinner.setOnItemSelectedListener(this);
        }
        void synchronize(){adapter=spinner.getAdapter();position=spinner.getSelectedItemPosition();}
        @Override public void onItemSelected(AdapterView<?> parent,View view,int ignored,long id){
            if(adapter==spinner.getAdapter()&&position==spinner.getSelectedItemPosition())return;
            synchronize();action.run();
        }
        @Override public void onNothingSelected(AdapterView<?> parent){synchronize();}
    }
}
