package io.github.taglia_ti.ms912x_ng.desktop;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Binder;
import android.os.Build;
import android.os.IBinder;

/** Started while USB is active; binding/recreating the interface does not own capture. */
public final class CaptureSessionService extends Service implements SessionCoordinator.ForegroundOwner {
    static final String ACTION_STOP="io.github.taglia_ti.ms912x_ng.session.STOP";
    private static final String ACTION_KEEP="io.github.taglia_ti.ms912x_ng.session.KEEP";
    private static final String CHANNEL="usb_output";
    private static final int NOTIFICATION=42;
    final class LocalBinder extends Binder { SessionCoordinator coordinator(){return coordinator;} }
    private final IBinder binder=new LocalBinder();
    private SessionCoordinator coordinator;
    private boolean foreground;
    @Override public void onCreate(){
        super.onCreate();
        if(Build.VERSION.SDK_INT>=26)getSystemService(NotificationManager.class).createNotificationChannel(
                new NotificationChannel(CHANNEL,"USB display output",NotificationManager.IMPORTANCE_LOW));
        coordinator=new SessionCoordinator(this,this);
    }
    @Override public IBinder onBind(Intent intent){return binder;}
    @Override public int onStartCommand(Intent intent,int flags,int startId){
        if(intent!=null&&ACTION_STOP.equals(intent.getAction()))coordinator.stop("Screencast stopped.");
        if(!foreground)stopSelf(startId);
        return START_NOT_STICKY;
    }
    @Override public void begin(){
        Intent keep=new Intent(this,CaptureSessionService.class).setAction(ACTION_KEEP);
        if(Build.VERSION.SDK_INT>=26)startForegroundService(keep);else startService(keep);
        PendingIntent open=PendingIntent.getActivity(this,0,new Intent(this,MainActivity.class),
                PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        PendingIntent stop=PendingIntent.getService(this,0,new Intent(this,CaptureSessionService.class).setAction(ACTION_STOP),
                PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder builder=Build.VERSION.SDK_INT>=26?new Notification.Builder(this,CHANNEL):new Notification.Builder(this);
        Notification notification=builder.setSmallIcon(android.R.drawable.ic_menu_view)
                .setContentTitle("MS912x USB display").setContentText("Screencast is running")
                .setContentIntent(open).setOngoing(true).addAction(android.R.drawable.ic_media_pause,"Stop",stop).build();
        if(Build.VERSION.SDK_INT>=29)startForeground(NOTIFICATION,notification,ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
        else startForeground(NOTIFICATION,notification);
        foreground=true;
    }
    @Override public void end(){foreground=false;stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();}
    @Override public void onDestroy(){coordinator.close();super.onDestroy();}
}
