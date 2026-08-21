package com.flynes.emu.input;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public final class GamepadHitMap {
    public enum Control { NONE, JOY, B, A, SELECT, START }

    public static final class Circle {
        private final Control control;
        private final float cx;
        private final float cy;
        private final float radius;

        public Circle(Control control, float cx, float cy, float radius) {
            this.control = control;
            this.cx = cx;
            this.cy = cy;
            this.radius = radius;
        }

        public Control control() { return control; }
        public float cx() { return cx; }
        public float cy() { return cy; }
        public float radius() { return radius; }

        boolean contains(float x, float y) {
            float dx = x - cx;
            float dy = y - cy;
            return dx * dx + dy * dy <= radius * radius;
        }

        boolean overlaps(Circle other) {
            float dx = cx - other.cx;
            float dy = cy - other.cy;
            float sum = radius + other.radius;
            return dx * dx + dy * dy < sum * sum;
        }
    }

    private final float left;
    private final float top;
    private final float right;
    private final float bottom;
    private final List<Circle> circles;

    private GamepadHitMap(float left, float top, float right, float bottom,
                          List<Circle> circles) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
        this.circles = Collections.unmodifiableList(new ArrayList<>(circles));
    }

    public static GamepadHitMap standard(int width, int height, float density,
                                         int insetLeft, int insetRight,
                                         int insetTop, int insetBottom) {
        float safeRight = width - insetRight;
        float controlBottom = height - insetBottom - 24f * density;
        float actionRadius = 28f * density;
        Circle joy = new Circle(Control.JOY, insetLeft + 100f * density,
                controlBottom - 76f * density, 76f * density);
        Circle select = new Circle(Control.SELECT, width / 2f - 28f * density,
                controlBottom - 24f * density, 24f * density);
        Circle start = new Circle(Control.START, width / 2f + 28f * density,
                controlBottom - 24f * density, 24f * density);
        Circle b = new Circle(Control.B, safeRight - 104f * density,
                controlBottom - actionRadius, actionRadius);
        Circle a = new Circle(Control.A, safeRight - 32f * density,
                controlBottom - actionRadius, actionRadius);

        GamepadHitMap map = new GamepadHitMap(insetLeft, insetTop,
                width - insetRight, height - insetBottom,
                List.of(joy, select, start, b, a));
        List<String> errors = map.validate();
        if (!errors.isEmpty()) {
            throw new IllegalArgumentException(String.join("; ", errors));
        }
        return map;
    }

    public Control hit(float x, float y) {
        for (Circle circle : circles) {
            if (circle.contains(x, y)) {
                return circle.control;
            }
        }
        return Control.NONE;
    }

    public Circle circle(Control control) {
        for (Circle circle : circles) {
            if (circle.control == control) {
                return circle;
            }
        }
        throw new IllegalArgumentException(control.name());
    }

    public List<Circle> circles() {
        return circles;
    }

    public List<String> validate() {
        List<String> errors = new ArrayList<>();
        for (int i = 0; i < circles.size(); i++) {
            Circle first = circles.get(i);
            if (first.cx - first.radius < left || first.cx + first.radius > right
                    || first.cy - first.radius < top || first.cy + first.radius > bottom) {
                errors.add(first.control + " outside safe rect");
            }
            for (int j = i + 1; j < circles.size(); j++) {
                Circle second = circles.get(j);
                if (first.overlaps(second)) {
                    errors.add(first.control + " overlaps " + second.control);
                }
            }
        }
        return Collections.unmodifiableList(errors);
    }
}
