package io.github.ms912x.desktop;

import android.app.Activity;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.os.SystemClock;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

/** Standalone test-APK content: another UID, with no dependency on the target APK. */
public final class ExternalDisplayFixtureActivity extends Activity {
    private FixtureView fixture;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        fixture = new FixtureView(this);
        setContentView(fixture);
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_A) {
            if (event.getAction() == KeyEvent.ACTION_DOWN) {
                fixture.lastKey = "KEYCODE_A";
                fixture.updateDescription();
                fixture.invalidate();
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    private static final class FixtureView extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Handler handler = new Handler(Looper.getMainLooper());
        private final long started = SystemClock.elapsedRealtime();
        private final Runnable tick = new Runnable() {
            @Override public void run() {
                invalidate();
                handler.postDelayed(this, 1000);
            }
        };
        private int clicks;
        private String lastKey = "waiting";

        FixtureView(Context context) { super(context); }

        @Override protected void onAttachedToWindow() {
            super.onAttachedToWindow();
            updateDescription();
            handler.post(tick);
        }

        @Override protected void onDetachedFromWindow() {
            handler.removeCallbacks(tick);
            super.onDetachedFromWindow();
        }

        @Override protected void onDraw(Canvas canvas) {
            float w = getWidth(), h = getHeight();
            canvas.drawColor(Color.rgb(16, 35, 70));
            paint.setColor(Color.WHITE);
            paint.setTextSize(w * .035f);
            canvas.drawText("INDEPENDENT TEST APP", w * .06f, h * .16f, paint);
            paint.setTextSize(w * .025f);
            canvas.drawText("UID " + Process.myUid() + " / display "
                    + getDisplay().getDisplayId(), w * .06f, h * .29f, paint);
            canvas.drawText("Live: " + (SystemClock.elapsedRealtime() - started) / 1000 + " s",
                    w * .06f, h * .41f, paint);
            paint.setColor(Color.rgb(20, 130, 100));
            canvas.drawRect(w * .25f, h * .50f, w * .75f, h * .70f, paint);
            paint.setColor(Color.WHITE);
            paint.setTextAlign(Paint.Align.CENTER);
            canvas.drawText("Pointer test: " + clicks, w * .50f, h * .62f, paint);
            paint.setTextAlign(Paint.Align.LEFT);
            canvas.drawText("Key test: " + lastKey, w * .06f, h * .85f, paint);
        }

        @Override public boolean onTouchEvent(MotionEvent event) {
            if (event.getAction() == MotionEvent.ACTION_UP
                    && event.getX() >= getWidth() * .25f && event.getX() <= getWidth() * .75f
                    && event.getY() >= getHeight() * .50f && event.getY() <= getHeight() * .70f) {
                performClick();
            }
            return true;
        }

        @Override public boolean performClick() {
            ++clicks;
            updateDescription();
            invalidate();
            super.performClick();
            return true;
        }

        private void updateDescription() {
            if (getDisplay() != null) {
                setContentDescription("UID=" + Process.myUid() + "; display="
                        + getDisplay().getDisplayId() + "; pointer=" + clicks + "; key=" + lastKey);
            }
        }
    }
}
