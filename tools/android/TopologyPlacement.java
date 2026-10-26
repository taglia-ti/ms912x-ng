package io.github.taglia_ti.ms912x_ng.helper;

import java.util.LinkedHashMap;
import java.util.Map;

/** Geometry for the standalone native mouse-topology experiment, in display-independent pixels. */
final class TopologyPlacement {
    static final class Bounds {
        final float left, top, width, height;
        Bounds(float left, float top, float width, float height) {
            if (!Float.isFinite(left) || !Float.isFinite(top) || !Float.isFinite(width)
                    || !Float.isFinite(height) || width <= 0 || height <= 0) {
                throw new IllegalArgumentException("Invalid topology bounds");
            }
            this.left = left; this.top = top; this.width = width; this.height = height;
        }
        boolean overlaps(Bounds other) {
            // Fractional density conversions can round a shared edge differently.
            return left < other.left + other.width - .05f && other.left < left + width - .05f
                    && top < other.top + other.height - .05f && other.top < top + height - .05f;
        }
    }

    static Map<Integer, Bounds> arrange(Map<Integer, Bounds> current, int screen, int anchor, String side) {
        if (screen == anchor || !current.containsKey(screen) || !current.containsKey(anchor)) {
            throw new IllegalArgumentException("Both distinct displays must already belong to the native topology");
        }
        Bounds target = current.get(screen), reference = current.get(anchor);
        float x = reference.left + (reference.width - target.width) / 2;
        float y = reference.top + (reference.height - target.height) / 2;
        switch (side) {
            case "left": x = reference.left - target.width; break;
            case "right": x = reference.left + reference.width; break;
            case "top": y = reference.top - target.height; break;
            case "bottom": y = reference.top + reference.height; break;
            default: throw new IllegalArgumentException("Position must be left, right, top or bottom");
        }
        Bounds next = new Bounds(x, y, target.width, target.height);
        for (Map.Entry<Integer, Bounds> entry : current.entrySet()) {
            if (entry.getKey() != screen && next.overlaps(entry.getValue())) {
                throw new IllegalArgumentException("Requested position overlaps display " + entry.getKey());
            }
        }
        Map<Integer, Bounds> result = new LinkedHashMap<>(current);
        result.put(screen, next);
        return result;
    }

    /** Android may translate the whole arrangement when normalizing its root. */
    static boolean matches(Map<Integer, Bounds> expected, Map<Integer, Bounds> actual, int anchor) {
        if (!expected.keySet().equals(actual.keySet()) || !expected.containsKey(anchor)) return false;
        Bounds expectedAnchor = expected.get(anchor), actualAnchor = actual.get(anchor);
        for (int id : expected.keySet()) {
            Bounds a = expected.get(id), b = actual.get(id);
            if (Math.abs(a.width - b.width) > .05f || Math.abs(a.height - b.height) > .05f
                    || Math.abs((a.left - expectedAnchor.left) - (b.left - actualAnchor.left)) > .05f
                    || Math.abs((a.top - expectedAnchor.top) - (b.top - actualAnchor.top)) > .05f) return false;
        }
        return true;
    }
    private TopologyPlacement() {}
}
