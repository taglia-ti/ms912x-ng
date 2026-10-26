package io.github.ms912x.desktop;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.ColorDrawable;
import android.os.Build;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Builds and styles the main, settings and debug pages, and owns navigation between them. */
final class MainScreenView {
    interface Actions {
        void refreshSourceOptions();
        void probeMonitorModes();
        void toggleScreencast();
        void openAppOnNewScreen();
        void openTouchpad();
        void sendTouchpadInput(int type,float x,float y,int button);
        void openTouchpadApps();
        void startPreview();
        void setShowDraws(boolean enabled);
        void clearSavedOptions();
        void startDiscovery();
        void choosePairingEndpoint();
        void runLocalTcpProbe();
        void probeAppLaunchPermission();
        void toggleTestScene();
        void startTestScreen();
        void prepareHelper();
        void chooseHelperEndpoint();
        void disconnectHelper();
        void previewHelper();
        void stopHelper();
    }

    private static final int INK = 0xff14243b;
    private static final int MUTED = 0xff526477;
    private static final int ACCENT = 0xff1264a3;

    private final Activity activity;
    private final AppAppearance appearance;
    private final Actions actions;
    private final List<Runnable> appearanceBindings = new ArrayList<>();
    Button appearanceButton;
    Switch showDrawsToggle;
    private LinearLayout page;
    FrameLayout root;
    ScrollView mainScroll;
    ScrollView settingsScroll;
    ScrollView debugScroll;
    LinearLayout touchpadPage;
    View drawerScrim;
    LinearLayout drawerPanel;
    LinearLayout endpoints;
    TextView status;
    TextView sourceDescription;
    TextView rotationDescription;
    TextView modeDescription;
    TextView captureStatus;
    TextView outputStatus;
    TextView monitorName;
    TextView screencastStatus;
    TextView launchStatus;
    Spinner sourceSelector;
    Spinner modeSelector;
    Spinner rateSelector;
    Spinner rotationSelector;
    Spinner profileSelector,scanSelector,strideSelector,toleranceSelector;
    TextView imageSettingsSummary,imageSettingsStatus;
    LinearLayout mouseSharingCard;
    Spinner mousePositionSelector,mouseDeviceSelector;
    LinearLayout newScreenPlacementRow;
    Spinner newScreenPlacementSelector;
    TextView mouseSharingStatus;
    Button mouseSharingButton,mouseRefreshButton;
    Button discoverButton;
    Button pairButton;
    Button probeButton;
    Button screencastButton;
    Button previewButton;
    Button sceneButton;
    Button menuButton;
    Button touchpadButton;
    Button drawerSettingsButton;
    Button drawerDebugButton;
    Button prepareHelperButton;
    Button connectHelperButton,disconnectHelperButton;
    Button openAppButton;
    Button helperPreviewButton;
    Button stopHelperButton;
    TextView helperStatus;
    Spinner helperRotationSelector;
    TouchpadSurfaceView touchpadSurface;
    TextView touchpadTitle,touchpadStatus;
    private boolean touchpadKeepsScreenOn;

    MainScreenView(Activity activity, AppAppearance appearance, Actions actions) {
        this.activity = activity;
        this.appearance = appearance;
        this.actions = actions;
        activity.getWindow().setStatusBarColor(Color.TRANSPARENT);
        activity.getWindow().setNavigationBarColor(Color.TRANSPARENT);
        if (Build.VERSION.SDK_INT >= 30) activity.getWindow().setDecorFitsSystemWindows(false);
        if (Build.VERSION.SDK_INT >= 29) activity.getWindow().setNavigationBarContrastEnforced(false);

        root = new FrameLayout(activity);
        themed(() -> root.setBackgroundColor(appearance.color(0xfff0f4f8)));
        mainScroll = new ScrollView(activity);
        mainScroll.setFillViewport(true);
        themed(() -> mainScroll.setBackgroundColor(appearance.color(0xfff0f4f8)));
        if (Build.VERSION.SDK_INT >= 30) mainScroll.setOnApplyWindowInsetsListener((view, insets) -> {
            Insets bars = insets.getInsets(WindowInsets.Type.systemBars()
                    | WindowInsets.Type.displayCutout());
            view.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            return insets;
        });
        page = new LinearLayout(activity);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(dp(20), dp(12), dp(20), dp(24));
        mainScroll.addView(page);
        root.addView(mainScroll, new FrameLayout.LayoutParams(-1, -1));
        buildDrawer();

        LinearLayout topBar = new LinearLayout(activity);
        topBar.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = new TextView(activity);
        title.setText(BuildConfig.MIRROR_ONLY ? "MS912x Mirror" : "MS912x Desktop");
        title.setTextSize(25);
        themed(() -> title.setTextColor(appearance.color(INK)));
        title.setPadding(0, dp(6), 0, dp(6));
        title.setTypeface(null, Typeface.BOLD);
        topBar.addView(title, new LinearLayout.LayoutParams(0, -2, 1));
        menuButton = new Button(activity);
        menuButton.setText("⚙");
        menuButton.setTextSize(22);
        menuButton.setContentDescription("Settings and navigation");
        menuButton.setAllCaps(false);
        styleButton(menuButton);
        menuButton.setMinWidth(dp(48));
        menuButton.setOnClickListener(view -> openDrawer());
        touchpadButton=new Button(activity);touchpadButton.setText("Touchpad");touchpadButton.setAllCaps(false);
        touchpadButton.setContentDescription("Open touchpad for the new HDMI screen");styleButton(touchpadButton);
        touchpadButton.setVisibility(View.GONE);touchpadButton.setEnabled(false);touchpadButton.setMinWidth(dp(80));
        touchpadButton.setOnClickListener(view->actions.openTouchpad());
        topBar.addView(touchpadButton,new LinearLayout.LayoutParams(dp(84),dp(52)));
        topBar.addView(menuButton, new LinearLayout.LayoutParams(dp(52), dp(52)));
        page.addView(topBar);
        text(page, "Your phone, on the big screen", 15, MUTED);
        TextView build = text(page, "ANDROID " + Build.VERSION.RELEASE, 11, ACCENT);
        build.setPadding(0, dp(4), 0, dp(16));

        LinearLayout sourceCard = card("01  Screen");
        LinearLayout sourceRow = selectorRow(sourceCard);
        sourceSelector = spinner(selectorColumn(sourceRow, true), "Source screen",
                new RefreshingSpinner(activity, actions::refreshSourceOptions));
        rotationSelector = compactSpinner(selectorColumn(sourceRow, false), "Rotation", "Screen rotation",
                new RefreshingSpinner(activity, actions::probeMonitorModes));
        setItems(rotationSelector, Build.VERSION.SDK_INT >= 36
                ? Arrays.asList("0°", "90°", "180°", "270°")
                : Arrays.asList("Auto"));
        newScreenPlacementRow=new LinearLayout(activity);newScreenPlacementRow.setOrientation(LinearLayout.VERTICAL);
        sourceCard.addView(newScreenPlacementRow);
        newScreenPlacementSelector=spinner(newScreenPlacementRow,"New screen placement",new RefreshingSpinner(activity,actions::probeMonitorModes));
        setItems(newScreenPlacementSelector,Arrays.asList("Virtual","Left","Right","Up","Down"));
        text(newScreenPlacementRow,"Choose a side to share the mouse automatically. Virtual uses Touchpad without mouse sharing.",13,MUTED);
        newScreenPlacementRow.setVisibility(View.GONE);
        LinearLayout modeCard = card("02  Monitor output");
        monitorName = text(modeCard, "Not connected", 17, INK);
        LinearLayout modeRow = selectorRow(modeCard);
        modeSelector = spinner(selectorColumn(modeRow, true), "Resolution",
                new RefreshingSpinner(activity, actions::probeMonitorModes));
        modeSelector.setContentDescription("Resolution · EDID ∩ adapter modes");
        rateSelector = compactSpinner(selectorColumn(modeRow, false), "Refresh", "Monitor refresh rate",
                new RefreshingSpinner(activity, actions::probeMonitorModes));

        LinearLayout actionCard = card("03  Screencast");
        screencastButton = button(actionCard, "Start screencast", actions::toggleScreencast);
        themed(() -> {
            screencastButton.setBackgroundTintList(states(appearance.color(ACCENT), appearance.color(0xffdde5ee)));
            screencastButton.setTextColor(states(appearance.isDark() ? 0xff082138 : Color.WHITE, appearance.color(MUTED)));
        });
        openAppButton=button(actionCard,"Open app on new screen",actions::openAppOnNewScreen);
        openAppButton.setVisibility(View.GONE);
        screencastStatus = text(actionCard, "Choose a screen to detect your monitor.", 14, MUTED);

        buildSettingsScreen();
        buildDebugScreen();
        buildTouchpadPage();

    }

    boolean navigateBack() {
        if (drawerPanel.getVisibility() == View.VISIBLE) closeDrawer();
        else if (settingsScroll.getVisibility() == View.VISIBLE) showMainPage();
        else if (debugScroll.getVisibility() == View.VISIBLE) showMainPage();
        else if(touchpadPage.getVisibility()==View.VISIBLE)showMainPage();
        else return false;
        return true;
    }

    void configurationChanged(Configuration configuration) {
        appearance.update(configuration);
        applyAppearance();
    }

    private void buildDebugScreen() {
        debugScroll = makeSecondaryPage("Debug tools", "Back to screencast", this::showMainPage);
        LinearLayout debug = (LinearLayout) debugScroll.getChildAt(0);
        page = debug;
        showDrawsToggle = new Switch(activity);
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
        showDrawsToggle.setOnCheckedChangeListener((button, checked) -> actions.setShowDraws(checked));
        text(debug, "Outline submitted damage rectangles on the monitor.", 13, MUTED);
        probeButton = button(debug, "Read monitor modes", actions::probeMonitorModes);
        outputStatus = text(debug, "Monitor not read yet.", 14, MUTED);
        sourceDescription = text(debug, "", 14, MUTED);
        rotationDescription = text(debug, Build.VERSION.SDK_INT >= 36
                ? "Rotates this capture/new screen using Android's virtual-display control."
                : "Manual virtual-screen rotation requires Android 16+. "
                    + "This phone follows Android's capture orientation.", 13, MUTED);
        modeDescription = text(debug, "", 13, MUTED);
        captureStatus = text(debug, "No capture session.", 14, INK);
        button(debug,"Clear saved screen and output options",actions::clearSavedOptions);
        text(debug,"This clears saved screen/output and mouse-sharing choices, image profile, scan on/off, stride and tolerance. Oversize HDMI modes are discarded during each probe and are not cached. ADB pairing credentials are kept.",13,MUTED);
        previewButton = button(debug, "Test capture without USB output",
                actions::startPreview);
        button(debug, "Preview app-owned test screen", actions::startTestScreen);
        text(debug, "Without monitor EDID, capture-only tests use 640×480. "
                + "Monitor refresh is independent of capture rate.",
                13, MUTED);
        status = text(debug, BuildConfig.MIRROR_ONLY ? "Screen mirroring build" : NativeAdbCall.backendVersionOrUnavailable()
                + "\nWireless ADB can start the display helper on this phone. Enable Wireless debugging and pair once.",
                13, MUTED);
        LinearLayout adbTools = new LinearLayout(activity);
        adbTools.setOrientation(LinearLayout.VERTICAL);
        adbTools.setVisibility(BuildConfig.MIRROR_ONLY ? View.GONE : View.VISIBLE);
        debug.addView(adbTools);
        discoverButton = button(adbTools, "Find Wireless debugging endpoints",
                actions::startDiscovery);
        pairButton = button(adbTools, "Pair with this phone",
                actions::choosePairingEndpoint);
        pairButton.setEnabled(false);
        button(adbTools, "Debug: test local adbd TCP port 5555",
                actions::runLocalTcpProbe);
        button(adbTools, "Probe app launch on the new screen", actions::probeAppLaunchPermission);
        sceneButton = button(adbTools, "Hide test scene", actions::toggleTestScene);
        connectHelperButton=button(adbTools,"Connect ADB helper on this phone",actions::chooseHelperEndpoint);
        disconnectHelperButton=button(adbTools,"Disconnect ADB helper",actions::disconnectHelper);
        prepareHelperButton = button(adbTools, "Prepare ADB helper", actions::prepareHelper);
        helperRotationSelector = spinner(adbTools, "Helper output rotation", new Spinner(activity));
        setItems(helperRotationSelector, Arrays.asList("0°", "90°", "180°", "270°"));
        helperPreviewButton = button(adbTools, "Preview selected display with ADB helper", actions::previewHelper);
        stopHelperButton = button(adbTools, "Stop helper preview", actions::stopHelper);
        helperStatus = text(adbTools,
                "Pair with this phone, find its Wireless debugging endpoint, then connect the helper.", 13, MUTED);
        launchStatus = text(debug, "App launch probe not run.", 13, MUTED);
        endpoints = new LinearLayout(activity);
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
        profileSelector=spinner(profile,"Image profile",new Spinner(activity));
        setItems(profileSelector,Arrays.asList(ImageOptions.PROFILES));
        text(profile,"Profiles set stride and tolerance defaults. Scan on/off is independent of the profile.",14,MUTED);
        LinearLayout output = card("Image matching");
        scanSelector=spinner(output,"Damage scanning",new Spinner(activity));
        setItems(scanSelector,Arrays.asList("On","Off"));
        text(output,"On sends changed regions. Off skips comparison and sends each captured frame in full. Stride and tolerance apply when scanning is On.",13,MUTED);
        strideSelector=spinner(output,"Scan stride",new Spinner(activity));
        List<String> strides=new ArrayList<>();strides.add("Profile default");strides.add("Every row (1)");
        for(int i=2;i<=16;i++)strides.add("Every "+i+" rows");setItems(strideSelector,strides);
        text(output,"Higher strides reduce scan work but can delay or miss small or brief changes. Use 1 for precise updates.",13,MUTED);
        toleranceSelector=spinner(output,"Color tolerance",new Spinner(activity));
        setItems(toleranceSelector,Arrays.asList("Profile default","0 · Exact","1","3","7","15","31"));
        text(output,"Higher tolerance ignores more low-order color bits and reduces USB updates. Zero compares every UYVY byte exactly.",13,MUTED);
        imageSettingsSummary=text(output,"",14,INK);
        imageSettingsStatus=text(output,"Saved automatically. Changes apply immediately, including during a screencast.",13,MUTED);
        mouseSharingCard=card("Mouse sharing · Experimental");
        text(mouseSharingCard,"Move a mouse between your main desktop monitor and this screen. Position and mouse choices are saved. Using Touchpad stops sharing.",14,MUTED);
        mousePositionSelector=spinner(mouseSharingCard,"Adapter position",new Spinner(activity));
        setItems(mousePositionSelector,Arrays.asList("Left","Right","Above","Below"));
        mouseDeviceSelector=spinner(mouseSharingCard,"Mouse",new Spinner(activity));
        setItems(mouseDeviceSelector,Arrays.asList("No mouse detected"));
        mouseSharingButton=button(mouseSharingCard,"Start mouse sharing",()->{});
        mouseRefreshButton=button(mouseSharingCard,"Refresh mice",()->{});
        mouseSharingStatus=text(mouseSharingCard,"Create an independent screen to share a mouse.",13,MUTED);
        mouseSharingCard.setVisibility(View.GONE);
        root.addView(settingsScroll, new FrameLayout.LayoutParams(-1, -1));
        settingsScroll.setVisibility(View.GONE);
        page = (LinearLayout) mainScroll.getChildAt(0);
    }

    private void buildTouchpadPage(){
        touchpadPage=new LinearLayout(activity);touchpadPage.setOrientation(LinearLayout.VERTICAL);
        touchpadPage.setPadding(dp(16),dp(12),dp(16),dp(12));
        themed(()->touchpadPage.setBackgroundColor(appearance.color(0xfff0f4f8)));
        if(Build.VERSION.SDK_INT>=30)touchpadPage.setOnApplyWindowInsetsListener((view,insets)->{
            Insets bars=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());
            view.setPadding(dp(16)+bars.left,bars.top+dp(8),dp(16)+bars.right,dp(8)+bars.bottom);return insets;
        });
        LinearLayout heading=new LinearLayout(activity);heading.setGravity(Gravity.CENTER_VERTICAL);
        Button back=button(heading,"‹  Screencast",this::showMainPage);back.setContentDescription("Back to screencast controls");
        back.setLayoutParams(new LinearLayout.LayoutParams(dp(104),dp(48)));
        touchpadTitle=new TextView(activity);touchpadTitle.setText("Screen");touchpadTitle.setTextSize(20);touchpadTitle.setGravity(Gravity.END|Gravity.CENTER_VERTICAL);
        themed(()->touchpadTitle.setTextColor(appearance.color(INK)));
        heading.addView(touchpadTitle,new LinearLayout.LayoutParams(0,dp(48),1));touchpadPage.addView(heading);
        touchpadStatus=text(touchpadPage,"Move · tap to click · hold + move to drag · two fingers scroll",13,MUTED);
        touchpadStatus.setGravity(Gravity.CENTER);
        touchpadSurface=new TouchpadSurfaceView(activity,appearance,actions::sendTouchpadInput);
        themed(()->touchpadSurface.applyAppearance(appearance));
        LinearLayout.LayoutParams padParams=new LinearLayout.LayoutParams(-1,0,1);padParams.topMargin=dp(8);padParams.bottomMargin=dp(8);
        touchpadPage.addView(touchpadSurface,padParams);
        LinearLayout clicks=new LinearLayout(activity);clicks.setOrientation(LinearLayout.HORIZONTAL);
        Button left=button(clicks,"Left click",()->actions.sendTouchpadInput(TouchpadInput.CLICK,0,0,1));
        left.setLayoutParams(new LinearLayout.LayoutParams(0,dp(48),1));
        Button right=button(clicks,"Right click",()->actions.sendTouchpadInput(TouchpadInput.CLICK,0,0,2));
        LinearLayout.LayoutParams rightParams=new LinearLayout.LayoutParams(0,dp(48),1);rightParams.leftMargin=dp(8);right.setLayoutParams(rightParams);
        touchpadPage.addView(clicks);
        LinearLayout shortcuts=new LinearLayout(activity);shortcuts.setOrientation(LinearLayout.HORIZONTAL);
        Button apps=button(shortcuts,"Apps",actions::openTouchpadApps);
        apps.setLayoutParams(new LinearLayout.LayoutParams(0,dp(52),1));
        Button home=button(shortcuts,"Home",()->actions.sendTouchpadInput(TouchpadInput.HOME,0,0,0));
        LinearLayout.LayoutParams homeParams=new LinearLayout.LayoutParams(0,dp(52),1);homeParams.leftMargin=dp(8);home.setLayoutParams(homeParams);
        Button remoteBack=button(shortcuts,"Back",()->actions.sendTouchpadInput(TouchpadInput.BACK,0,0,0));
        LinearLayout.LayoutParams backParams=new LinearLayout.LayoutParams(0,dp(52),1);backParams.leftMargin=dp(8);remoteBack.setLayoutParams(backParams);
        touchpadPage.addView(shortcuts);
        root.addView(touchpadPage,new FrameLayout.LayoutParams(-1,-1));touchpadPage.setVisibility(View.GONE);
        page=(LinearLayout)mainScroll.getChildAt(0);
    }

    void setTouchpadEntry(boolean visible,boolean enabled,int displayId){
        touchpadButton.setVisibility(visible?View.VISIBLE:View.GONE);touchpadButton.setEnabled(enabled);
        touchpadButton.setContentDescription(enabled?"Open touchpad for HDMI screen "+displayId:"Touchpad unavailable for this display");
        if(touchpadPage.getVisibility()==View.VISIBLE&&!enabled)showMainPage();
    }
    void showTouchpad(int displayId,int width,int height){
        if(!touchpadButton.isEnabled()||displayId<0)return;
        touchpadTitle.setText("HDMI screen  #"+displayId);touchpadSurface.setOutputSize(width,height);
        if(!touchpadKeepsScreenOn){activity.getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);touchpadKeepsScreenOn=true;}
        showPage(touchpadPage);touchpadSurface.requestFocus();
    }
    void cancelTouchpadGesture(){if(touchpadSurface!=null)touchpadSurface.cancelGesture();}
    boolean touchpadVisible(){return touchpadPage!=null&&touchpadPage.getVisibility()==View.VISIBLE;}

    private ScrollView makeSecondaryPage(String title, String backLabel, Runnable back) {
        ScrollView scroll = new ScrollView(activity);
        scroll.setFillViewport(true);
        themed(() -> scroll.setBackgroundColor(appearance.color(0xfff0f4f8)));
        LinearLayout content = new LinearLayout(activity);
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
        drawerScrim = new View(activity);
        drawerScrim.setBackgroundColor(0x66000000);
        drawerScrim.setVisibility(View.GONE);
        drawerScrim.setOnClickListener(view -> closeDrawer());
        root.addView(drawerScrim, new FrameLayout.LayoutParams(-1, -1));
        drawerPanel = new LinearLayout(activity);
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
        if(touchpadPage!=null&&touchpadPage.getVisibility()==View.VISIBLE&&pageView!=touchpadPage)cancelTouchpadGesture();
        if(pageView!=touchpadPage&&touchpadKeepsScreenOn){activity.getWindow().clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);touchpadKeepsScreenOn=false;}
        mainScroll.setVisibility(pageView == mainScroll ? View.VISIBLE : View.GONE);
        settingsScroll.setVisibility(pageView == settingsScroll ? View.VISIBLE : View.GONE);
        debugScroll.setVisibility(pageView == debugScroll ? View.VISIBLE : View.GONE);
        if(touchpadPage!=null)touchpadPage.setVisibility(pageView==touchpadPage?View.VISIBLE:View.GONE);
    }

    private int dp(int value) {
        return Math.round(value * activity.getResources().getDisplayMetrics().density);
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
        new AlertDialog.Builder(activity).setTitle("Appearance")
                .setSingleChoiceItems(new String[]{"Auto (system)", "Light", "Dark"},
                        appearance.mode().ordinal(), (dialog, selected) -> {
                            selectAppearance(AppAppearance.Mode.values()[selected]);
                            dialog.dismiss();
                        }).setNegativeButton("Cancel", null).show();
    }

    private void selectAppearance(AppAppearance.Mode mode) {
        appearance.select(mode, activity.getResources().getConfiguration());
        applyAppearance();
    }

    void applyAppearance() {
        activity.setTheme(appearance.theme());
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
            activity.getWindow().getInsetsController().setSystemBarsAppearance(light ? mask : 0, mask);
        } else {
            View decor = activity.getWindow().getDecorView();
            int mask = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR;
            if (Build.VERSION.SDK_INT >= 26) mask |= View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
            decor.setSystemUiVisibility((decor.getSystemUiVisibility() & ~mask) | (light ? mask : 0));
        }
    }

    private TextView text(LinearLayout parent, String value, int size, int color) {
        TextView view = new TextView(activity);
        view.setText(value);
        view.setTextSize(size);
        themed(() -> view.setTextColor(appearance.color(color)));
        view.setPadding(0, dp(6), 0, dp(6));
        parent.addView(view);
        return view;
    }

    private LinearLayout card(String heading) {
        LinearLayout container = new LinearLayout(activity);
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
        Button button = new Button(activity);
        button.setText(label);
        button.setAllCaps(false);
        button.setMinHeight(dp(48));
        styleButton(button);
        button.setOnClickListener(view -> action.run());
        parent.addView(button, new LinearLayout.LayoutParams(-1, -2));
        return button;
    }

    private LinearLayout selectorRow(LinearLayout parent) {
        LinearLayout row = new LinearLayout(activity);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setBaselineAligned(false);
        parent.addView(row, new LinearLayout.LayoutParams(-1, -2));
        return row;
    }

    private LinearLayout selectorColumn(LinearLayout row, boolean primary) {
        LinearLayout column = new LinearLayout(activity);
        column.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams params = primary
                ? new LinearLayout.LayoutParams(0, -2, 1)
                : new LinearLayout.LayoutParams(-2, -2);
        if (!primary) params.leftMargin = dp(8);
        row.addView(column, params);
        return column;
    }

    private Spinner compactSpinner(LinearLayout parent, String label, String description,
                                   Spinner spinner) {
        Spinner view = spinner(parent, label, spinner);
        view.setContentDescription(description);
        view.getLayoutParams().width = ViewGroup.LayoutParams.WRAP_CONTENT;
        view.setTag(R.layout.spinner_compact_item);
        return view;
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

    <T> void setItems(Spinner spinner, List<T> options) {
        ArrayAdapter<T> adapter = new ArrayAdapter<>(activity, spinner.getTag() instanceof Integer
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
}
