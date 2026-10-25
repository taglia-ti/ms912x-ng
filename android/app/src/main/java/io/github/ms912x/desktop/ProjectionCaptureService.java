package io.github.ms912x.desktop;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.Binder;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;

import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicBoolean;

/** Owns the consented screen/window capture session while it is running. */
public final class ProjectionCaptureService extends Service {
    static final String ACTION_KEEP_ALIVE = "io.github.ms912x.projection.KEEP_ALIVE";
    static final String EXTRA_RESULT_CODE = "projection.result_code";
    static final String EXTRA_RESULT_DATA = "projection.result_data";
    static final String EXTRA_WIDTH = "projection.width";
    static final String EXTRA_HEIGHT = "projection.height";
    static final String EXTRA_DENSITY_DPI = "projection.density_dpi";
    static final String EXTRA_ROTATION = "projection.rotation";

    private static final String CHANNEL_ID = "screen_capture";
    private static final int NOTIFICATION_ID = 41;

    enum State { WAITING, ACTIVE, STOPPED, FAILED }

    interface Listener {
        void onCaptureStatus(State state, String message);
    }

    final class LocalBinder extends Binder {
        ProjectionCaptureService service() {
            return ProjectionCaptureService.this;
        }
    }

    private final IBinder binder = new LocalBinder();
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final AtomicLong acceptedFrames = new AtomicLong();
    private final AtomicLong rejectedFrames = new AtomicLong();
    private final AtomicBoolean statusPending = new AtomicBoolean();
    private final Runnable publishStatus = () -> {
        statusPending.set(false);
        publishFrameStatus();
    };
    private Listener listener;
    private CaptureController captureController;
    private MediaProjection projection;
    private VirtualDisplay virtualDisplay;
    private boolean started;
    private String status = "Waiting for capture consent.";
    private State state = State.WAITING;

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null) {
            stopSelf(startId);
            return START_NOT_STICKY;
        }
        if (started) return START_NOT_STICKY;
        if (ACTION_KEEP_ALIVE.equals(intent.getAction())) { stopSelf(startId); return START_NOT_STICKY; }
        beginCapture(intent);
        return START_NOT_STICKY;
    }

    /** Called through the bound local owner; cancelled binds cannot create a late projection. */
    void beginCapture(Intent intent) {
        if (started) return;

        int resultCode = intent.getIntExtra(EXTRA_RESULT_CODE, 0);
        Intent resultData = getResultData(intent);
        int width = intent.getIntExtra(EXTRA_WIDTH, 0);
        int height = intent.getIntExtra(EXTRA_HEIGHT, 0);
        int densityDpi = intent.getIntExtra(EXTRA_DENSITY_DPI, 0);
        int rotation = intent.getIntExtra(EXTRA_ROTATION, 0);
        if (resultCode == 0 || resultData == null || width <= 0 || height <= 0
                || densityDpi <= 0) {
            stopSelf();
            return;
        }

        try {
            if (Build.VERSION.SDK_INT >= 29) {
                startForeground(NOTIFICATION_ID, buildNotification(),
                        android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION);
            } else {
                startForeground(NOTIFICATION_ID, buildNotification());
            }
            acceptedFrames.set(0);
            rejectedFrames.set(0);
            MediaProjectionManager manager = getSystemService(MediaProjectionManager.class);
            projection = manager.getMediaProjection(resultCode, resultData);
            if (projection == null) {
                throw new IllegalStateException("Android returned no MediaProjection");
            }

            captureController = new CaptureController(width, height, 2, result -> {
                boolean first=false;
                if (result == 0) {
                    first=acceptedFrames.incrementAndGet()==1;
                } else {
                    rejectedFrames.incrementAndGet();
                }
                // Coalesce diagnostics only; captured frames remain unpaced.
                // Updating visible text per frame would capture our own counter again.
                if(first){
                    // Readiness must not wait for the diagnostic throttle.
                    statusPending.set(true);mainHandler.removeCallbacks(publishStatus);mainHandler.post(publishStatus);
                }else if (statusPending.compareAndSet(false, true)) {
                    mainHandler.postDelayed(publishStatus, 2000);
                }
            });
            projection.registerCallback(callback, mainHandler);
            virtualDisplay = projection.createVirtualDisplay(
                    "ms912x-consented-screen", width, height, densityDpi,
                    DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                    captureController.surface(), null, mainHandler);
            if (virtualDisplay == null) {
                throw new IllegalStateException("MediaProjection display creation returned null");
            }
            VirtualScreenOptions.applyRotation(virtualDisplay, rotation);
            started = true;
            updateStatus(State.ACTIVE, "Screen/window capture active; accepted native copies: 0, rejected: 0.");
        } catch (RuntimeException failure) {
            updateStatus(State.FAILED, "Screen capture failed: " + failure.getClass().getSimpleName()
                    + ": " + failure.getMessage());
            stopProducer();
            if (listener == null) releaseReader(this::stopSelf);
        }
    }

    private Intent getResultData(Intent intent) {
        if (Build.VERSION.SDK_INT >= 33) {
            return intent.getParcelableExtra(EXTRA_RESULT_DATA, Intent.class);
        }
        //noinspection deprecation
        return intent.getParcelableExtra(EXTRA_RESULT_DATA);
    }

    private void publishFrameStatus() {
        if (!started) return;
        updateStatus(State.ACTIVE, "Screen/window capture active; accepted native copies: "
                + acceptedFrames.get() + ", rejected: " + rejectedFrames.get() + ".");
    }

    private void updateStatus(State nextState, String nextStatus) {
        state = nextState;
        status = nextStatus;
        if (listener != null) {
            listener.onCaptureStatus(state, status);
        }
    }

    void setListener(Listener nextListener) {
        listener = nextListener;
        if (listener != null) {
            listener.onCaptureStatus(state, status);
        }
    }

    CaptureController captureController() {
        return started ? captureController : null;
    }

    void stopCapture() {
        updateStatus(State.STOPPED, "Screen/window capture stopped.");
        stopProducer();
        releaseReader(this::stopSelf);
    }

    void stopProducer() {
        if (virtualDisplay != null) {
            virtualDisplay.release();
            virtualDisplay = null;
        }
        if (projection != null) {
            MediaProjection oldProjection = projection;
            projection = null;
            oldProjection.unregisterCallback(callback);
            oldProjection.stop();
        }
        started = false;
        mainHandler.removeCallbacks(publishStatus);
        statusPending.set(false);
        stopForeground(STOP_FOREGROUND_REMOVE);
    }

    void releaseReader(Runnable completed) {
        CaptureController old = captureController;
        captureController = null;
        CaptureCleanup.close(old, completed);
    }

    private final MediaProjection.Callback callback = new MediaProjection.Callback() {
        @Override
        public void onStop() {
            stopProducer();
            updateStatus(State.STOPPED, "Android ended screen capture.");
            if (listener == null) releaseReader(ProjectionCaptureService.this::stopSelf);
        }
    };

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(CHANNEL_ID,
                    "Screen capture", NotificationManager.IMPORTANCE_LOW);
            getSystemService(NotificationManager.class).createNotificationChannel(channel);
        }
    }

    private Notification buildNotification() {
        Intent activityIntent = new Intent(this, MainActivity.class);
        PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, activityIntent,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);
        return builder.setSmallIcon(android.R.drawable.ic_menu_view)
                .setContentTitle("MS912x screen capture")
                .setContentText("Capturing the screen or app window you selected")
                .setContentIntent(pendingIntent)
                .setOngoing(true)
                .build();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }

    @Override
    public void onDestroy() {
        stopProducer();
        if (listener != null) updateStatus(State.STOPPED, "Screen capture service stopped.");
        else releaseReader(() -> {});
        listener = null;
        super.onDestroy();
    }
}
