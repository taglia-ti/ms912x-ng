package io.github.ms912x.helper;

import java.util.LinkedHashMap;
import java.util.Map;

public final class TopologyPlacementTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    private static void rejects(Runnable action) {
        try { action.run(); } catch (IllegalArgumentException expected) { return; }
        throw new AssertionError("Invalid arrangement accepted");
    }
    public static void main(String[] args) {
        Map<Integer, TopologyPlacement.Bounds> input = new LinkedHashMap<>();
        // Model different densities: a 1920x1080/240-dpi monitor is 1280x720 dp.
        input.put(2, new TopologyPlacement.Bounds(-100, -80, 1280, 720));
        input.put(7, new TopologyPlacement.Bounds(1180, -80, 1920, 1200));
        for (String side : new String[]{"left", "right", "top", "bottom"}) {
            Map<Integer, TopologyPlacement.Bounds> placed = TopologyPlacement.arrange(input, 7, 2, side);
            TopologyPlacement.Bounds anchor = placed.get(2), screen = placed.get(7);
            check(!anchor.overlaps(screen), "Screens overlap on " + side);
            check(side.equals("left") ? screen.left + screen.width == anchor.left
                    : side.equals("right") ? screen.left == anchor.left + anchor.width
                    : side.equals("top") ? screen.top + screen.height == anchor.top
                    : screen.top == anchor.top + anchor.height, "Missing shared edge on " + side);
            check(placed.get(2) == input.get(2) && input.get(7).left == 1180, "Input or anchor mutated");
            Map<Integer, TopologyPlacement.Bounds> translated = new LinkedHashMap<>();
            for (int id : placed.keySet()) {
                TopologyPlacement.Bounds b = placed.get(id);
                translated.put(id, new TopologyPlacement.Bounds(b.left + 300, b.top - 170, b.width, b.height));
            }
            check(TopologyPlacement.matches(placed, translated, 2), "Root translation rejected");
            translated.put(7, new TopologyPlacement.Bounds(screen.left + 301, screen.top - 170, screen.width, screen.height));
            check(!TopologyPlacement.matches(placed, translated, 2), "Unexpected display movement accepted");
        }
        rejects(() -> TopologyPlacement.arrange(input, 7, 7, "left"));
        rejects(() -> TopologyPlacement.arrange(input, 3, 2, "left"));
        rejects(() -> TopologyPlacement.arrange(input, 7, 3, "left"));
        rejects(() -> TopologyPlacement.arrange(input, 7, 2, "diagonal"));
        rejects(() -> new TopologyPlacement.Bounds(0, 0, 0, 10));
        rejects(() -> new TopologyPlacement.Bounds(Float.NaN, 0, 10, 10));
        input.put(9, new TopologyPlacement.Bounds(-1380, -80, 1280, 720));
        rejects(() -> TopologyPlacement.arrange(input, 7, 2, "left"));
        Map<Integer, TopologyPlacement.Bounds> right = TopologyPlacement.arrange(input, 7, 2, "right");
        check(right.get(9) == input.get(9), "Other monitor moved");
        check(!TopologyPlacement.matches(input, right, 2), "A changed placement matched");
        right.remove(9);
        check(!TopologyPlacement.matches(input, right, 2), "Membership change matched");
        input.clear();
        input.put(2, new TopologyPlacement.Bounds(.17f, -.31f, 1097.1428f, 617.1428f));
        input.put(7, new TopologyPlacement.Bounds(2000, 0, 853.3333f, 533.3333f));
        for (String side : new String[]{"left", "right", "top", "bottom"}) {
            Map<Integer, TopologyPlacement.Bounds> placed = TopologyPlacement.arrange(input, 7, 2, side);
            check(!placed.get(2).overlaps(placed.get(7)), "Fractional shared edge rejected on " + side);
        }
        System.out.println("PASS: four sides, mixed densities, translations, collisions and membership fences");
    }
}
