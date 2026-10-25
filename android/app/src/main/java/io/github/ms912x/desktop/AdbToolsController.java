package io.github.ms912x.desktop;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Owns Wireless ADB discovery, endpoint selection, and blocking native calls. */
final class AdbToolsController implements AutoCloseable {
    private static final long DISCOVERY_TIMEOUT_MS = 15_000L;

    final List<AdbEndpointDiscovery.Endpoint> pairingEndpoints = new ArrayList<>();
    final List<AdbEndpointDiscovery.Endpoint> connectEndpoints = new ArrayList<>();
    private final Set<String> endpointRows = new LinkedHashSet<>();
    private final Activity activity;
    private final AdbCredentialStore credentials;
    private final TextView status;
    private final LinearLayout endpointViews;
    private final Button discoverButton;
    private final Button pairButton;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    interface SetupListener { void onDiscoveryChanged(); void onPairingFinished(String host,boolean success); }
    private SetupListener setupListener;
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    AdbEndpointDiscovery discovery;
    final Runnable discoveryTimeout;

    AdbToolsController(Activity activity, AdbCredentialStore credentials, TextView status,
                       LinearLayout endpointViews, Button discoverButton, Button pairButton) {
        this.activity = activity;
        this.credentials = credentials;
        this.status = status;
        this.endpointViews = endpointViews;
        this.discoverButton = discoverButton;
        this.pairButton = pairButton;
        discoveryTimeout = () -> {
            stopDiscovery();
            status.setText("Discovery timed out. Enable Wireless debugging and try again.");
            if(setupListener!=null)setupListener.onDiscoveryChanged();
        };
    }

    void startDiscovery() {
        stopDiscovery();
        endpointRows.clear();
        pairingEndpoints.clear();
        connectEndpoints.clear();
        endpointViews.removeAllViews();
        status.setText("Searching local DNS-SD for pairing and TLS connection services.\n"
                + "Only endpoints on this phone's network interfaces are accepted.");
        AdbEndpointDiscovery[] owner = new AdbEndpointDiscovery[1];
        owner[0] = new AdbEndpointDiscovery(activity, new AdbEndpointDiscovery.Listener() {
            @Override
            public void onEndpoint(AdbEndpointDiscovery.Endpoint endpoint) {
                activity.runOnUiThread(() -> {
                    if (discovery != owner[0] || !endpointRows.add(endpoint.toString())) return;
                    TextView row = new TextView(activity);
                    row.setText(endpoint.toString());
                    row.setTextSize(15);
                    endpointViews.addView(row);
                    if (AdbEndpointDiscovery.PAIRING_SERVICE.equals(endpoint.serviceType)) {
                        pairingEndpoints.add(endpoint);
                    } else if (AdbEndpointDiscovery.CONNECT_SERVICE.equals(endpoint.serviceType)) {
                        connectEndpoints.add(endpoint);
                    }
                    refreshPairButton();
                    if(setupListener!=null)setupListener.onDiscoveryChanged();
                });
            }

            @Override
            public void onFailure(String serviceType, int errorCode) {
                activity.runOnUiThread(() -> {
                    if (discovery == owner[0]) {
                        status.setText("Discovery error for " + serviceType + ": " + errorCode
                                + "\nCheck that Wireless debugging is enabled.");
                        if(setupListener!=null)setupListener.onDiscoveryChanged();
                    }
                });
            }
        });
        discovery = owner[0];
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

    void stopDiscovery() {
        mainHandler.removeCallbacks(discoveryTimeout);
        if (discovery != null) {
            discovery.close();
            discovery = null;
        }
        if (discoverButton != null) discoverButton.setEnabled(true);
    }

    void setSetupListener(SetupListener listener){setupListener=listener;}
    boolean hasReusableCredentialFor(String host){return credentials!=null&&credentials.hasReusableCredentialFor(host);}
    boolean hasMatchingPairingEndpoint(String host){
        for(AdbEndpointDiscovery.Endpoint pairing:pairingEndpoints)
            if(pairing.address.getHostAddress().equals(host))for(AdbEndpointDiscovery.Endpoint connect:connectEndpoints)
                if(connect.address.equals(pairing.address))return true;
        return false;
    }
    boolean hasAnyMatchingPairingEndpoint(){
        for(AdbEndpointDiscovery.Endpoint pairing:pairingEndpoints)
            if(hasMatchingPairingEndpoint(pairing.address.getHostAddress()))return true;
        return false;
    }
    boolean discoveryRunning(){return discovery!=null;}

    void refreshPairButton() {
        boolean compatible = false;
        for (AdbEndpointDiscovery.Endpoint pairing : pairingEndpoints) {
            for (AdbEndpointDiscovery.Endpoint connect : connectEndpoints) {
                if (pairing.address.equals(connect.address)) {
                    compatible = true;
                    break;
                }
            }
            if (compatible) break;
        }
        if (pairButton != null) pairButton.setEnabled(compatible);
    }

    void choosePairingEndpoint() {
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
        new AlertDialog.Builder(activity)
                .setTitle("Select this device's ADB endpoints")
                .setItems(labels.toArray(new String[0]),
                        (dialog, which) -> promptPairingCode(pairs.get(which)))
                .setNegativeButton("Cancel", null)
                .show();
    }

    void chooseHelperEndpoint(java.util.function.BiConsumer<String,Integer> connect) {
        if(connectEndpoints.isEmpty()){status.setText("Find this phone's Wireless debugging endpoint first.");return;}
        List<AdbEndpointDiscovery.Endpoint> options=new ArrayList<>(connectEndpoints);
        String[] labels=new String[options.size()];
        for(int i=0;i<labels.length;++i)labels[i]=options.get(i).address.getHostAddress()+":"+options.get(i).port;
        new AlertDialog.Builder(activity).setTitle("Connect this phone's ADB helper")
                .setItems(labels,(dialog,which)->connect.accept(options.get(which).address.getHostAddress(),options.get(which).port))
                .setNegativeButton("Cancel",null).show();
    }

    private void promptPairingCode(AdbEndpointDiscovery.Endpoint[] pair) {
        EditText codeInput = new EditText(activity);
        codeInput.setInputType(InputType.TYPE_CLASS_NUMBER
                | InputType.TYPE_NUMBER_VARIATION_PASSWORD);
        codeInput.setHint("6-digit pairing code");
        int pad = (int) (24 * activity.getResources().getDisplayMetrics().density);
        codeInput.setPadding(pad, pad / 2, pad, pad / 2);
        new AlertDialog.Builder(activity)
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
        executor.execute(() -> {
            NativeAdbCall.Result result = NativeAdbCall.run(
                    () -> NativeAdbBridge.nativePairAndVerify(credentials,
                            pairing.address.getHostAddress(), pairing.port,
                            connect.address.getHostAddress(), connect.port, code),
                    NativeAdbBridge::nativeBackendVersion, "Wireless ADB failed");
            activity.runOnUiThread(() -> {
                if (activity.isFinishing() || activity.isDestroyed()) return;
                status.setText(result.backendVersion + "\n" + result.message);
                refreshPairButton();
                if(setupListener!=null)setupListener.onPairingFinished(pairing.address.getHostAddress(),result.message.startsWith("Paired device "));
            });
        });
    }

    void runLocalTcpProbe() {
        String host = credentials == null ? null : credentials.firstLocalIpv4Address();
        if (host == null) {
            status.setText("No local IPv4 address is available for the debug ADB probe.");
            return;
        }
        status.setText("Connecting to local adbd at " + host + ":5555…");
        executor.execute(() -> {
            NativeAdbCall.Result result = NativeAdbCall.run(
                    () -> NativeAdbBridge.nativeConnectLocalTcpAndVerify(
                            credentials, host, 5555),
                    NativeAdbBridge::nativeBackendVersion, "Local ADB TCP probe failed");
            activity.runOnUiThread(() -> {
                if (!activity.isFinishing() && !activity.isDestroyed())
                    status.setText(result.backendVersion + "\n" + result.message);
            });
        });
    }

    @Override
    public void close() {
        stopDiscovery();
        NativeAdbCall.cancelCurrentIfAvailable();
        executor.shutdownNow();
    }
}
