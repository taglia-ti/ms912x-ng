package io.github.ms912x.desktop;

import android.app.Presentation;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.Display;
import android.view.View;
import android.view.WindowManager;

/** Visible app-owned content for checking color, orientation, and partial updates. */
final class TestScreenPresentation extends Presentation {
    TestScreenPresentation(Context context, Display display) {
        super(context, display);
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(new PatternView(getContext()));
    }

    private static final class PatternView extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final long started = SystemClock.elapsedRealtime();
        private final int[] colors = {Color.WHITE, Color.YELLOW, Color.CYAN,
                Color.GREEN, Color.MAGENTA, Color.RED, Color.BLUE, Color.BLACK};

        PatternView(Context context) {
            super(context);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            int width = getWidth();
            int height = getHeight();
            canvas.drawColor(Color.rgb(12, 23, 42));
            for (int i = 0; i < colors.length; ++i) {
                paint.setColor(colors[i]);
                canvas.drawRect(i * width / 8f, height * .48f,
                        (i + 1) * width / 8f, height * .78f, paint);
            }
            paint.setColor(Color.WHITE);
            paint.setTextSize(Math.max(16, width * .05f));
            canvas.drawText("MS912x · NEW SCREEN", width * .05f, height * .16f, paint);
            paint.setTextSize(Math.max(12, width * .032f));
            canvas.drawText("TOP ↑  " + width + " × " + height,
                    width * .05f, height * .29f, paint);
            long elapsed = (SystemClock.elapsedRealtime() - started) / 1000;
            canvas.drawText("Live test · " + elapsed + " s",
                    width * .05f, height * .40f, paint);
            paint.setColor(Color.CYAN);
            float left = (elapsed % 8) * width / 8f;
            canvas.drawRect(left, height * .85f, left + width / 9f, height * .92f, paint);
            paint.setColor(Color.WHITE);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(3);
            canvas.drawRect(2, 2, width - 2, height - 2, paint);
            paint.setStyle(Paint.Style.FILL);
            postInvalidateDelayed(1000);
        }
    }
}
