package io.github.taglia_ti.ms912x_ng.desktop;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.RemoteInput;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
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
    private static final long PAIRING_DISCOVERY_TIMEOUT_MS = 300_000L;
    private static final String PAIRING_CHANNEL = "adb_pairing";
    private static final String PAIRING_ACTION = "io.github.taglia_ti.ms912x_ng.desktop.PAIRING_REPLY";
    private static final String PAIRING_CODE = "pairing_code";
    private static final int PAIRING_NOTIFICATION = 9123;

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
    private final AdbCredentialAvailability credentialAvailability;
    AdbEndpointDiscovery discovery;
    final Runnable discoveryTimeout;
    private boolean notificationPairing;
    private boolean pairingInProgress;
    private final BroadcastReceiver pairingReceiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            android.os.Bundle results = RemoteInput.getResultsFromIntent(intent);
            CharSequence input = results == null ? null : results.getCharSequence(PAIRING_CODE);
            String code = input == null ? "" : input.toString().trim();
            if (!code.matches("[0-9]{6}")) {
                showPairingNotification("Enter the six-digit code shown in Wireless debugging.");
                return;
            }
            if (pairingInProgress) return;
            AdbEndpointDiscovery.Endpoint pairing = null, connect = null;
            for (AdbEndpointDiscovery.Endpoint candidate : pairingEndpoints) {
                for (AdbEndpointDiscovery.Endpoint endpoint : connectEndpoints) {
                    if (!candidate.address.equals(endpoint.address)) continue;
                    if (pairing != null) {
                        showPairingNotification("Multiple endpoints found. Refresh discovery and try again.");
                        return;
                    }
                    pairing = candidate; connect = endpoint;
                }
            }
            if (pairing == null) {
                showPairingNotification("Pairing service not found yet. Keep the code dialog open and try again.");
                return;
            }
            pairingInProgress = true;
            showPairingNotification("Pairing and checking the connection…");
            runPairing(pairing, connect, code);
        }
    };

    AdbToolsController(Activity activity, AdbCredentialStore credentials, TextView status,
                       LinearLayout endpointViews, Button discoverButton, Button pairButton) {
        this.activity = activity;
        this.credentials = credentials;
        this.status = status;
        this.endpointViews = endpointViews;
        this.discoverButton = discoverButton;
        this.pairButton = pairButton;
        credentialAvailability=new AdbCredentialAvailability(executor,mainHandler::post,
                host->credentials!=null&&credentials.hasReusableCredentialFor(host),
                ()->{if(setupListener!=null&&!activity.isFinishing()&&!activity.isDestroyed())setupListener.onDiscoveryChanged();});
        discoveryTimeout = () -> {
            stopDiscovery();
            status.setText("Discovery timed out. Enable Wireless debugging and try again.");
            if(notificationPairing)showPairingNotification("Discovery timed out. Return to MS912x and open Wireless debugging again.");
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
            @Override public void onEndpointLost(String serviceType,String serviceName){
                activity.runOnUiThread(()->{
                    if(discovery!=owner[0])return;
                    pairingEndpoints.removeIf(endpoint->endpoint.serviceType.equals(serviceType)&&java.util.Objects.equals(endpoint.serviceName,serviceName));
                    connectEndpoints.removeIf(endpoint->endpoint.serviceType.equals(serviceType)&&java.util.Objects.equals(endpoint.serviceName,serviceName));
                    endpointRows.clear();endpointViews.removeAllViews();
                    List<AdbEndpointDiscovery.Endpoint> remaining=new ArrayList<>(pairingEndpoints);remaining.addAll(connectEndpoints);
                    for(AdbEndpointDiscovery.Endpoint endpoint:remaining){
                        endpointRows.add(endpoint.toString());TextView row=new TextView(activity);row.setText(endpoint.toString());row.setTextSize(15);endpointViews.addView(row);
                    }
                    refreshPairButton();if(setupListener!=null)setupListener.onDiscoveryChanged();
                });
            }
        },address->credentials!=null&&credentials.isLocalAddress(address.getHostAddress()));
        discovery = owner[0];
        try {
            discovery.start();
            discoverButton.setEnabled(false);
            mainHandler.postDelayed(discoveryTimeout, notificationPairing ? PAIRING_DISCOVERY_TIMEOUT_MS : DISCOVERY_TIMEOUT_MS);
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
    boolean hasReusableCredentialFor(String host){return credentialAvailability.hasFor(host);}
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

    boolean notificationPairingActive(){return notificationPairing;}

    void beginNotificationPairing(){
        if (Build.VERSION.SDK_INT < 30) return;
        if (!notificationPairing) {
            IntentFilter filter = new IntentFilter(PAIRING_ACTION);
            if (Build.VERSION.SDK_INT >= 33) activity.registerReceiver(pairingReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
            else registerLegacyPairingReceiver(filter);
            notificationPairing = true;
        }
        startDiscovery();
        showPairingNotification("Open Wireless debugging, choose pairing code, then enter it here.");
    }

    @SuppressLint("UnspecifiedRegisterReceiverFlag")
    private void registerLegacyPairingReceiver(IntentFilter filter) {
        // API 30-32 has no exported-state flag; restrict replies to this app's signer.
        activity.registerReceiver(pairingReceiver, filter,
                activity.getPackageName() + ".permission.PAIRING_CALLBACK", null);
    }

    void endNotificationPairing(){
        if (!notificationPairing) return;
        notificationPairing = false;
        activity.unregisterReceiver(pairingReceiver);
        activity.getSystemService(NotificationManager.class).cancel(PAIRING_NOTIFICATION);
    }

    private void showPairingNotification(String message){
        if (!notificationPairing || Build.VERSION.SDK_INT < 30) return;
        NotificationManager manager = activity.getSystemService(NotificationManager.class);
        manager.createNotificationChannel(new NotificationChannel(PAIRING_CHANNEL, "Wireless debugging pairing", NotificationManager.IMPORTANCE_DEFAULT));
        Intent reply = new Intent(PAIRING_ACTION).setPackage(activity.getPackageName());
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 31) flags |= PendingIntent.FLAG_MUTABLE;
        PendingIntent pending = PendingIntent.getBroadcast(activity, 0, reply, flags);
        RemoteInput input = new RemoteInput.Builder(PAIRING_CODE).setLabel("Six-digit pairing code").build();
        Notification.Action action = new Notification.Action.Builder(null, "Enter pairing code", pending)
                .addRemoteInput(input).build();
        Notification notification = new Notification.Builder(activity, PAIRING_CHANNEL)
                .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
                .setContentTitle("Pair MS912x with this phone")
                .setContentText(message).setStyle(new Notification.BigTextStyle().bigText(message))
                .setOngoing(true).setOnlyAlertOnce(true).addAction(action).build();
        manager.notify(PAIRING_NOTIFICATION, notification);
    }

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
            NativeAdbCall.Result result = NativeAdbCall.runOwned(this,
                    () -> NativeAdbBridge.nativePairAndVerify(credentials,
                            pairing.address.getHostAddress(), pairing.port,
                            connect.address.getHostAddress(), connect.port, code),
                    NativeAdbBridge::nativeBackendVersion, "Wireless ADB failed");
            activity.runOnUiThread(() -> {
                if (activity.isFinishing() || activity.isDestroyed()) return;
                pairingInProgress = false;
                boolean success = result.message.startsWith("Paired device ");
                if (notificationPairing) {
                    if (success) endNotificationPairing();
                    else showPairingNotification(result.message + " Try again while the code is visible.");
                }
                credentialAvailability.invalidate(pairing.address.getHostAddress());
                status.setText(result.backendVersion + "\n" + result.message);
                refreshPairButton();
                if(setupListener!=null)setupListener.onPairingFinished(pairing.address.getHostAddress(),success);
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
            NativeAdbCall.Result result = NativeAdbCall.runOwned(this,
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
        endNotificationPairing();
        credentialAvailability.close();
        stopDiscovery();
        executor.shutdownNow();
        NativeAdbCall.cancelIfOwned(this);
    }
}
