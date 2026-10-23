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
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.ColorDrawable;
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
import android.widget.Switch;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.Arrays;
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

    private AppAppearance appearance;
    private final List<Runnable> appearanceBindings = new ArrayList<>();
    private Button appearanceButton;
    private Switch showDrawsToggle;

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
    private TextView monitorName;
    private TextView screencastStatus;
    private String userMessage;
    private boolean stoppingScreencast;
    private AdapterCapabilities capabilitiesBeforeProbe;
    private DisplayModeOption modeBeforeProbe;
    private TextView launchStatus;
    private Spinner sourceSelector;
    private Spinner modeSelector;
    private Spinner rateSelector;
    private Spinner rotationSelector;
    private String rateResolution;
    private Button discoverButton;
    private Button pairButton;
    private Button probeButton;
    private Button screencastButton;
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
                        showCaptureStatus("The selected screen was disconnected.");
                    }
                    refreshDisplayInventory();
                }
            };

    private final ServiceConnection projectionConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            projectionService = ((ProjectionCaptureService.LocalBinder) binder).service();
            projectionService.setListener((state, message) -> {
                showCaptureStatus(message);
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
        appearance = new AppAppearance(this);
        setTheme(appearance.theme());
        super.onCreate(state);
        // Android's app-sharing picker can relaunch our launcher above the consent owner.
        // Keep that owner alive so its pending result and active capture stay attached.
        if (!isTaskRoot() && Intent.ACTION_MAIN.equals(getIntent().getAction())
                && getIntent().hasCategory(Intent.CATEGORY_LAUNCHER)) {
            finish();
            return;
        }
        if (!BuildConfig.MIRROR_ONLY) adbCredentialStore = new AdbCredentialStore(this);
        getWindow().setStatusBarColor(Color.TRANSPARENT);
        getWindow().setNavigationBarColor(Color.TRANSPARENT);
        if (Build.VERSION.SDK_INT >= 30) getWindow().setDecorFitsSystemWindows(false);
        if (Build.VERSION.SDK_INT >= 29) getWindow().setNavigationBarContrastEnforced(false);

        root = new FrameLayout(this);
        themed(() -> root.setBackgroundColor(appearance.color(0xfff0f4f8)));
        mainScroll = new ScrollView(this);
        mainScroll.setFillViewport(true);
        themed(() -> mainScroll.setBackgroundColor(appearance.color(0xfff0f4f8)));
        if (Build.VERSION.SDK_INT >= 30) mainScroll.setOnApplyWindowInsetsListener((view, insets) -> {
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


        LinearLayout topBar = new LinearLayout(this);
        topBar.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = new TextView(this);
        title.setText(BuildConfig.MIRROR_ONLY ? "MS912x Mirror" : "MS912x Desktop");
        title.setTextSize(25);
        themed(() -> title.setTextColor(appearance.color(INK)));
        title.setPadding(0, dp(6), 0, dp(6));
        title.setTypeface(null, Typeface.BOLD);
        topBar.addView(title, new LinearLayout.LayoutParams(0, -2, 1));
        menuButton = new Button(this);
        menuButton.setText("⚙");
        menuButton.setTextSize(22);
        menuButton.setContentDescription("Settings and navigation");
        menuButton.setAllCaps(false);
        styleButton(menuButton);
        menuButton.setMinWidth(dp(48));
        menuButton.setOnClickListener(view -> openDrawer());
        topBar.addView(menuButton, new LinearLayout.LayoutParams(dp(52), dp(52)));
        page.addView(topBar);
        text(page, "Your phone, on the big screen", 15, MUTED);
        TextView build = text(page, "ANDROID " + Build.VERSION.RELEASE, 11, ACCENT);
        build.setPadding(0, dp(4), 0, dp(16));

        LinearLayout sourceCard = card("01  Screen");
        LinearLayout sourceRow = selectorRow(sourceCard);
        sourceSelector = spinner(selectorColumn(sourceRow, true), "Source screen",
                new RefreshingSpinner(this, this::refreshSourceOptions));
        rotationSelector = compactSpinner(selectorColumn(sourceRow, false), "Rotation", "Screen rotation");
        setItems(rotationSelector, Build.VERSION.SDK_INT >= 36
                ? Arrays.asList("0°", "90°", "180°", "270°")
                : Arrays.asList("Auto"));
        LinearLayout modeCard = card("02  Monitor output");
        monitorName = text(modeCard, "Not connected", 17, INK);
        LinearLayout modeRow = selectorRow(modeCard);
        modeSelector = spinner(selectorColumn(modeRow, true), "Resolution");
        modeSelector.setContentDescription("Resolution · EDID ∩ adapter modes");
        rateSelector = compactSpinner(selectorColumn(modeRow, false), "Refresh", "Monitor refresh rate");

        LinearLayout actionCard = card("03  Screencast");
        screencastButton = button(actionCard, "Start screencast", this::toggleScreencast);
        themed(() -> {
            screencastButton.setBackgroundTintList(states(appearance.color(ACCENT), appearance.color(0xffdde5ee)));
            screencastButton.setTextColor(states(appearance.isDark() ? 0xff082138 : Color.WHITE, appearance.color(MUTED)));
        });
        screencastStatus = text(actionCard, "Choose a screen to detect your monitor.", 14, MUTED);

        buildSettingsScreen();
        buildDebugScreen();
        applyCapabilities(null);

        usbOutputController = new UsbOutputController(this, new UsbOutputController.Listener() {
            @Override public void onStatus(String message) { showOutputStatus(message); }
            @Override public void onCapabilities(AdapterCapabilities next) {
                if (next == null && settingsLocked) stopSharing();
                applyProbedCapabilities(next);
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
        applyAppearance();
        probeMonitorModes();
    }

    private void buildDebugScreen() {
        debugScroll = makeSecondaryPage("Debug tools", "Back to screencast", this::showMainPage);
        LinearLayout debug = (LinearLayout) debugScroll.getChildAt(0);
        page = debug;
        showDrawsToggle = new Switch(this);
        showDrawsToggle.setText("Show drawing");
        showDrawsToggle.setMinHeight(dp(48));
        showDrawsToggle.setPadding(0, dp(8), 0, dp(8));
        debug.addView(showDrawsToggle, new LinearLayout.LayoutParams(-1, -2));
        themed(() -> {
            showDrawsToggle.setTextColor(appearance.color(INK));
            showDrawsToggle.setThumbTintList(new ColorStateList(
                    new int[][]{new int[]{android.R.attr.state_checked}, new int[]{}},
                    new int[]{appearance.color(ACCENT), appearance.color(MUTED)}));
            showDrawsToggle.setTrackTintList(ColorStateList.valueOf(appearance.color(MUTED)));
        });
        showDrawsToggle.setOnCheckedChangeListener((button, checked) -> {
            CaptureController capture = captureController != null ? captureController
                    : projectionService == null ? null : projectionService.captureController();
            if (capture != null) capture.setShowDraws(checked);
        });
        text(debug, "Outline submitted damage rectangles on the monitor.", 13, MUTED);
        probeButton = button(debug, "Read monitor modes", this::probeMonitorModes);
        outputStatus = text(debug, "Monitor not read yet.", 14, MUTED);
        sourceDescription = text(debug, "", 14, MUTED);
        rotationDescription = text(debug, Build.VERSION.SDK_INT >= 36
                ? "Rotates this capture/new screen using Android's virtual-display control."
                : "Manual virtual-screen rotation requires Android 16+. "
                    + "This phone follows Android's capture orientation.", 13, MUTED);
        modeDescription = text(debug, "", 13, MUTED);
        captureStatus = text(debug, "No capture session.", 14, INK);
        previewButton = button(debug, "Test capture without USB output",
                () -> startSession(false));
        text(debug, "Without monitor EDID, capture-only tests use 640×480. "
                + "Monitor refresh is independent of capture rate.",
                13, MUTED);
        status = text(debug, BuildConfig.MIRROR_ONLY ? "Screen mirroring build" : NativeAdbBridge.nativeBackendVersion()
                + "\nThe independent desktop helper is not included in this test build.",
                13, MUTED);
        LinearLayout adbTools = new LinearLayout(this);
        adbTools.setOrientation(LinearLayout.VERTICAL);
        adbTools.setVisibility(BuildConfig.MIRROR_ONLY ? View.GONE : View.VISIBLE);
        debug.addView(adbTools);
        discoverButton = button(adbTools, "Find Wireless debugging endpoints", this::startDiscovery);
        pairButton = button(adbTools, "Pair with this phone", this::choosePairingEndpoint);
        pairButton.setEnabled(false);
        button(adbTools, "Debug: test local adbd TCP port 5555", this::runLocalTcpProbe);
        button(adbTools, "Probe app launch on the new screen", this::probeAppLaunchPermission);
        sceneButton = button(adbTools, "Hide test scene", this::toggleTestScene);
        launchStatus = text(debug, "App launch probe not run.", 13, MUTED);
        endpoints = new LinearLayout(this);
        endpoints.setOrientation(LinearLayout.VERTICAL);
        debug.addView(endpoints);
        root.addView(debugScroll, new FrameLayout.LayoutParams(-1, -1));
        debugScroll.setVisibility(View.GONE);
        page = (LinearLayout) mainScroll.getChildAt(0);
    }

    private void buildSettingsScreen() {
        settingsScroll = makeSecondaryPage("Settings", "Back to screencast", this::showMainPage);
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
        themed(() -> scroll.setBackgroundColor(appearance.color(0xfff0f4f8)));
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
        themed(() -> drawerPanel.setBackgroundColor(appearance.color(Color.WHITE)));
        drawerPanel.setVisibility(View.GONE);
        TextView brand = text(drawerPanel, "MS912x", 24, INK);
        brand.setTypeface(null, Typeface.BOLD);
        text(drawerPanel, "Desktop display", 14, MUTED);
        drawerSettingsButton = button(drawerPanel, "Settings", () -> { closeDrawer(); showSettingsPage(); });
        drawerDebugButton = button(drawerPanel, "Debug", () -> { closeDrawer(); showDebugPage(); });
        appearanceButton = button(drawerPanel, "Appearance: Auto", this::chooseAppearance);
        appearanceButton.setContentDescription("Appearance mode");
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
        // Show Home without finishing the Activity that owns the running session.
        else if (settingsLocked) startActivity(new Intent(Intent.ACTION_MAIN)
                .addCategory(Intent.CATEGORY_HOME).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        else finish();
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void themed(Runnable binding) {
        appearanceBindings.add(binding);
        binding.run();
    }

    private static ColorStateList states(int enabled, int disabled) {
        return new ColorStateList(new int[][]{new int[]{android.R.attr.state_enabled}, new int[]{}},
                new int[]{enabled, disabled});
    }

    private void styleButton(Button button) {
        themed(() -> {
            button.setBackgroundTintList(states(appearance.color(0xffe4edf7), appearance.color(0xffedf1f5)));
            button.setTextColor(states(appearance.color(ACCENT), appearance.color(MUTED)));
        });
    }

    private void chooseAppearance() {
        new AlertDialog.Builder(this).setTitle("Appearance")
                .setSingleChoiceItems(new String[]{"Auto (system)", "Light", "Dark"},
                        appearance.mode().ordinal(), (dialog, selected) -> {
                            selectAppearance(AppAppearance.Mode.values()[selected]);
                            dialog.dismiss();
                        }).setNegativeButton("Cancel", null).show();
    }

    private void selectAppearance(AppAppearance.Mode mode) {
        appearance.select(mode, getResources().getConfiguration());
        applyAppearance();
    }

    private void applyAppearance() {
        setTheme(appearance.theme());
        for (Runnable binding : appearanceBindings) binding.run();
        appearanceButton.setText("Appearance: " + (appearance.mode() == AppAppearance.Mode.AUTO
                ? "Auto" : appearance.mode() == AppAppearance.Mode.DARK ? "Dark" : "Light"));
        if (endpoints != null) {
            for (int i = 0; i < endpoints.getChildCount(); ++i)
                ((TextView)endpoints.getChildAt(i)).setTextColor(appearance.color(INK));
        }
        boolean light = !appearance.isDark();
        if (Build.VERSION.SDK_INT >= 30) {
            int mask = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                    | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
            getWindow().getInsetsController().setSystemBarsAppearance(light ? mask : 0, mask);
        } else {
            View decor = getWindow().getDecorView();
            int mask = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR;
            if (Build.VERSION.SDK_INT >= 26) mask |= View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
            decor.setSystemUiVisibility((decor.getSystemUiVisibility() & ~mask) | (light ? mask : 0));
        }
    }

    @Override public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        appearance.update(configuration);
        if (root != null) applyAppearance();
    }

    private TextView text(LinearLayout parent, String value, int size, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        themed(() -> view.setTextColor(appearance.color(color)));
        view.setPadding(0, dp(6), 0, dp(6));
        parent.addView(view);
        return view;
    }

    private LinearLayout card(String heading) {
        LinearLayout container = new LinearLayout(this);
        container.setOrientation(LinearLayout.VERTICAL);
        container.setPadding(dp(16), dp(14), dp(16), dp(16));
        GradientDrawable background = new GradientDrawable();
        themed(() -> {
            background.setColor(appearance.color(Color.WHITE));
            background.setStroke(dp(1), appearance.color(0xffdde5ee));
        });
        background.setCornerRadius(dp(16));
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
        styleButton(button);
        button.setOnClickListener(view -> action.run());
        parent.addView(button, new LinearLayout.LayoutParams(-1, -2));
        return button;
    }

    private LinearLayout selectorRow(LinearLayout parent) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setBaselineAligned(false);
        parent.addView(row, new LinearLayout.LayoutParams(-1, -2));
        return row;
    }

    private LinearLayout selectorColumn(LinearLayout row, boolean primary) {
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams params = primary
                ? new LinearLayout.LayoutParams(0, -2, 1)
                : new LinearLayout.LayoutParams(-2, -2);
        if (!primary) params.leftMargin = dp(8);
        row.addView(column, params);
        return column;
    }

    private Spinner compactSpinner(LinearLayout parent, String label, String description) {
        Spinner view = spinner(parent, label);
        view.setContentDescription(description);
        view.getLayoutParams().width = ViewGroup.LayoutParams.WRAP_CONTENT;
        view.setTag(R.layout.spinner_compact_item);
        return view;
    }

    private Spinner spinner(LinearLayout parent, String label) {
        return spinner(parent, label, new Spinner(this, Spinner.MODE_DROPDOWN));
    }

    private Spinner spinner(LinearLayout parent, String label, Spinner spinner) {
        text(parent, label, 12, MUTED);
        spinner.setContentDescription(label);
        themed(() -> {
            spinner.setBackgroundTintList(ColorStateList.valueOf(appearance.color(MUTED)));
            spinner.setPopupBackgroundDrawable(new ColorDrawable(appearance.color(Color.WHITE)));
            if (spinner.getAdapter() instanceof ArrayAdapter)
                ((ArrayAdapter<?>)spinner.getAdapter()).notifyDataSetChanged();
        });
        parent.addView(spinner, new LinearLayout.LayoutParams(-1, dp(64)));
        return spinner;
    }

    private <T> void setItems(Spinner spinner, List<T> options) {
        ArrayAdapter<T> adapter = new ArrayAdapter<>(this, spinner.getTag() instanceof Integer
                ? (Integer)spinner.getTag() : R.layout.spinner_item, options) {
            @Override public View getView(int position, View recycled, ViewGroup parent) {
                TextView view = (TextView)super.getView(position, recycled, parent);
                view.setTextColor(appearance.color(INK));
                return view;
            }
            @Override public View getDropDownView(int position, View recycled, ViewGroup parent) {
                TextView view = (TextView)super.getDropDownView(position, recycled, parent);
                view.setTextColor(appearance.color(INK));
                return view;
            }
        };
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

    private void refreshSourceOptions() {
        if (settingsLocked) return;
        refreshDisplayInventory();
        probeMonitorModes();
    }

    private void probeMonitorModes() {
        if (settingsLocked || usbOutputController == null || usbOutputController.isBusy()) return;
        userMessage = null;
        capabilitiesBeforeProbe = adapterCapabilities;
        modeBeforeProbe = adapterCapabilities == null ? null : selectedMode();
        usbOutputController.probeModes();
    }

    private void applyProbedCapabilities(AdapterCapabilities next) {
        applyCapabilities(next);
        if (next == null) return;
        if (capabilitiesBeforeProbe != null && modeBeforeProbe != null
                && java.util.Objects.equals(capabilitiesBeforeProbe.device, next.device)
                && capabilitiesBeforeProbe.monitorName.equals(next.monitorName)) {
            for (DisplayModeOption mode : next.modes) {
                if (mode.id != modeBeforeProbe.id) continue;
                for (int i = 0; i < modeSelector.getCount(); ++i) {
                    if ((mode.width + "×" + mode.height).equals(modeSelector.getItemAtPosition(i)))
                        modeSelector.setSelection(i);
                }
                rebuildRefreshRates();
                for (int i = 0; i < rateSelector.getCount(); ++i) {
                    if ((mode.refreshHz + " Hz").equals(rateSelector.getItemAtPosition(i)))
                        rateSelector.setSelection(i);
                }
                break;
            }
        }
        capabilitiesBeforeProbe = null;
        modeBeforeProbe = null;
    }

    private void showOutputStatus(String message) {
        outputStatus.setText(message);
        boolean routine = message.startsWith("Screencast") || message.startsWith("Configuring ")
                || message.startsWith("Reading monitor") || "Monitor ready.".equals(message);
        userMessage = routine ? null : message;
        updateControls();
    }

    private void showCaptureStatus(String message) {
        captureStatus.setText(message);
        String lower = message.toLowerCase(Locale.ROOT);
        if (lower.contains("failed") || lower.contains("declined") || lower.contains("cannot")
                || lower.contains("could not") || lower.contains("disconnected")) {
            userMessage = message;
            updateControls();
        }
    }

    private void toggleScreencast() {
        if (settingsLocked) stopSharing();
        else startSession(true);
    }

    private void refreshDisplayInventory() {
        int selectedId = selectedSource() == null ? Display.DEFAULT_DISPLAY : selectedSource().id;
        screens.clear();
        for (Display display : getSystemService(DisplayManager.class).getDisplays()) {
            if (!BuildConfig.MIRROR_ONLY || display.getDisplayId() == Display.DEFAULT_DISPLAY)
                screens.add(ScreenSource.fromDisplay(display));
        }
        if (Build.VERSION.SDK_INT >= 34) {
            screens.add(new ScreenSource(ScreenSource.APP_PICKER,
                    "App window · Android picker", 0, 0));
        }
        if (!BuildConfig.MIRROR_ONLY) screens.add(new ScreenSource(ScreenSource.CREATE_NEW, "Create a new screen", 0, 0));
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
        monitorName.setText(next == null ? "Not connected" : next.monitorName);
        displayModes = next == null ? new ArrayList<>() : new ArrayList<>(next.modes);
        List<String> resolutions = new ArrayList<>();
        for (DisplayModeOption mode : displayModes) {
            String resolution = mode.width + "×" + mode.height;
            if (!resolutions.contains(resolution)) resolutions.add(resolution);
        }
        if (resolutions.isEmpty()) resolutions.add("—");
        setItems(modeSelector, resolutions);
        DisplayModeOption initial = next == null ? null : next.initialMode;
        int preferred = initial == null ? 0 : resolutions.indexOf(initial.width + "×" + initial.height);
        modeSelector.setSelection(Math.max(0, preferred));
        rateResolution = null;
        rebuildRefreshRates();
        updateControls();
    }

    private void rebuildRefreshRates() {
        List<String> rates = new ArrayList<>();
        String resolution = String.valueOf(modeSelector.getSelectedItem());
        String previousRate = resolution.equals(rateResolution)
                ? String.valueOf(rateSelector.getSelectedItem()) : null;
        rateResolution = resolution;
        for (DisplayModeOption mode : displayModes) {
            if ((mode.width + "×" + mode.height).equals(resolution)) {
                String rate = mode.refreshHz + " Hz";
                if (!rates.contains(rate)) rates.add(rate);
            }
        }
        if (rates.isEmpty()) rates.add("—");
        setItems(rateSelector, rates);
        int selectedRate = rates.indexOf(previousRate);
        if (selectedRate < 0 && adapterCapabilities != null) {
            DisplayModeOption preferred = adapterCapabilities.preferredMode;
            if (preferred != null && resolution.equals(preferred.width + "×" + preferred.height)) {
                selectedRate = rates.indexOf(preferred.refreshHz + " Hz");
            }
        }
        rateSelector.setSelection(Math.max(0, selectedRate));
        modeDescription.setText(adapterCapabilities == null
                ? "Only modes confirmed by both the monitor EDID and adapter will be offered."
                : "Monitor refresh does not set capture FPS.");
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
        if (screencastButton == null) return;
        boolean busy = usbOutputController != null && usbOutputController.isBusy();
        ScreenSource source = selectedSource();
        boolean usable = source != null && source.canStart();
        sourceSelector.setEnabled(!settingsLocked);
        modeSelector.setEnabled(!settingsLocked && !busy && adapterCapabilities != null);
        rateSelector.setEnabled(!settingsLocked && !busy && adapterCapabilities != null);
        rotationSelector.setEnabled(!settingsLocked && Build.VERSION.SDK_INT >= 36);
        probeButton.setEnabled(!settingsLocked && !busy);
        if (!busy) stoppingScreencast = false;
        screencastButton.setText(settingsLocked ? "Stop screencast"
                : stoppingScreencast ? "Stopping…" : "Start screencast");
        screencastButton.setEnabled(settingsLocked || (!busy && usable && adapterCapabilities != null));
        screencastStatus.setText(userMessage != null ? userMessage : settingsLocked
                ? (pendingStartOutput ? "Starting screencast…" : "Screencast active")
                : stoppingScreencast ? "Stopping screencast…"
                : busy ? "Reading monitor…"
                : adapterCapabilities == null ? "Choose a screen to detect your monitor."
                : !usable ? "This screen cannot be captured by this build."
                : "Ready to screencast");
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
        userMessage = null;
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
            showCaptureStatus("Cannot request screen capture: " + failure.getMessage());
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
            showCaptureStatus("Screen capture consent was declined.");
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
            if (Build.VERSION.SDK_INT >= 26) startForegroundService(service);
            else startService(service);
            projectionServiceStarted = true;
            showCaptureStatus("Consent granted. Starting screen capture…");
            bindProjectionService();
        } catch (RuntimeException failure) {
            stopSharing();
            showCaptureStatus("Screen capture could not start: " + failure.getMessage());
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
                                showCaptureStatus(String.format(Locale.US,
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
            showCaptureStatus(String.format(Locale.US, "New test screen · Android %.1f Hz · %d°",
                    captureDisplay.getDisplay().getRefreshRate(), sessionRotation * 90));
            startPendingOutput(captureController);
            updateControls();
        } catch (RuntimeException failure) {
            stopSharing();
            showCaptureStatus("New screen failed: " + failure.getMessage());
        }
    }

    private void startPendingOutput(CaptureController capture) {
        if (capture != null) capture.setShowDraws(showDrawsToggle.isChecked());
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
            showCaptureStatus("Test scene failed: " + failure.getMessage());
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
        stoppingScreencast = settingsLocked && usbOutputController != null && usbOutputController.isBusy();
        userMessage = null;
        pendingStartOutput = false;
        if (usbOutputController != null) usbOutputController.stopStreaming();
        stopProjectionCapture();
        stopCaptureTest();
        settingsLocked = false;
        sessionMode = null;
        if (captureStatus != null) showCaptureStatus("Screencast stopped.");
        updateControls();
    }

    private void probeAppLaunchPermission() {
        if (Build.VERSION.SDK_INT < 26 || captureDisplay == null) {
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
        // Keep startup and system-stop callbacks connected while casting in the background.
        if (!settingsLocked) {
            if (projectionService != null) projectionService.setListener(null);
            if (projectionServiceBound) {
                unbindService(projectionConnection);
                projectionServiceBound = false;
                projectionService = null;
            }
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
