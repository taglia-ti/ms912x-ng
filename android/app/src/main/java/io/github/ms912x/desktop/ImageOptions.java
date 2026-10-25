package io.github.ms912x.desktop;

/** Immutable live capture policy. Zero stride and -1 tolerance follow the profile. */
final class ImageOptions {
    // IDs 1..3 match the shared Linux profiles and existing saved preferences.
    static final String[] PROFILES = {"Efficiency", "Latency", "Quality"};
    static final int[] TOLERANCES = {-1, 0, 1, 3, 7, 15, 31};
    static final ImageOptions DEFAULT = new ImageOptions(3, 0, -1);
    final int profile, stride, tolerance;
    final boolean scanEnabled;

    ImageOptions(int profile, int stride, int tolerance) {
        this(profile, stride, tolerance, true);
    }

    ImageOptions(int profile, int stride, int tolerance, boolean scanEnabled) {
        boolean bucket = false;
        for (int value : TOLERANCES) if (value == tolerance) bucket = true;
        if (profile < 1 || profile > PROFILES.length || stride < 0 || stride > 16 || !bucket)
            throw new IllegalArgumentException("Invalid image matching options");
        this.profile = profile;
        this.stride = stride;
        this.tolerance = tolerance;
        this.scanEnabled = scanEnabled;
    }

    @Override public boolean equals(Object other) {
        if (!(other instanceof ImageOptions)) return false;
        ImageOptions options = (ImageOptions) other;
        return profile == options.profile && stride == options.stride && tolerance == options.tolerance
                && scanEnabled == options.scanEnabled;
    }

    @Override public int hashCode() { return ((profile * 17 + stride) * 33 + tolerance) * 2 + (scanEnabled ? 1 : 0); }

    String summary() {
        int[] defaults = NativeCapture.profileDefaults(profile);
        int rows = stride == 0 ? defaults[1] : stride;
        int minimum = tolerance < 0 ? defaults[2] : tolerance;
        int maximum = tolerance < 0 ? defaults[3] : tolerance;
        if (!scanEnabled) return "Profile: " + PROFILES[profile - 1] + " · scan Off · full captured frames";
        return "Profile: " + PROFILES[profile - 1] + " · scan On · stride " + rows
                + " · tolerance " + (minimum == maximum ? String.valueOf(minimum) : minimum + "–" + maximum + " (adaptive)");
    }
}
