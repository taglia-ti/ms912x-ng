package io.github.ms912x.desktop;

import android.app.*;
import android.content.*;
import android.hardware.display.*;
import android.hardware.usb.UsbManager;
import android.os.*;
import android.view.View;
import android.view.ViewGroup;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.*;
import java.lang.reflect.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicReference;
import static io.github.ms912x.desktop.AndroidFixtures.*;
import static io.github.ms912x.desktop.OutputFixtureTests.main;

final class ActivityFixtureTests {
    static MainScreenView screen(Activity activity) throws Exception {
        return (MainScreenView)field(activity, "screen");
    }
    static DisplaySelectionController selection(Activity activity) throws Exception {
        return (DisplaySelectionController)field(activity, "selection");
    }
    static void awaitService(Instrumentation test,Activity activity) throws Exception {
        CaptureFixtureTests.waitFor(()->{
            try{return main(test,()->screencast(activity).coordinator()!=null);}
            catch(Exception failure){throw new AssertionError(failure);}
        },"Session service binding");
    }
    static ScreencastController screencast(Activity activity) throws Exception {
        return (ScreencastController)field(activity, "screencast");
    }
    static Object field(Object target, String name) throws Exception {
        if(target instanceof ScreencastController){
            SessionCoordinator owner=((ScreencastController)target).coordinator();
            CaptureSource source=owner==null?null:owner.source();
            switch(name){
                case "usbOutputController":return owner==null?null:owner.usb();
                case "captureController":return source==null?null:source.capture();
                case "captureDisplay":return source==null?null:source.ownedDisplay();
                case "presentation":return source==null?null:source.presentation();
                case "settingsLocked":return owner!=null&&owner.active();
                case "acceptedFrames":return ((OwnedCaptureSource)source).accepted;
            }
        }
        Field f = target.getClass().getDeclaredField(name); f.setAccessible(true); return f.get(target);
    }
    static void field(Object target, String name, Object value) throws Exception {
        Field f = target.getClass().getDeclaredField(name); f.setAccessible(true); f.set(target, value);
    }
    static Object call(Object target, String name, Object... args) throws Exception {
        for (Method method : target.getClass().getDeclaredMethods()) {
            if (method.getName().equals(name)) { method.setAccessible(true); return method.invoke(target, args); }
        }
        throw new AssertionError("Missing method " + name);
    }
    static void select(MainActivity activity, int id) throws Exception {
        Spinner sources = (Spinner)field(screen(activity), "sourceSelector");
        for (int i = 0; i < sources.getCount(); ++i) {
            if (((ScreenSource)sources.getItemAtPosition(i)).id == id) {
                sources.setSelection(i); call(selection(activity), "updateSourceDescription"); call(screencast(activity), "updateControls"); return;
            }
        }
        throw new AssertionError("Missing source " + id);
    }
    static DisplayModeOption mode1080p60() {
        for (DisplayModeOption mode : NativeAdapter.modeCatalog())
            if (mode.width == 1920 && mode.height == 1080 && mode.refreshHz == 60) return mode;
        throw new AssertionError("Missing shared 1080p60 mode");
    }

    private static void selectors(MainActivity activity) throws Exception {
        selection(activity).clearSavedOptions();
        AppAppearance appearance = (AppAppearance)field(screen(activity), "appearance");
        AppAppearance.Mode originalMode = appearance.mode();
        android.content.res.Configuration day = new android.content.res.Configuration(activity.getResources().getConfiguration());
        day.uiMode = (day.uiMode & ~android.content.res.Configuration.UI_MODE_NIGHT_MASK)
                | android.content.res.Configuration.UI_MODE_NIGHT_NO;
        android.content.res.Configuration night = new android.content.res.Configuration(day);
        night.uiMode = (night.uiMode & ~android.content.res.Configuration.UI_MODE_NIGHT_MASK)
                | android.content.res.Configuration.UI_MODE_NIGHT_YES;
        try {
            call(screen(activity), "selectAppearance", AppAppearance.Mode.AUTO);
            activity.onConfigurationChanged(night);
            require(appearance.isDark(), "Auto follows system dark appearance");
            activity.onConfigurationChanged(day);
            require(!appearance.isDark(), "Auto follows system light appearance");
            call(screen(activity), "selectAppearance", AppAppearance.Mode.DARK);
            activity.onConfigurationChanged(day);
            require(appearance.isDark() && new AppAppearance(activity).mode() == AppAppearance.Mode.DARK,
                    "Explicit dark appearance persists and overrides system light");
            require(((android.graphics.drawable.ColorDrawable)((View)field(screen(activity), "root")).getBackground()).getColor()
                    == appearance.color(0xfff0f4f8), "Dark palette reaches page background");
            call(screen(activity), "selectAppearance", AppAppearance.Mode.LIGHT);
            activity.onConfigurationChanged(night);
            require(!appearance.isDark(), "Explicit light appearance overrides system dark");
        } finally { call(screen(activity), "selectAppearance", originalMode); }
        Spinner sources = (Spinner)field(screen(activity), "sourceSelector");
        Spinner rotation = (Spinner)field(screen(activity), "rotationSelector");
        Spinner resolutions = (Spinner)field(screen(activity), "modeSelector");
        Spinner rates = (Spinner)field(screen(activity), "rateSelector");
        View mainPage = (View)field(screen(activity), "mainScroll");
        View debugPage = (View)field(screen(activity), "debugScroll");
        for (String name : new String[]{"probeButton", "sourceDescription", "rotationDescription",
                "modeDescription", "captureStatus", "outputStatus"}) {
            require(isDescendant((View)field(screen(activity), name), debugPage), name + " belongs to Debug");
        }
        require(sources instanceof RefreshingSpinner
                        && rotation instanceof RefreshingSpinner
                        && resolutions instanceof RefreshingSpinner
                        && rates instanceof RefreshingSpinner,
                "Every main-screen dropdown refreshes USB and display state before opening");
        require(hasText(mainPage, "Start screencast") && !hasText(mainPage, "Stop screencast"),
                "Idle main page has one screencast action");
        require(!hasText(mainPage, "Read monitor modes"), "Manual EDID control is absent from main page");
        require(isDescendant((View)field(screen(activity), "appearanceButton"), (View)field(screen(activity), "drawerPanel")),
                "Appearance mode button belongs to side menu");
        require(isDescendant((View)field(screen(activity), "showDrawsToggle"), debugPage)
                && !((Switch)field(screen(activity), "showDrawsToggle")).isChecked(),
                "Damage-drawing toggle belongs to Debug and defaults off");
        require(sources.getParent().getParent() == rotation.getParent().getParent(),
                "Rotation is beside the screen selector");
        require(resolutions.getParent().getParent() == rates.getParent().getParent(),
                "Refresh is beside the resolution selector");
        require(rotation.getLayoutParams().width == ViewGroup.LayoutParams.WRAP_CONTENT
                && rates.getLayoutParams().width == ViewGroup.LayoutParams.WRAP_CONTENT,
                "Secondary selectors only request their content width");
        List<DisplayModeOption> catalog = NativeAdapter.modeCatalog();
        // Reversed input proves the UI does not depend on catalog insertion order.
        List<DisplayModeOption> reversed = new ArrayList<>(catalog);
        Collections.reverse(reversed);
        call(selection(activity), "applyCapabilities", new AdapterCapabilities(null, "No EDID hint", reversed));
        DisplayModeOption selected = (DisplayModeOption)call(selection(activity), "selectedMode");
        require(selected.width == 1920 && selected.height == 1200 && selected.refreshHz == 60,
                "Missing hint defaults to highest supported timing");
        long previousPixels = Long.MAX_VALUE;
        for (int i = 0; i < resolutions.getCount(); ++i) {
            String[] size = resolutions.getItemAtPosition(i).toString()
                    .split(" ", 2)[0].split("×");
            long pixels = Long.parseLong(size[0]) * Long.parseLong(size[1]);
            require(pixels <= previousPixels, "Resolution menu is descending");
            previousPixels = pixels;
            resolutions.setSelection(i); call(selection(activity), "rebuildRefreshRates");
            int previousHz = Integer.MAX_VALUE;
            for (int r = 0; r < rates.getCount(); ++r) {
                int hz = Integer.parseInt(rates.getItemAtPosition(r).toString().split(" ")[0]);
                require(hz <= previousHz, "Refresh menu is descending"); previousHz = hz;
            }
            require(rates.getSelectedItemPosition() == 0, "New resolution defaults to highest rate without hint");
        }
        DisplayModeOption preferred = catalog.stream().filter(m -> m.width == 1920
                && m.height == 1080 && m.refreshHz == 50).findFirst().get();
        call(selection(activity), "applyCapabilities", new AdapterCapabilities(null, "Preferred 1080p50", catalog, edid(preferred)));
        require("Preferred 1080p50".contentEquals(((TextView)field(screen(activity), "monitorName")).getText()),
                "Monitor output shows the monitor name");
        require(((DisplayModeOption)call(selection(activity), "selectedMode")).id == preferred.id,
                "EDID chooses preferred resolution and refresh instead of highest");
        require("60 Hz".equals(rates.getItemAtPosition(0)), "Preferred rate does not reorder the menu");
        rates.setSelection(0); call(selection(activity), "rebuildRefreshRates");
        require(((DisplayModeOption)call(selection(activity), "selectedMode")).refreshHz == 60,
                "Rebuilding preserves a manual rate choice");
        call(selection(activity), "applyCapabilities", new AdapterCapabilities(null, "Preferred 1080p50", catalog, edid(preferred)));
    }

    static void mirror(Instrumentation test) throws Exception {
        MainActivity activity = (MainActivity)test.startActivitySync(new Intent(test.getTargetContext(), MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        awaitService(test,activity);
        try {
            main(test, () -> { selectors(activity); return null; });
            test.waitForIdleSync();
            main(test, () -> {
                require(((DisplayModeOption)call(selection(activity), "selectedMode")).refreshHz == 50,
                        "Deferred selector callbacks preserve EDID's preferred rate");
                require(field(activity, "adbCredentialStore") == null, "Mirror build does not initialize ADB");
                Spinner sources = (Spinner)field(screen(activity), "sourceSelector");
                require(sources.getCount() > 0, "Mirror source inventory is populated");
                for (int i = 0; i < sources.getCount(); ++i) {
                    int id = ((ScreenSource)sources.getItemAtPosition(i)).id;
                    require(id == 0 || id == ScreenSource.APP_PICKER, "Mirror build exposes only consented capture sources");
                }
                require(((TextView)field(screen(activity), "status")).getText().toString().contains("Screen mirroring"),
                        "Mirror startup avoids Rust native loading");
                return null;
            });
        } finally { main(test, () -> { activity.finish(); return null; }); }
    }

    static void run(Instrumentation test) throws Exception {
        if(Build.VERSION.SDK_INT>=33)test.getUiAutomation().grantRuntimePermission(test.getTargetContext().getPackageName(),android.Manifest.permission.POST_NOTIFICATIONS);
        MainActivity activity = (MainActivity)test.startActivitySync(new Intent(test.getTargetContext(), MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        Host host = new Host(new Connection(edid(mode1080p60()))); host.devices.add(adapter());
        awaitService(test,activity);
        SessionCoordinator fixture=main(test,()->{
            ScreencastController facade=screencast(activity);
            facade.close();field(facade,"closed",false);
            SessionCoordinator owner=new SessionCoordinator(activity,new SessionCoordinator.ForegroundOwner(){
                public void begin(){} public void end(){}
            },host,ShellCaptureFactory.createBackend(activity),null);
            call(facade,"attach",owner);return owner;
        });
        UsbOutputController output=fixture.usb();
        try {
            main(test, () -> { selectors(activity); return null; });
            test.waitForIdleSync();
            main(test, () -> {
                require(((DisplayModeOption)call(selection(activity), "selectedMode")).refreshHz == 50,
                        "Deferred selector callbacks preserve EDID's preferred rate");
                call(selection(activity), "applyCapabilities", (Object)null);
                View mainPage = (View)field(screen(activity), "mainScroll");
                View settingsPage = (View)field(screen(activity), "settingsScroll");
                View debugPage = (View)field(screen(activity), "debugScroll");
                Button menu = (Button)field(screen(activity), "menuButton");
                require(mainPage.getVisibility() == View.VISIBLE
                        && settingsPage.getVisibility() == View.GONE
                        && debugPage.getVisibility() == View.GONE,
                        "Sharing is the initial screen");
                require(menu.performClick()
                        && ((View)field(screen(activity), "drawerPanel")).getVisibility() == View.VISIBLE,
                        "Settings button opens the navigation drawer");
                require(((Button)field(screen(activity), "drawerSettingsButton")).performClick()
                        && settingsPage.getVisibility() == View.VISIBLE
                        && mainPage.getVisibility() == View.GONE,
                        "Drawer opens Settings page");
                require(hasText(settingsPage, "Profile")
                        && hasText(settingsPage, "Stride and color tolerance controls are planned here."),
                        "Settings placeholders are present");
                call(activity, "onBackPressed");
                require(mainPage.getVisibility() == View.VISIBLE, "Settings returns to sharing screen");
                require(menu.performClick()
                        && ((Button)field(screen(activity), "drawerDebugButton")).performClick()
                        && debugPage.getVisibility() == View.VISIBLE
                        && mainPage.getVisibility() == View.GONE,
                        "Drawer opens Debug page");
                require(isDescendant((View)field(screen(activity), "sceneButton"), debugPage)
                        && isDescendant((View)field(screen(activity), "discoverButton"), debugPage)
                        && isDescendant((View)field(screen(activity), "previewButton"), debugPage),
                        "Capture, scene and ADB test controls are on Debug page");
                call(activity, "onBackPressed");
                require(!((Button)field(screen(activity), "screencastButton")).isEnabled(), "Sharing disabled without EDID");
                require(!((Button)field(screen(activity), "sceneButton")).isEnabled(), "Scene control requires own display");
                call(screencast(activity), "probeAppLaunchPermission");
                require(((TextView)field(screen(activity), "launchStatus")).getText().toString().contains("Create a new"), "Launch probe requires display");
                call(field(activity, "adbToolsController"), "choosePairingEndpoint");
                require(((TextView)field(screen(activity), "status")).getText().toString().contains("No matching"), "No pairing endpoints");
                call(selection(activity), "applyCapabilities", new AdapterCapabilities(adapter(), "All fixture timings", NativeAdapter.modeCatalog()));
                Spinner resolutions = (Spinner)field(screen(activity), "modeSelector");
                Spinner rates = (Spinner)field(screen(activity), "rateSelector");
                for (DisplayModeOption mode : NativeAdapter.modeCatalog()) {
                    for (int i = 0; i < resolutions.getCount(); ++i)
                        if (resolutions.getItemAtPosition(i).equals(mode.resolutionLabel())) resolutions.setSelection(i);
                    call(selection(activity), "rebuildRefreshRates");
                    for (int i = 0; i < rates.getCount(); ++i)
                        if (rates.getItemAtPosition(i).equals(mode.refreshHz + " Hz")) rates.setSelection(i);
                    require(((DisplayModeOption)call(selection(activity), "selectedMode")).id == mode.id, "UI selects each catalog timing");
                }
                require(((Button)field(screen(activity), "screencastButton")).isEnabled(), "EDID enables sharing");
                if (Build.VERSION.SDK_INT >= 34) {
                    select(activity, ScreenSource.APP_PICKER);
                    require(((TextView)field(screen(activity), "sourceDescription")).getText().toString().contains("app window"), "App-picker source description");
                }
                call(selection(activity), "applyCapabilities", (Object)null);
                require(((DisplayModeOption)call(selection(activity), "selectedMode")).id == 0x40, "No-EDID diagnostic fallback");
                return null;
            });
            byte[] probeEdid = Arrays.copyOf(edid(mode1080p60()), 256);
            probeEdid[126] = 1;
            probeEdid[128] = 2; probeEdid[129] = 3; probeEdid[130] = 7;
            probeEdid[132] = 0x42; probeEdid[133] = 16; probeEdid[134] = 31;
            checksum(probeEdid, 0); checksum(probeEdid, 1);
            host.connection.edid = probeEdid;
            probeFromDropdown(test, activity, output, host);
            main(test, () -> {
                require("Fixture LCD".contentEquals(((TextView)field(screen(activity), "monitorName")).getText()),
                        "Opening screen dropdown reads EDID and shows monitor name");
                Spinner rates = (Spinner)field(screen(activity), "rateSelector");
                require(rates.getCount() == 2, "EDID probe offers both fixture refresh rates");
                rates.setSelection(1);
                return null;
            });
            probeFromDropdown(test, activity, output, host);
            probeFromAnyDropdown(test, activity, output, host,
                    (Spinner)field(screen(activity), "modeSelector"), "resolution dropdown", true);
            probeFromAnyDropdown(test, activity, output, host,
                    (Spinner)field(screen(activity), "rateSelector"), "refresh-rate dropdown", false);
            if (Build.VERSION.SDK_INT >= 36) {
                probeFromAnyDropdown(test, activity, output, host,
                        (Spinner)field(screen(activity), "rotationSelector"), "rotation dropdown", false);
            }
            main(test, () -> {
                require(((DisplayModeOption)call(selection(activity), "selectedMode")).refreshHz == 50,
                        "Dropdown EDID refresh preserves a valid manual timing over preferred 60 Hz");
                return null;
            });
            try (CaptureController secondaryCapture = new CaptureController(640, 480, 1, null)) {
                VirtualDisplay secondary = activity.getSystemService(DisplayManager.class).createVirtualDisplay(
                        "fixture-secondary",640,480,160,secondaryCapture.surface(),DisplayManager.VIRTUAL_DISPLAY_FLAG_OWN_CONTENT_ONLY);
                try {
                    main(test, () -> {
                        call(selection(activity), "refreshDisplayInventory"); select(activity, secondary.getDisplay().getDisplayId());
                        require(!((Button)field(screen(activity), "previewButton")).isEnabled(), "Secondary display capture disabled");
                        require(((TextView)field(screen(activity), "sourceDescription")).getText().toString().contains("ADB setup"), "Secondary source limitation");
                        return null;
                    });
                } finally { secondary.release(); }
            }
            main(test, () -> { call(selection(activity), "refreshDisplayInventory"); output.probeModes(); return null; });
            CaptureFixtureTests.waitFor(() -> !output.isBusy(), "Activity EDID probe exits"); test.waitForIdleSync();
            main(test, () -> {
                call(selection(activity), "applyCapabilities", new AdapterCapabilities(
                        adapter(), "All fixture timings", NativeAdapter.modeCatalog()));
                Spinner resolutions = (Spinner)field(screen(activity), "modeSelector");
                Spinner rates = (Spinner)field(screen(activity), "rateSelector");
                boolean selected1080 = false;
                for (int i = 0; i < resolutions.getCount(); ++i) {
                    if (String.valueOf(resolutions.getItemAtPosition(i)).startsWith("1920×1080 ")) {
                        resolutions.setSelection(i);
                        call(selection(activity), "rebuildRefreshRates");
                        for (int r = 0; r < rates.getCount(); ++r) {
                            if ("60 Hz".contentEquals(String.valueOf(rates.getItemAtPosition(r)))) {
                                rates.setSelection(r);
                                selected1080 = true;
                                break;
                            }
                        }
                    }
                }
                require(selected1080, "1080p60 is available in the shared adapter catalog");
                DisplayModeOption chosen = (DisplayModeOption)call(selection(activity), "selectedMode");
                require(chosen.width == 1920 && chosen.height == 1080 && chosen.refreshHz == 60,
                        "New-screen session selects exact 1080p60 timing");
                Spinner sources=(Spinner)field(screen(activity),"sourceSelector");
                for(int i=0;i<sources.getCount();i++)require(((ScreenSource)sources.getItemAtPosition(i)).id!=ScreenSource.TEST_SCREEN,
                        "App test screen stays out of the production source picker");
                require(hasText((View)field(screen(activity),"debugScroll"),"Preview app-owned test screen"),
                        "App-owned test screen remains available in Debug");
                if (Build.VERSION.SDK_INT >= 36) ((Spinner)field(screen(activity), "rotationSelector")).setSelection(1);
                Button screencast = (Button)field(screen(activity), "screencastButton");
                require(fixture.start(ScreenSource.TEST_SCREEN,"",chosen,Build.VERSION.SDK_INT>=36?1:0,160,true,false),
                        "Fixture starts hidden test-screen output with the selected HDMI timing");
                require("Stop screencast".contentEquals(screencast.getText()) && screencast.isEnabled(),
                        "Running action becomes Stop on the same button");
                CaptureController capture = (CaptureController)field(screencast(activity), "captureController");
                require(capture != null && capture.width() == 1920 && capture.height() == 1080,
                        "New virtual screen and ImageReader use selected 1080p geometry");
                require(!((Spinner)field(screen(activity), "sourceSelector")).isEnabled(), "Settings locked during session");
                require(!((Spinner)field(screen(activity), "sourceSelector")).performClick()
                        && field(screencast(activity), "captureController") == capture
                        && (Boolean)field(screencast(activity), "settingsLocked"),
                        "Locked source dropdown cannot interrupt output with an EDID read");
                call(screencast(activity), "startSession", false); // A second start must be ignored.
                call(screencast(activity), "probeAppLaunchPermission");
                return null;
            });
            CaptureFixtureTests.waitFor(() -> host.connection.bulkBytes > 0 || !fixture.active(),
                    "Activity output worker reports a result");
            main(test, () -> {
                require(host.connection.bulkBytes > 0, "Activity output: "
                        + ((TextView)field(screen(activity), "outputStatus")).getText());
                return null;
            });
            main(test, () -> {
                Object capture = field(screencast(activity), "captureController");
                VirtualDisplay display = (VirtualDisplay)field(screencast(activity), "captureDisplay");
                Button scene = (Button)field(screen(activity), "sceneButton");
                require(scene.isEnabled() && scene.performClick(), "Hide scene control is wired");
                require(field(screencast(activity), "presentation") == null
                        && field(screencast(activity), "captureController") == capture
                        && field(screencast(activity), "captureDisplay") == display
                        && ((CaptureController)capture).isOpen(), "Hiding scene preserves capture/display ownership");
                require(scene.performClick(), "Show scene control is wired");
                require(((TestScreenPresentation)field(screencast(activity), "presentation")).isShowing()
                        && field(screencast(activity), "captureController") == capture
                        && field(screencast(activity), "captureDisplay") == display, "Showing scene reuses running pipeline");
                AppAppearance.Mode mode = ((AppAppearance)field(screen(activity), "appearance")).mode();
                call(screen(activity), "selectAppearance", AppAppearance.Mode.DARK);
                Switch draws = (Switch)field(screen(activity), "showDrawsToggle");
                draws.setChecked(true);
                draws.setChecked(false);
                call(screen(activity), "selectAppearance", mode);
                require(field(screencast(activity), "captureController") == capture && field(screencast(activity), "captureDisplay") == display
                        && ((CaptureController)capture).isOpen(), "Appearance and draw toggles preserve running capture");
                return null;
            });
            long capturedBeforeBack = main(test, () ->
                    ((java.util.concurrent.atomic.AtomicLong)field(screencast(activity), "acceptedFrames")).get());
            long bytesBeforeBack = host.connection.bulkBytes;
            Object runningCapture = main(test, () -> field(screencast(activity), "captureController"));
            Object runningDisplay = main(test, () -> field(screencast(activity), "captureDisplay"));
            main(test, () -> { activity.onBackPressed(); return null; });
            CaptureFixtureTests.waitFor(() -> {
                try {
                    return main(test, () -> !activity.hasWindowFocus()
                            && ((java.util.concurrent.atomic.AtomicLong)field(screencast(activity), "acceptedFrames")).get() > capturedBeforeBack)
                            && host.connection.bulkBytes > bytesBeforeBack;
                } catch (Exception failure) { throw new AssertionError(failure); }
            }, "Back backgrounds the Activity while capture and USB output keep advancing");
            main(test, () -> {
                require(!activity.isFinishing() && !activity.isDestroyed()
                        && field(screencast(activity), "captureController") == runningCapture
                        && field(screencast(activity), "captureDisplay") == runningDisplay
                        && (Boolean)field(screencast(activity), "settingsLocked"),
                        "Back preserves the running session and its owners");
                activity.startActivity(new Intent(activity, MainActivity.class)
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));
                return null;
            });
            CaptureFixtureTests.waitFor(() -> {
                try { return main(test, activity::hasWindowFocus); }
                catch (Exception failure) { throw new AssertionError(failure); }
            }, "Returning resumes the existing screencast Activity");
            main(test, () -> {
                require(((Button)field(screen(activity), "screencastButton")).performClick(), "Same button stops screencast");
                return null;
            });
            CaptureFixtureTests.waitFor(() -> !output.isBusy() && !fixture.active(), "Activity stop drains output and releases capture");
            main(test, () -> {
                require(field(screencast(activity), "captureDisplay") == null && field(screencast(activity), "captureController") == null, "Activity releases own screen");
                require(((Spinner)field(screen(activity), "sourceSelector")).isEnabled(), "Settings unlocked");
                require("Start screencast".contentEquals(((Button)field(screen(activity), "screencastButton")).getText()),
                        "Stopped action returns to Start");
                require(!((Button)field(screen(activity), "sceneButton")).isEnabled(), "Stop disables scene control");
                require(((TextView)field(screen(activity), "launchStatus")).getText().toString().contains("closed"),
                        "Stop clears the running-screen diagnostic");
                return null;
            });
            main(test, () -> {
                Object adb = field(activity, "adbToolsController");
                @SuppressWarnings("unchecked") List<AdbEndpointDiscovery.Endpoint> pair =
                        (List<AdbEndpointDiscovery.Endpoint>)field(adb, "pairingEndpoints");
                @SuppressWarnings("unchecked") List<AdbEndpointDiscovery.Endpoint> connect =
                        (List<AdbEndpointDiscovery.Endpoint>)field(adb, "connectEndpoints");
                pair.add(new AdbEndpointDiscovery.Endpoint(AdbEndpointDiscovery.PAIRING_SERVICE,
                        "fixture", java.net.InetAddress.getByName("127.0.0.1"), 1));
                call(adb, "refreshPairButton");
                require(!((Button)field(screen(activity), "pairButton")).isEnabled(), "Unmatched pairing endpoint disabled");
                connect.add(new AdbEndpointDiscovery.Endpoint(AdbEndpointDiscovery.CONNECT_SERVICE,
                        "fixture", java.net.InetAddress.getByName("127.0.0.1"), 2));
                call(adb, "refreshPairButton");
                require(((Button)field(screen(activity), "pairButton")).isEnabled(), "Matched endpoints enable pairing");
                call(adb, "choosePairingEndpoint"); return null;
            });
            OutputFixtureTests.click(test, "Cancel");
            main(test, () -> { call(field(activity, "adbToolsController"), "choosePairingEndpoint"); return null; });
            OutputFixtureTests.click(test, "127.0.0.1");
            setInput(test, "12345"); OutputFixtureTests.click(test, "Pair");
            CaptureFixtureTests.waitFor(() -> {
                try { return main(test, () -> ((TextView)field(screen(activity), "status")).getText().toString().contains("six digits")); }
                catch (Exception failure) { throw new AssertionError(failure); }
            }, "Activity pairing executor -> Rust validation -> UI result");
            main(test, () -> {
                Object adb = field(activity, "adbToolsController");
                call(adb, "startDiscovery"); call(adb, "startDiscovery");
                ((Runnable)field(adb, "discoveryTimeout")).run();
                require(((Button)field(screen(activity), "discoverButton")).isEnabled(), "Discovery timeout restores controls");
                require(field(adb, "discovery") == null, "Discovery timeout closes listener");
                return null;
            });
        } finally { main(test, () -> { fixture.close(); activity.finish(); return null; }); }
    }

    @SuppressWarnings("unchecked")
    private static void probeFromDropdown(Instrumentation test, MainActivity activity,
            UsbOutputController output, Host host) throws Exception {
        int opens = host.opens;
        main(test, () -> {
            Spinner sources = (Spinner)field(screen(activity), "sourceSelector");
            ((ArrayAdapter<ScreenSource>)sources.getAdapter()).add(new ScreenSource(999999, "Stale screen", 0, 0));
            require(sources.performClick(), "Source dropdown opens");
            for (int i = 0; i < sources.getCount(); ++i)
                require(((ScreenSource)sources.getItemAtPosition(i)).id != 999999,
                        "Opening dropdown refreshes inventory before presenting it");
            return null;
        });
        test.waitForIdleSync();
        OutputFixtureTests.click(test, "Phone screen");
        CaptureFixtureTests.waitFor(() -> !output.isBusy() && host.opens > opens, "Dropdown EDID read completes");
        test.waitForIdleSync();
        require(!activity.isFinishing() && !activity.isDestroyed(), "Dropdown selection keeps the Activity alive");
    }

    private static void probeFromAnyDropdown(Instrumentation test, MainActivity activity,
            UsbOutputController output, Host host, Spinner spinner, String description,
            boolean revokePermission) throws Exception {
        int opens = host.opens;
        int requests = host.requests;
        if (revokePermission) host.permission = false;
        String selected = main(test, () -> {
            require(spinner.performClick(), description + " opens");
            return String.valueOf(spinner.getSelectedItem());
        });
        OutputFixtureTests.click(test, selected);
        if (revokePermission) {
            CaptureFixtureTests.waitFor(() -> output.isBusy() && host.requests > requests,
                    description + " re-requests USB access when permission is missing");
            host.permission = true;
            host.callback.send(activity, 0, new Intent().putExtra(UsbManager.EXTRA_DEVICE,
                    host.devices.get(0)));
        }
        CaptureFixtureTests.waitFor(() -> !output.isBusy() && host.opens > opens,
                description + " re-detects adapter and reads monitor EDID");
        test.waitForIdleSync();
    }

    private static void setInput(Instrumentation test, String text) throws Exception {
        CaptureFixtureTests.waitFor(() -> setNodeText(test.getUiAutomation().getRootInActiveWindow(), text), "Pairing input");
    }

    private static boolean isDescendant(View target, View ancestor) {
        View current = target;
        while (current != null) {
            if (current == ancestor) return true;
            if (!(current.getParent() instanceof View)) return false;
            current = (View)current.getParent();
        }
        return false;
    }

    private static boolean hasText(View view, String expected) {
        if (view instanceof TextView && expected.contentEquals(((TextView)view).getText())) return true;
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup)view;
            for (int i = 0; i < group.getChildCount(); ++i)
                if (hasText(group.getChildAt(i), expected)) return true;
        }
        return false;
    }

    private static boolean setNodeText(AccessibilityNodeInfo node, String text) {
        if (node == null) return false;
        if ("android.widget.EditText".contentEquals(node.getClassName())) {
            Bundle args = new Bundle(); args.putCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE, text);
            return node.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, args);
        }
        for (int i = 0; i < node.getChildCount(); ++i) if (setNodeText(node.getChild(i), text)) return true;
        return false;
    }

    static void service(Instrumentation test) throws Exception {
        Activity activity = test.startActivitySync(new Intent(test.getTargetContext(), MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        AtomicReference<ProjectionCaptureService> service = new AtomicReference<>();
        CountDownLatch connected = new CountDownLatch(1);
        ServiceConnection binding = new ServiceConnection() {
            public void onServiceConnected(ComponentName name, IBinder binder) {
                service.set(((ProjectionCaptureService.LocalBinder)binder).service()); connected.countDown();
            }
            public void onServiceDisconnected(ComponentName name) {}
        };
        boolean bound = main(test, () -> activity.bindService(new Intent(activity, ProjectionCaptureService.class), binding, Context.BIND_AUTO_CREATE));
        require(bound && connected.await(10, TimeUnit.SECONDS), "Capture service bound");
        try {
            main(test, () -> {
                ProjectionCaptureService capture = service.get(); List<ProjectionCaptureService.State> states = new ArrayList<>();
                capture.setListener((state, message) -> states.add(state));
                require(states.get(0) == ProjectionCaptureService.State.WAITING, "Service initial typed state");
                require(capture.captureController() == null, "No active controller before consent");
                require(capture.onStartCommand(null, 0, 1) == Service.START_NOT_STICKY, "Null service intent");
                require(capture.onStartCommand(new Intent(), 0, 2) == Service.START_NOT_STICKY, "Missing consent extras");
                Intent invalid = new Intent().putExtra(ProjectionCaptureService.EXTRA_RESULT_CODE, Activity.RESULT_OK)
                        .putExtra(ProjectionCaptureService.EXTRA_RESULT_DATA, new Intent())
                        .putExtra(ProjectionCaptureService.EXTRA_WIDTH, 640)
                        .putExtra(ProjectionCaptureService.EXTRA_HEIGHT, 480)
                        .putExtra(ProjectionCaptureService.EXTRA_DENSITY_DPI, 160);
                capture.onStartCommand(invalid, 0, 3);
                require(states.contains(ProjectionCaptureService.State.FAILED), "Invalid consent/foreground permission failure handled");
                capture.stopCapture(); require(states.contains(ProjectionCaptureService.State.STOPPED), "Service stopped typed state");
                android.media.projection.MediaProjection.Callback callback =
                        (android.media.projection.MediaProjection.Callback)field(capture, "callback");
                callback.onStop(); capture.setListener(null);
                return null;
            });
        } finally {
            main(test, () -> { activity.unbindService(binding); activity.finish(); return null; });
        }
    }
}
