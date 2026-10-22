package io.github.ms912x.desktop;

import android.app.Activity;
import android.os.Handler;
import android.os.Bundle;
import android.os.Looper;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.LinkedHashSet;
import java.util.Set;

public final class MainActivity extends Activity {
    private static final long DISCOVERY_TIMEOUT_MS = 15_000L;
    private final Set<String> endpointRows = new LinkedHashSet<>();
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private LinearLayout page;
    private TextView status;
    private LinearLayout endpoints;
    private Button discoverButton;
    private AdbEndpointDiscovery discovery;
    private final Runnable discoveryTimeout = () -> {
        stopDiscovery();
        if (status != null) {
            status.setText("Discovery timed out. Enable Wireless debugging and try again.\n"
                    + "Pairing and connecting are not implemented yet.");
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);

        page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setGravity(Gravity.CENTER_VERTICAL);
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        page.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("MS912x Desktop");
        title.setTextSize(24);
        page.addView(title);

        TextView status = new TextView(this);
        this.status = status;
        status.setText("Android 11+ virtual desktop prototype\n\n"
                + "Pairing protocol and shell display helper are not implemented yet.\n"
                + "No phone-screen mirroring fallback is used.");
        status.setTextSize(16);
        status.setPadding(0, pad / 2, 0, 0);
        page.addView(status);

        discoverButton = new Button(this);
        discoverButton.setText("Find wireless debugging endpoints");
        discoverButton.setOnClickListener(view -> startDiscovery());
        page.addView(discoverButton);

        ScrollView scroll = new ScrollView(this);
        endpoints = new LinearLayout(this);
        endpoints.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(endpoints, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        LinearLayout.LayoutParams scrollParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1);
        page.addView(scroll, scrollParams);
        setContentView(page);
    }

    private void startDiscovery() {
        if (discovery != null) {
            discovery.close();
            discovery = null;
        }
        endpointRows.clear();
        endpoints.removeAllViews();
        status.setText("Searching local DNS-SD for pairing and TLS connection services.\n"
                + "This does not pair or connect yet.");
        discovery = new AdbEndpointDiscovery(this, new AdbEndpointDiscovery.Listener() {
            @Override
            public void onEndpoint(AdbEndpointDiscovery.Endpoint endpoint) {
                runOnUiThread(() -> {
                    if (discovery != null && endpointRows.add(endpoint.toString())) {
                        TextView row = new TextView(MainActivity.this);
                        row.setText(endpoint.toString());
                        row.setTextSize(15);
                        endpoints.addView(row);
                    }
                });
            }

            @Override
            public void onFailure(String serviceType, int errorCode) {
                runOnUiThread(() -> status.setText(
                        "Discovery error for " + serviceType + ": " + errorCode
                                + "\nPairing and connecting are not implemented yet."));
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

    @Override
    protected void onStop() {
        stopDiscovery();
        super.onStop();
    }
}
