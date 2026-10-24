package io.github.ms912x.desktop;

import android.os.Handler;
import android.os.Looper;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Never join ImageReader's callback thread on the UI thread. */
final class CaptureCleanup {
    private static final ExecutorService work=Executors.newSingleThreadExecutor(r->{Thread t=new Thread(r,"ms912x-capture-cleanup");t.setDaemon(true);return t;});
    private static final Handler main=new Handler(Looper.getMainLooper());
    static void close(CaptureController capture,Runnable completed) {
        work.execute(()->{try{if(capture!=null)capture.close();}finally{main.post(completed);}});
    }
    private CaptureCleanup(){ }
}
