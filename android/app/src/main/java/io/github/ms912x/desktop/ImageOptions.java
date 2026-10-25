package io.github.ms912x.desktop;

/** Immutable live capture policy. Zero stride and -1 tolerance follow the profile. */
final class ImageOptions {
    static final String[] PROFILES = {"Auto", "Efficiency", "Latency", "Quality"};
    static final int[] TOLERANCES = {-1, 0, 1, 3, 7, 15, 31};
    static final ImageOptions DEFAULT = new ImageOptions(0, 0, -1);
    final int profile, stride, tolerance;

    ImageOptions(int profile, int stride, int tolerance) {
        boolean bucket = false;
        for (int value : TOLERANCES) if (value == tolerance) bucket = true;
        if (profile < 0 || profile >= PROFILES.length || stride < 0 || stride > 16 || !bucket)
            throw new IllegalArgumentException("Invalid image matching options");
        this.profile = profile;
        this.stride = stride;
        this.tolerance = tolerance;
    }

    @Override public boolean equals(Object other) {
        if (!(other instanceof ImageOptions)) return false;
        ImageOptions options = (ImageOptions) other;
        return profile == options.profile && stride == options.stride && tolerance == options.tolerance;
    }

    @Override public int hashCode() { return (profile * 17 + stride) * 33 + tolerance; }

    String summary() {
        int[] defaults = NativeCapture.profileDefaults(profile);
        int rows = stride == 0 ? defaults[1] : stride;
        int minimum = tolerance < 0 ? defaults[2] : tolerance;
        int maximum = tolerance < 0 ? defaults[3] : tolerance;
        return (profile == 0 ? "Auto uses Quality" : "Profile: " + PROFILES[profile]) + " · stride " + rows
                + " · tolerance " + (minimum == maximum ? String.valueOf(minimum) : minimum + "–" + maximum + " (adaptive)");
    }
}
