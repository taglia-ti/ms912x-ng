package io.github.ms912x.desktop;

import android.content.Context;
import android.widget.Spinner;

/** Refresh options before opening by touch, keyboard, or accessibility action. */
final class RefreshingSpinner extends Spinner {
    private final Runnable beforeOpen;

    RefreshingSpinner(Context context, Runnable beforeOpen) {
        super(context, Spinner.MODE_DROPDOWN);
        this.beforeOpen = beforeOpen;
    }

    @Override public boolean performClick() {
        if (!isEnabled()) return false;
        beforeOpen.run();
        return super.performClick();
    }
}
