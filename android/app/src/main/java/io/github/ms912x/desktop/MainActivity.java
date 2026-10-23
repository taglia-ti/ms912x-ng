package io.github.ms912x.desktop;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.ActivityOptions;
import android.app.AlertDialog;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.projection.MediaProjectionConfig;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.text.InputType;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicLong;

public final class MainActivity extends Activity {
    private static final int REQUEST_SCREEN_CAPTURE = 47;
    private static final long DISCOVERY_TIMEOUT_MS = 15_000L;
    private static final int INK = 0xff14243b;
    private static final int MUTED = 0xff526477;
    private static final int ACCENT = 0xff1264a3;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Set<String> endpointRows = new LinkedHashSet<>();
    private final List<AdbEndpointDiscovery.Endpoint> pairingEndpoints = new ArrayList<>();
    private final List<AdbEndpointDiscovery.Endpoint> connectEndpoints = new ArrayList<>();
    private final ExecutorService adbExecutor = Executors.newSingleThreadExecutor();
    private final AtomicLong acceptedFrames = new AtomicLong();
    private final AtomicLong rejectedFrames = new AtomicLong();
    private LinearLayout page;
    private FrameLayout root;
    private ScrollView mainScroll;
    private ScrollView settingsScroll;
    private ScrollView debugScroll;
    private View drawerScrim;
    private LinearLayout drawerPanel;
    private OnBackInvokedCallback backInvokedCallback;
    private LinearLayout endpoints;
    private TextView status;
    private TextView sourceDescription;
    private TextView rotationDescription;
    private TextView modeDescription;
    private TextView captureStatus;
    private TextView outputStatus;
    private TextView launchStatus;
    private Spinner sourceSelector;
    private Spinner modeSelector;
    private Spinner rateSelector;
    private Spinner rotationSelector;
    private Button discoverButton;
    private Button pairButton;
    private Button probeButton;
    private Button startButton;
    private Button stopButton;
    private Button previewButton;
    private Button sceneButton;
    private Button menuButton;
    private Button drawerSettingsButton;
    private Button drawerDebugButton;
    private final List<ScreenSource> screens = new ArrayList<>();
    private List<DisplayModeOption> displayModes = new ArrayList<>();
    private AdapterCapabilities adapterCapabilities;
    private DisplayModeOption sessionMode;
    private int sessionSource;
    private int sessionRotation;
    private float sessionRefreshHz;
    private boolean settingsLocked;
    private boolean pendingStartOutput;
    private boolean displayListenerRegistered;
    private CaptureController captureController;
    private VirtualDisplay captureDisplay;
    private TestScreenPresentation presentation;
    private ProjectionCaptureService projectionService;
    private boolean projectionServiceBound;
    private boolean projectionServiceStarted;
    private UsbOutputController usbOutputController;
    private AdbEndpointDiscovery discovery;
    private AdbCredentialStore adbCredentialStore;

    private final DisplayManager.DisplayListener displayListener =
            new DisplayManager.DisplayListener() {
                @Override public void onDisplayAdded(int id) { refreshDisplayInventory(); }
                @Override public void onDisplayChanged(int id) { refreshDisplayInventory(); }
                @Override public void onDisplayRemoved(int id) {
                    if (settingsLocked && sessionSource == id) {
                        stopSharing();
                        captureStatus.setText("The selected screen was disconnected.");
                    }
                    refreshDisplayInventory();
                }
            };

    private final ServiceConnection projectionConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            projectionService = ((ProjectionCaptureService.LocalBinder) binder).service();
            projectionService.setListener((state, message) -> {
                captureStatus.setText(message);
                if (state == ProjectionCaptureService.State.FAILED
                        || state == ProjectionCaptureService.State.STOPPED) {
                    projectionServiceStarted = false;
                    pendingStartOutput = false;
                    settingsLocked = false;
                    usbOutputController.stopStreaming();
                    updateControls();
                } else if (state == ProjectionCaptureService.State.ACTIVE
                        && projectionService != null) {
                    startPendingOutput(projectionService.captureController());
                }
            });
            startPendingOutput(projectionService.captureController());
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            projectionService = null;
            projectionServiceBound = false;
            projectionServiceStarted = false;
            pendingStartOutput = false;
            settingsLocked = false;
            if (usbOutputController != null) usbOutputController.stopStreaming();
            updateControls();
        }
    };

    private final Runnable discoveryTimeout = () -> {
        stopDiscovery();
        status.setText("Discovery timed out. Enable Wireless debugging and try again.");
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        // Android's app-sharing picker can relaunch our launcher above the consent owner.
        // Keep that owner alive so its pending result and active capture stay attached.
        if (!isTaskRoot() && Intent.ACTION_MAIN.equals(getIntent().getAction())
                && getIntent().hasCategory(Intent.CATEGORY_LAUNCHER)) {
            finish();
            return;
        }
        adbCredentialStore = new AdbCredentialStore(this);
        getWindow().setStatusBarColor(Color.TRANSPARENT);
        getWindow().setNavigationBarColor(Color.TRANSPARENT);
        getWindow().setDecorFitsSystemWindows(false);
        getWindow().setNavigationBarContrastEnforced(false);

        root = new FrameLayout(this);
        root.setBackgroundColor(0xfff0f4f8);
        mainScroll = new ScrollView(this);
        mainScroll.setFillViewport(true);
        mainScroll.setBackgroundColor(0xfff0f4f8);
        mainScroll.setOnApplyWindowInsetsListener((view, insets) -> {
            Insets bars = insets.getInsets(WindowInsets.Type.systemBars()
                    | WindowInsets.Type.displayCutout());
            view.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            return insets;
        });
        page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(dp(20), dp(12), dp(20), dp(24));
        mainScroll.addView(page);
        root.addView(mainScroll, new FrameLayout.LayoutParams(-1, -1));
        buildDrawer();
        setContentView(root);
        if (Build.VERSION.SDK_INT >= 33) {
            backInvokedCallback = this::handleBack;
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, backInvokedCallback);
        }
        getWindow().getInsetsController().setSystemBarsAppearance(
                WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                        | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS,
                WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                        | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS);

        LinearLayout topBar = new LinearLayout(this);
        topBar.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = new TextView(this);
        title.setText("MS912x Desktop");
        title.setTextSize(25);
        title.setTextColor(INK);
        title.setPadding(0, dp(6), 0, dp(6));
        title.setTypeface(null, Typeface.BOLD);
        topBar.addView(title, new LinearLayout.LayoutParams(0, -2, 1));
        menuButton = new Button(this);
        menuButton.setText("⚙");
        menuButton.setTextSize(22);
        menuButton.setContentDescription("Settings and navigation");
        menuButton.setAllCaps(false);
        menuButton.setMinWidth(dp(48));
        menuButton.setOnClickListener(view -> openDrawer());
        topBar.addView(menuButton, new LinearLayout.LayoutParams(dp(52), dp(52)));
        page.addView(topBar);
        text(page, "Your phone, on the big screen", 15, MUTED);
        TextView build = text(page, "ANDROID " + Build.VERSION.RELEASE, 11, ACCENT);
        build.setPadding(0, dp(4), 0, dp(16));

        LinearLayout sourceCard = card("01  Screen");
        sourceSelector = spinner(sourceCard, "Screen to share");
        sourceDescription = text(sourceCard, "", 14, MUTED);
        rotationSelector = spinner(sourceCard, "Screen rotation");
        setItems(rotationSelector, Build.VERSION.SDK_INT >= 36
                ? List.of("0° · Default", "90°", "180°", "270°")
                : List.of("0° · Android managed"));
        rotationDescription = text(sourceCard, Build.VERSION.SDK_INT >= 36
                ? "Rotates this capture/new screen using Android's virtual-display control."
                : "Manual virtual-screen rotation requires Android 16+. "
                    + "This phone follows Android's capture orientation.", 13, MUTED);

        LinearLayout modeCard = card("02  Monitor output");
        probeButton = button(modeCard, "Read monitor modes", () -> usbOutputController.probeModes());
        outputStatus = text(modeCard,
                "Connect an MS912x adapter through USB host/OTG, then read the monitor EDID.",
                14, MUTED);
        modeSelector = spinner(modeCard, "Resolution · EDID ∩ adapter modes");
        rateSelector = spinner(modeCard, "Monitor refresh rate");
        modeDescription = text(modeCard, "", 13, MUTED);
        applyCapabilities(null);

        LinearLayout actionCard = card("03  Share");
        startButton = button(actionCard, "Start sharing", () -> startSession(true));
        startButton.setBackgroundTintList(new ColorStateList(
                new int[][] {new int[] {android.R.attr.state_enabled}, new int[] {}},
                new int[] {ACCENT, 0xffdde5ee}));
        startButton.setTextColor(new ColorStateList(
                new int[][] {new int[] {android.R.attr.state_enabled}, new int[] {}},
                new int[] {Color.WHITE, MUTED}));
        stopButton = button(actionCard, "Stop sharing", this::stopSharing);
        captureStatus = text(actionCard, "Ready. Read monitor modes to enable sharing.", 14, INK);

        buildSettingsScreen();
        buildDebugScreen();

        usbOutputController = new UsbOutputController(this, new UsbOutputController.Listener() {
            @Override public void onStatus(String message) { outputStatus.setText(message); }
            @Override public void onCapabilities(AdapterCapabilities next) {
                if (next == null && settingsLocked) stopSharing();
                applyCapabilities(next);
            }
            @Override public void onBusyChanged(boolean busy) { updateControls(); }
        });
        onSelection(sourceSelector, () -> {
            updateSourceDescription();
            updateControls();
        });
        onSelection(modeSelector, this::rebuildRefreshRates);
        onSelection(rateSelector, this::updateSourceDescription);
        refreshDisplayInventory();
        updateControls();
    }

    private void buildDebugScreen() {
        debugScroll = makeSecondaryPage("Debug tools", "Back to sharing", this::showMainPage);
        LinearLayout debug = (LinearLayout) debugScroll.getChildAt(0);
        page = debug;
        previewButton = button(debug, "Test capture without USB output",
                () -> startSession(false));
        text(debug, "Without monitor EDID, capture-only tests use 640×480. "
                + "Monitor refresh is independent of capture rate.",
                13, MUTED);
        status = text(debug, NativeAdbBridge.nativeBackendVersion()
                + "\nThe independent desktop helper is not included in this test build.",
                13, MUTED);
        discoverButton = button(debug, "Find Wireless debugging endpoints", this::startDiscovery);
        pairButton = button(debug, "Pair with this phone", this::choosePairingEndpoint);
        pairButton.setEnabled(false);
        button(debug, "Debug: test local adbd TCP port 5555", this::runLocalTcpProbe);
        button(debug, "Probe app launch on the new screen", this::probeAppLaunchPermission);
        sceneButton = button(debug, "Hide test scene", this::toggleTestScene);
        launchStatus = text(debug, "App launch probe not run.", 13, MUTED);
        endpoints = new LinearLayout(this);
        endpoints.setOrientation(LinearLayout.VERTICAL);
        debug.addView(endpoints);
        root.addView(debugScroll, new FrameLayout.LayoutParams(-1, -1));
        debugScroll.setVisibility(View.GONE);
        page = (LinearLayout) mainScroll.getChildAt(0);
    }

    private void buildSettingsScreen() {
        settingsScroll = makeSecondaryPage("Settings", "Back to sharing", this::showMainPage);
        LinearLayout settings = (LinearLayout) settingsScroll.getChildAt(0);
        page = settings;
        LinearLayout profile = card("Profile");
        text(profile, "Profile selection will be available here.", 14, MUTED);
        LinearLayout output = card("Image matching");
        text(output, "Stride and color tolerance controls are planned here.", 14, MUTED);
        text(output, "These options will tune how the captured image is compared and sent.", 13, MUTED);
        root.addView(settingsScroll, new FrameLayout.LayoutParams(-1, -1));
        settingsScroll.setVisibility(View.GONE);
        page = (LinearLayout) mainScroll.getChildAt(0);
    }

    private ScrollView makeSecondaryPage(String title, String backLabel, Runnable back) {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(0xfff0f4f8);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(20), dp(16), dp(20), dp(24));
        scroll.addView(content);
        Button backButton = button(content, backLabel, back);
        backButton.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        TextView heading = text(content, title, 27, INK);
        heading.setTypeface(null, Typeface.BOLD);
        return scroll;
    }

    private void buildDrawer() {
        drawerScrim = new View(this);
        drawerScrim.setBackgroundColor(0x66000000);
        drawerScrim.setVisibility(View.GONE);
        drawerScrim.setOnClickListener(view -> closeDrawer());
        root.addView(drawerScrim, new FrameLayout.LayoutParams(-1, -1));
        drawerPanel = new LinearLayout(this);
        drawerPanel.setOrientation(LinearLayout.VERTICAL);
        drawerPanel.setPadding(dp(20), dp(40), dp(20), dp(20));
        drawerPanel.setBackgroundColor(Color.WHITE);
        drawerPanel.setVisibility(View.GONE);
        TextView brand = text(drawerPanel, "MS912x", 24, INK);
        brand.setTypeface(null, Typeface.BOLD);
        text(drawerPanel, "Desktop display", 14, MUTED);
        drawerSettingsButton = button(drawerPanel, "Settings", () -> { closeDrawer(); showSettingsPage(); });
        drawerDebugButton = button(drawerPanel, "Debug", () -> { closeDrawer(); showDebugPage(); });
        root.addView(drawerPanel, new FrameLayout.LayoutParams(dp(300), -1, Gravity.START));
    }

    private void openDrawer() {
        drawerScrim.setVisibility(View.VISIBLE);
        drawerPanel.setVisibility(View.VISIBLE);
        drawerScrim.bringToFront();
        drawerPanel.bringToFront();
    }

    private void closeDrawer() {
        drawerScrim.setVisibility(View.GONE);
        drawerPanel.setVisibility(View.GONE);
    }

    private void showMainPage() { showPage(mainScroll); }
    private void showSettingsPage() { showPage(settingsScroll); }
    private void showDebugPage() { showPage(debugScroll); }

    private void showPage(View pageView) {
        mainScroll.setVisibility(pageView == mainScroll ? View.VISIBLE : View.GONE);
        settingsScroll.setVisibility(pageView == settingsScroll ? View.VISIBLE : View.GONE);
        debugScroll.setVisibility(pageView == debugScroll ? View.VISIBLE : View.GONE);
    }

    @SuppressLint("GestureBackNavigation")
    @Override
    public void onBackPressed() {
        handleBack();
    }

    private void handleBack() {
        if (drawerPanel != null && drawerPanel.getVisibility() == View.VISIBLE) closeDrawer();
        else if (settingsScroll != null && settingsScroll.getVisibility() == View.VISIBLE) showMainPage();
        else if (debugScroll != null && debugScroll.getVisibility() == View.VISIBLE) showMainPage();
        else finish();
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private TextView text(LinearLayout parent, String value, int size, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(color);
        view.setPadding(0, dp(6), 0, dp(6));
        parent.addView(view);
        return view;
    }

    private LinearLayout card(String heading) {
        LinearLayout container = new LinearLayout(this);
        container.setOrientation(LinearLayout.VERTICAL);
        container.setPadding(dp(16), dp(14), dp(16), dp(16));
        GradientDrawable background = new GradientDrawable();
        background.setColor(Color.WHITE);
        background.setCornerRadius(dp(16));
        background.setStroke(dp(1), 0xffdde5ee);
        container.setBackground(background);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2);
        params.bottomMargin = dp(16);
        page.addView(container, params);
        TextView label = text(container, heading, 18, INK);
        label.setTypeface(null, Typeface.BOLD);
        return container;
    }

    private Button button(LinearLayout parent, String label, Runnable action) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setMinHeight(dp(48));
        button.setBackgroundTintList(new ColorStateList(
                new int[][] {new int[] {android.R.attr.state_enabled}, new int[] {}},
                new int[] {0xffe4edf7, 0xffedf1f5}));
        button.setTextColor(new ColorStateList(
                new int[][] {new int[] {android.R.attr.state_enabled}, new int[] {}},
                new int[] {ACCENT, MUTED}));
        button.setOnClickListener(view -> action.run());
        parent.addView(button, new LinearLayout.LayoutParams(-1, -2));
        return button;
    }

    private Spinner spinner(LinearLayout parent, String label) {
        text(parent, label, 12, MUTED);
        Spinner spinner = new Spinner(this, Spinner.MODE_DROPDOWN);
        spinner.setContentDescription(label);
        parent.addView(spinner, new LinearLayout.LayoutParams(-1, dp(64)));
        return spinner;
    }

    private <T> void setItems(Spinner spinner, List<T> options) {
        ArrayAdapter<T> adapter = new ArrayAdapter<>(this, R.layout.spinner_item, options);
        adapter.setDropDownViewResource(R.layout.spinner_item);
        spinner.setAdapter(adapter);
    }

    private void onSelection(Spinner spinner, Runnable action) {
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View view,
                                                 int position, long id) { action.run(); }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });
    }

    private void refreshDisplayInventory() {
        int selectedId = selectedSource() == null ? Display.DEFAULT_DISPLAY : selectedSource().id;
        screens.clear();
        for (Display display : getSystemService(DisplayManager.class).getDisplays()) {
            screens.add(ScreenSource.fromDisplay(display));
        }
        if (Build.VERSION.SDK_INT >= 34) {
            screens.add(new ScreenSource(ScreenSource.APP_PICKER,
                    "App window · Android picker", 0, 0));
        }
        screens.add(new ScreenSource(ScreenSource.CREATE_NEW, "Create a new screen", 0, 0));
        setItems(sourceSelector, new ArrayList<>(screens));
        int selectedIndex = 0;
        for (int i = 0; i < screens.size(); ++i) {
            if (screens.get(i).id == selectedId) selectedIndex = i;
        }
        sourceSelector.setSelection(selectedIndex);
        updateSourceDescription();
        updateControls();
    }

    private ScreenSource selectedSource() {
        return sourceSelector == null ? null : (ScreenSource) sourceSelector.getSelectedItem();
    }

    private void applyCapabilities(AdapterCapabilities next) {
        adapterCapabilities = next;
        displayModes = next == null ? new ArrayList<>() : new ArrayList<>(next.modes);
        displayModes.sort(Comparator.comparingInt((DisplayModeOption mode) ->
                mode.width * mode.height).thenComparingInt(mode -> mode.width)
                .thenComparingInt(mode -> mode.refreshHz));
        List<String> resolutions = new ArrayList<>();
        for (DisplayModeOption mode : displayModes) {
            String resolution = mode.width + "×" + mode.height;
            if (!resolutions.contains(resolution)) resolutions.add(resolution);
        }
        if (resolutions.isEmpty()) resolutions.add("Read monitor modes first");
        setItems(modeSelector, resolutions);
        int safe = resolutions.indexOf("640×480");
        modeSelector.setSelection(Math.max(0, safe));
        rebuildRefreshRates();
        updateControls();
    }

    private void rebuildRefreshRates() {
        List<String> rates = new ArrayList<>();
        String resolution = String.valueOf(modeSelector.getSelectedItem());
        for (DisplayModeOption mode : displayModes) {
            if ((mode.width + "×" + mode.height).equals(resolution)) {
                String rate = mode.refreshHz + " Hz";
                if (!rates.contains(rate)) rates.add(rate);
            }
        }
        if (rates.isEmpty()) rates.add("Waiting for EDID");
        setItems(rateSelector, rates);
        modeDescription.setText(adapterCapabilities == null
                ? "Only modes confirmed by both the monitor EDID and adapter will be offered."
                : adapterCapabilities.monitorName + " · " + displayModes.size()
                    + " EDID-compatible timings. Monitor refresh does not set capture FPS.");
        updateSourceDescription();
        updateControls();
    }

    private DisplayModeOption selectedMode() {
        String resolution = String.valueOf(modeSelector.getSelectedItem());
        String rate = String.valueOf(rateSelector.getSelectedItem());
        for (DisplayModeOption mode : displayModes) {
            if ((mode.width + "×" + mode.height).equals(resolution)
                    && (mode.refreshHz + " Hz").equals(rate)) return mode;
        }
        for (DisplayModeOption mode : NativeCapture.modeCatalog()) {
            if (mode.id == 0x40) return mode;
        }
        throw new IllegalStateException("Shared mode catalog is unavailable");
    }

    private void updateSourceDescription() {
        if (sourceDescription == null || selectedSource() == null) return;
        ScreenSource source = selectedSource();
        if (source.id == ScreenSource.CREATE_NEW) {
            sourceDescription.setText("Creates an app-owned screen with a moving color test scene. "
                    + "Opening other apps needs the desktop helper, which is not included yet. "
                    + (Build.VERSION.SDK_INT >= 34
                        ? "Android will be asked for the selected output refresh; it may round it."
                        : "Android controls the new screen's refresh rate on this version."));
        } else if (source.canStart()) {
            sourceDescription.setText(source.id == ScreenSource.APP_PICKER
                    ? "Choose the app window in Android's consent dialog."
                    : String.format(Locale.US, "Capture this screen after Android consent. "
                        + "Source: %.1f Hz · rotation %d°. Source refresh stays Android-managed.",
                        source.refreshHz, source.rotation * 90));
        } else {
            sourceDescription.setText("This screen is visible to Android, but selecting it for "
                    + "capture needs the desktop helper. It cannot be captured by this build.");
        }
    }

    private void updateControls() {
        if (startButton == null) return;
        boolean busy = usbOutputController != null && usbOutputController.isBusy();
        ScreenSource source = selectedSource();
        boolean usable = source != null && source.canStart();
        sourceSelector.setEnabled(!settingsLocked);
        modeSelector.setEnabled(!settingsLocked && !busy && adapterCapabilities != null);
        rateSelector.setEnabled(!settingsLocked && !busy && adapterCapabilities != null);
        rotationSelector.setEnabled(!settingsLocked && Build.VERSION.SDK_INT >= 36);
        probeButton.setEnabled(!settingsLocked && !busy);
        startButton.setEnabled(!settingsLocked && !busy && usable && adapterCapabilities != null);
        stopButton.setEnabled(settingsLocked || busy);
        if (previewButton != null) previewButton.setEnabled(!settingsLocked && !busy && usable);
        if (sceneButton != null) {
            sceneButton.setEnabled(captureDisplay != null);
            sceneButton.setText(presentation == null ? "Show test scene" : "Hide test scene");
        }
    }

    private void startSession(boolean sendOutput) {
        if (settingsLocked || usbOutputController.isBusy()) return;
        ScreenSource source = selectedSource();
        if (source == null || !source.canStart() || (sendOutput && adapterCapabilities == null)) return;
        sessionMode = selectedMode();
        sessionSource = source.id;
        sessionRotation = Build.VERSION.SDK_INT >= 36 ? rotationSelector.getSelectedItemPosition() : 0;
        settingsLocked = true;
        pendingStartOutput = sendOutput;
        updateControls();
        if (source.id == ScreenSource.CREATE_NEW) {
            startCaptureTest();
        } else {
            requestScreenCaptureConsent();
        }
    }

    private void requestScreenCaptureConsent() {
        stopProjectionCapture();
        stopCaptureTest();
        MediaProjectionManager manager = getSystemService(MediaProjectionManager.class);
        Intent consent = Build.VERSION.SDK_INT >= 34 && sessionSource == Display.DEFAULT_DISPLAY
                ? manager.createScreenCaptureIntent(MediaProjectionConfig.createConfigForDefaultDisplay())
                : manager.createScreenCaptureIntent();
        try {
            startActivityForResult(consent, REQUEST_SCREEN_CAPTURE);
        } catch (RuntimeException failure) {
            settingsLocked = false;
            pendingStartOutput = false;
            captureStatus.setText("Cannot request screen capture: " + failure.getMessage());
            updateControls();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_SCREEN_CAPTURE || !settingsLocked || sessionMode == null) return;
        if (resultCode != RESULT_OK || data == null) {
            settingsLocked = false;
            pendingStartOutput = false;
            captureStatus.setText("Screen capture consent was declined.");
            updateControls();
            return;
        }
        DisplayMetrics metrics = getResources().getDisplayMetrics();
        Intent service = new Intent(this, ProjectionCaptureService.class)
                .putExtra(ProjectionCaptureService.EXTRA_RESULT_CODE, resultCode)
                .putExtra(ProjectionCaptureService.EXTRA_RESULT_DATA, data)
                .putExtra(ProjectionCaptureService.EXTRA_WIDTH, sessionMode.width)
                .putExtra(ProjectionCaptureService.EXTRA_HEIGHT, sessionMode.height)
                .putExtra(ProjectionCaptureService.EXTRA_ROTATION, sessionRotation)
                .putExtra(ProjectionCaptureService.EXTRA_DENSITY_DPI, metrics.densityDpi);
        try {
            startForegroundService(service);
            projectionServiceStarted = true;
            captureStatus.setText("Consent granted. Starting screen capture…");
            bindProjectionService();
        } catch (RuntimeException failure) {
            stopSharing();
            captureStatus.setText("Screen capture could not start: " + failure.getMessage());
        }
    }

    private void bindProjectionService() {
        if (!projectionServiceBound) {
            projectionServiceBound = bindService(new Intent(this, ProjectionCaptureService.class),
                    projectionConnection, Context.BIND_AUTO_CREATE);
        }
    }

    private void stopProjectionCapture() {
        if (projectionService != null) {
            projectionService.setListener(null);
            projectionService.stopCapture();
            projectionService = null;
        }
        if (projectionServiceBound) {
            unbindService(projectionConnection);
            projectionServiceBound = false;
        }
        if (projectionServiceStarted) stopService(new Intent(this, ProjectionCaptureService.class));
        projectionServiceStarted = false;
    }

    private void startDiscovery() {
        if (discovery != null) {
            discovery.close();
            discovery = null;
        }
        endpointRows.clear();
        pairingEndpoints.clear();
        connectEndpoints.clear();
        endpoints.removeAllViews();
        status.setText("Searching local DNS-SD for pairing and TLS connection services.\n"
                + "Only endpoints on this phone's network interfaces are accepted.");
        discovery = new AdbEndpointDiscovery(this, new AdbEndpointDiscovery.Listener() {
            @Override
            public void onEndpoint(AdbEndpointDiscovery.Endpoint endpoint) {
                runOnUiThread(() -> {
                    if (discovery != null && endpointRows.add(endpoint.toString())) {
                        TextView row = new TextView(MainActivity.this);
                        row.setText(endpoint.toString());
                        row.setTextSize(15);
                        endpoints.addView(row);
                        if (AdbEndpointDiscovery.PAIRING_SERVICE.equals(endpoint.serviceType)) {
                            pairingEndpoints.add(endpoint);
                        } else if (AdbEndpointDiscovery.CONNECT_SERVICE.equals(
                                endpoint.serviceType)) {
                            connectEndpoints.add(endpoint);
                        }
                        refreshPairButton();
                    }
                });
            }

            @Override
            public void onFailure(String serviceType, int errorCode) {
                runOnUiThread(() -> status.setText(
                        "Discovery error for " + serviceType + ": " + errorCode
                                + "\nCheck that Wireless debugging is enabled."));
            }
        });
        try {
            discovery.start();
            discoverButton.setEnabled(false);
            mainHandler.postDelayed(discoveryTimeout, DISCOVERY_TIMEOUT_MS);
        } catch (RuntimeException failure) {
            stopDiscovery();
            status.setText("Could not start local service discovery: "
                    + failure.getClass().getSimpleName());
        }
    }

    private void stopDiscovery() {
        mainHandler.removeCallbacks(discoveryTimeout);
        if (discovery != null) {
            discovery.close();
            discovery = null;
        }
        if (discoverButton != null) {
            discoverButton.setEnabled(true);
        }
    }

    private void refreshPairButton() {
        boolean compatible = false;
        for (AdbEndpointDiscovery.Endpoint pairing : pairingEndpoints) {
            for (AdbEndpointDiscovery.Endpoint connect : connectEndpoints) {
                if (pairing.address.equals(connect.address)) {
                    compatible = true;
                    break;
                }
            }
            if (compatible) {
                break;
            }
        }
        if (pairButton != null) {
            pairButton.setEnabled(compatible);
        }
    }

    private void choosePairingEndpoint() {
        List<AdbEndpointDiscovery.Endpoint[]> pairs = new ArrayList<>();
        List<String> labels = new ArrayList<>();
        for (AdbEndpointDiscovery.Endpoint pairing : pairingEndpoints) {
            for (AdbEndpointDiscovery.Endpoint connect : connectEndpoints) {
                if (pairing.address.equals(connect.address)) {
                    pairs.add(new AdbEndpointDiscovery.Endpoint[]{pairing, connect});
                    labels.add(pairing.address.getHostAddress() + " (pair " + pairing.port
                            + ", connect " + connect.port + ")");
                }
            }
        }
        if (pairs.isEmpty()) {
            status.setText("No matching pairing and TLS endpoints were discovered.");
            return;
        }
        String[] options = labels.toArray(new String[0]);
        new AlertDialog.Builder(this)
                .setTitle("Select this device's ADB endpoints")
                .setItems(options, (dialog, which) -> promptPairingCode(pairs.get(which)))
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void promptPairingCode(AdbEndpointDiscovery.Endpoint[] pair) {
        EditText codeInput = new EditText(this);
        codeInput.setInputType(InputType.TYPE_CLASS_NUMBER
                | InputType.TYPE_NUMBER_VARIATION_PASSWORD);
        codeInput.setHint("6-digit pairing code");
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        codeInput.setPadding(pad, pad / 2, pad, pad / 2);
        new AlertDialog.Builder(this)
                .setTitle("Enter the Wireless debugging pairing code")
                .setView(codeInput)
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Pair", (dialog, which) -> {
                    String code = codeInput.getText().toString();
                    codeInput.setText("");
                    runPairing(pair[0], pair[1], code);
                })
                .show();
    }

    private void runPairing(AdbEndpointDiscovery.Endpoint pairing,
                            AdbEndpointDiscovery.Endpoint connect, String code) {
        pairButton.setEnabled(false);
        status.setText("Pairing over Android Wireless debugging and verifying shell access…");
        adbExecutor.execute(() -> {
            String result;
            try {
                result = NativeAdbBridge.nativePairAndVerify(adbCredentialStore,
                        pairing.address.getHostAddress(), pairing.port,
                        connect.address.getHostAddress(), connect.port, code);
            } catch (Exception | LinkageError failure) {
                result = "Wireless ADB failed: " + failure.getClass().getSimpleName();
            }
            String finalResult = result;
            runOnUiThread(() -> {
                status.setText(NativeAdbBridge.nativeBackendVersion() + "\n" + finalResult);
                refreshPairButton();
            });
        });
    }

    private void runLocalTcpProbe() {
        String host = adbCredentialStore.firstLocalIpv4Address();
        if (host == null) {
            status.setText("No local IPv4 address is available for the debug ADB probe.");
            return;
        }
        status.setText("Connecting to local adbd at " + host + ":5555…");
        adbExecutor.execute(() -> {
            String result;
            try {
                result = NativeAdbBridge.nativeConnectLocalTcpAndVerify(
                        adbCredentialStore, host, 5555);
            } catch (Exception | LinkageError failure) {
                result = "Local ADB TCP probe failed: "
                        + failure.getClass().getSimpleName();
            }
            String finalResult = result;
            runOnUiThread(() -> status.setText(
                    NativeAdbBridge.nativeBackendVersion() + "\n" + finalResult));
        });
    }

    private void startCaptureTest() {
        stopProjectionCapture();
        stopCaptureTest();
        acceptedFrames.set(0);
        rejectedFrames.set(0);
        try {
            captureController = new CaptureController(sessionMode.width, sessionMode.height, 1,
                    result -> {
                        if (result == 0) acceptedFrames.incrementAndGet();
                        else rejectedFrames.incrementAndGet();
                        runOnUiThread(() -> {
                            if (captureController != null) {
                                captureStatus.setText(String.format(Locale.US,
                                        "New test screen · %.1f Hz · %d°\nCaptured %d frames · rejected %d",
                                        sessionRefreshHz, sessionRotation * 90,
                                        acceptedFrames.get(), rejectedFrames.get()));
                            }
                        });
                    });
            captureDisplay = VirtualScreenOptions.createOwnScreen(
                    getSystemService(DisplayManager.class), sessionMode.width, sessionMode.height,
                    160, captureController.surface(), sessionMode.refreshHz);
            if (captureDisplay == null) throw new IllegalStateException("Screen creation returned null");
            VirtualScreenOptions.applyRotation(captureDisplay, sessionRotation);
            sessionRefreshHz = captureDisplay.getDisplay().getRefreshRate();
            showTestScene();
            launchStatus.setText("App-owned screen " + captureDisplay.getDisplay().getDisplayId()
                    + " ready. App launch probe not run.");
            captureStatus.setText(String.format(Locale.US, "New test screen · Android %.1f Hz · %d°",
                    captureDisplay.getDisplay().getRefreshRate(), sessionRotation * 90));
            startPendingOutput(captureController);
            updateControls();
        } catch (RuntimeException failure) {
            stopSharing();
            captureStatus.setText("New screen failed: " + failure.getMessage());
        }
    }

    private void startPendingOutput(CaptureController capture) {
        if (pendingStartOutput && capture != null && capture.isOpen()) {
            pendingStartOutput = false;
            usbOutputController.startStreaming(capture, sessionMode);
        }
    }

    private void showTestScene() {
        presentation = new TestScreenPresentation(this, captureDisplay.getDisplay());
        presentation.show();
    }

    private void toggleTestScene() {
        if (captureDisplay == null) return;
        try {
            if (presentation == null) {
                showTestScene();
            } else {
                presentation.dismiss();
                presentation = null;
            }
            launchStatus.setText("Test scene " + (presentation == null ? "hidden" : "shown")
                    + " on screen " + captureDisplay.getDisplay().getDisplayId() + ".");
            updateControls();
        } catch (RuntimeException failure) {
            stopSharing();
            captureStatus.setText("Test scene failed: " + failure.getMessage());
        }
    }

    private void stopCaptureTest() {
        if (presentation != null) {
            presentation.dismiss();
            presentation = null;
        }
        if (captureDisplay != null) {
            captureDisplay.release();
            captureDisplay = null;
            launchStatus.setText("App-owned screen closed.");
        }
        if (captureController != null) {
            CaptureController old = captureController;
            captureController = null;
            old.close();
        }
    }

    private void stopSharing() {
        pendingStartOutput = false;
        if (usbOutputController != null) usbOutputController.stopStreaming();
        stopProjectionCapture();
        stopCaptureTest();
        settingsLocked = false;
        sessionMode = null;
        if (captureStatus != null) captureStatus.setText("Sharing stopped.");
        updateControls();
    }

    private void probeAppLaunchPermission() {
        if (captureDisplay == null) {
            launchStatus.setText("Create a new test screen before running this probe.");
            return;
        }
        int id = captureDisplay.getDisplay().getDisplayId();
        ActivityOptions options = ActivityOptions.makeBasic();
        options.setLaunchDisplayId(id);
        try {
            startActivity(new Intent("android.settings.SETTINGS"), options.toBundle());
            launchStatus.setText("App launch accepted for screen " + id + ".");
        } catch (RuntimeException failure) {
            launchStatus.setText("App launch denied: " + failure.getMessage());
        }
    }

    @Override
    protected void onStart() {
        super.onStart();
        if (sourceSelector != null) {
            if (!displayListenerRegistered) {
                getSystemService(DisplayManager.class).registerDisplayListener(
                        displayListener, mainHandler);
                displayListenerRegistered = true;
            }
            refreshDisplayInventory();
        }
        if (projectionServiceStarted) bindProjectionService();
    }

    @Override
    protected void onStop() {
        stopDiscovery();
        if (displayListenerRegistered) {
            getSystemService(DisplayManager.class).unregisterDisplayListener(displayListener);
            displayListenerRegistered = false;
        }
        if (projectionService != null) projectionService.setListener(null);
        if (projectionServiceBound) {
            unbindService(projectionConnection);
            projectionServiceBound = false;
            projectionService = null;
        }
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        if (Build.VERSION.SDK_INT >= 33 && backInvokedCallback != null) {
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backInvokedCallback);
            backInvokedCallback = null;
        }
        stopSharing();
        if (usbOutputController != null) usbOutputController.close();
        adbExecutor.shutdownNow();
        super.onDestroy();
    }
}
