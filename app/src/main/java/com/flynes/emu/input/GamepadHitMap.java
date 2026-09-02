package com.flynes.emu.input;

import com.flynes.emu.settings.AppSettings;

import java.util.ArrayList;
import java.util.Collections;
import java.util.EnumMap;
import java.util.List;
import java.util.Map;

/** Ergonomic, immutable landscape hit map. Visual size and touch target are intentionally separate. */
public final class GamepadHitMap {
    public enum Control { NONE, UP, DOWN, LEFT, RIGHT, B, A, SELECT, START, PAUSE }
    public enum Shape { CIRCLE, ROUNDED_SQUARE, PILL }

    private static final Control[] BUTTON_CONTROLS = {
            Control.A, Control.B, Control.SELECT, Control.START
    };

    public static final class Bounds {
        public final float left, top, right, bottom;
        Bounds(float left, float top, float right, float bottom) {
            this.left = left; this.top = top; this.right = right; this.bottom = bottom;
        }
        public float centerX() { return (left + right) / 2f; }
        public float centerY() { return (top + bottom) / 2f; }
        public float width() { return right - left; }
        public float height() { return bottom - top; }
        boolean contains(float x, float y) { return x >= left && x <= right && y >= top && y <= bottom; }
        boolean contains(Bounds value) { return value.left >= left && value.top >= top && value.right <= right && value.bottom <= bottom; }
        Bounds expanded(float amount) { return new Bounds(left - amount, top - amount, right + amount, bottom + amount); }
    }

    public static final class Target {
        private final Control control;
        private final Bounds bounds;
        private final Shape shape;

        Target(Control control, Bounds bounds, Shape shape) {
            this.control = control;
            this.bounds = bounds;
            this.shape = shape;
        }
        public Control control() { return control; }
        public Shape shape() { return shape; }
        public Bounds bounds() { return bounds; }
        public float left() { return bounds.left; }
        public float top() { return bounds.top; }
        public float right() { return bounds.right; }
        public float bottom() { return bounds.bottom; }
        public float centerX() { return bounds.centerX(); }
        public float centerY() { return bounds.centerY(); }
        public float width() { return bounds.width(); }
        public float height() { return bounds.height(); }
        boolean contains(float x, float y) { return bounds.contains(x, y); }
    }

    private final Bounds safeBounds;
    private final Bounds dpadBounds;
    private final float density;
    private final DirectionControlMode directionMode;
    private final float deadZone;
    private final Map<Control, Target> targets;
    private final List<Target> controls;

    private GamepadHitMap(Bounds safeBounds, Bounds dpadBounds, float density,
                          DirectionControlMode directionMode, float deadZone,
                          Map<Control, Target> targets) {
        this.safeBounds = safeBounds;
        this.dpadBounds = dpadBounds;
        this.density = density;
        this.directionMode = directionMode;
        this.deadZone = deadZone;
        this.targets = Collections.unmodifiableMap(new EnumMap<>(targets));
        this.controls = Collections.unmodifiableList(new ArrayList<>(targets.values()));
    }

    public static GamepadHitMap standard(int width, int height, float density,
                                         int insetLeft, int insetRight,
                                         int insetTop, int insetBottom) {
        float safeLeft = insetLeft;
        float safeTop = insetTop;
        float safeRight = width - insetRight;
        float safeBottom = height - insetBottom;
        float dpadSize = 144f * density;
        float dpadLeft = safeLeft + 16f * density;
        float dpadTop = safeBottom - 16f * density - dpadSize;
        Bounds dpad = new Bounds(dpadLeft, dpadTop, dpadLeft + dpadSize, dpadTop + dpadSize);

        EnumMap<Control, Target> values = new EnumMap<>(Control.class);
        float arm = 48f * density;
        values.put(Control.UP, new Target(Control.UP,
                new Bounds(dpad.centerX() - arm / 2f, dpad.top,
                        dpad.centerX() + arm / 2f, dpad.centerY()), Shape.ROUNDED_SQUARE));
        values.put(Control.DOWN, new Target(Control.DOWN,
                new Bounds(dpad.centerX() - arm / 2f, dpad.centerY(),
                        dpad.centerX() + arm / 2f, dpad.bottom), Shape.ROUNDED_SQUARE));
        values.put(Control.LEFT, new Target(Control.LEFT,
                new Bounds(dpad.left, dpad.centerY() - arm / 2f,
                        dpad.centerX(), dpad.centerY() + arm / 2f), Shape.ROUNDED_SQUARE));
        values.put(Control.RIGHT, new Target(Control.RIGHT,
                new Bounds(dpad.centerX(), dpad.centerY() - arm / 2f,
                        dpad.right, dpad.centerY() + arm / 2f), Shape.ROUNDED_SQUARE));

        float aSize = 72f * density;
        float bSize = 64f * density;
        float aCx = safeRight - 52f * density;
        float aCy = safeBottom - 132f * density;
        float bCx = aCx - 88f * density;
        float bCy = safeBottom - 48f * density;
        values.put(Control.A, centered(Control.A, aCx, aCy, aSize, aSize, Shape.CIRCLE));
        values.put(Control.B, centered(Control.B, bCx, bCy, bSize, bSize, Shape.ROUNDED_SQUARE));

        float pillW = 72f * density;
        float pillH = 48f * density;
        float systemY = Math.max(safeTop + pillH / 2f + 8f * density,
                dpad.top - 16f * density - pillH / 2f);
        values.put(Control.SELECT, centered(Control.SELECT, dpad.centerX(), systemY,
                pillW, pillH, Shape.PILL));
        values.put(Control.START, centered(Control.START, safeRight - 43f * density,
                systemY, pillW, pillH, Shape.PILL));

        GamepadHitMap map = new GamepadHitMap(
                new Bounds(safeLeft, safeTop, safeRight, safeBottom), dpad, density,
                DirectionControlMode.DPAD, .22f, values);
        List<String> errors = map.validate();
        if (!errors.isEmpty()) throw new IllegalArgumentException(join(errors));
        return map;
    }

    public static GamepadHitMap fromSettings(int width, int height, float density,
                                             int insetLeft, int insetRight,
                                             int insetTop, int insetBottom,
                                             AppSettings ignored) {
        return standard(width, height, density, insetLeft, insetRight, insetTop, insetBottom);
    }

    public static GamepadHitMap fromLayout(int width, int height, float density,
                                           int insetLeft, int insetRight,
                                           int insetTop, int insetBottom,
                                           ControlLayoutV2 layout) {
        return fromLayout(width, height, density, insetLeft, insetRight, insetTop,
                insetBottom, layout, DirectionControlMode.DPAD, .22f);
    }

    public static GamepadHitMap fromLayout(int width, int height, float density,
                                           int insetLeft, int insetRight,
                                           int insetTop, int insetBottom,
                                           ControlLayoutV2 layout,
                                           DirectionControlMode directionMode,
                                           float deadZone) {
        if (directionMode == null || !Float.isFinite(deadZone)
                || deadZone < .08f || deadZone > .45f) {
            throw new IllegalArgumentException("invalid direction control settings");
        }
        float safeLeft=insetLeft, safeTop=insetTop, safeRight=width-insetRight, safeBottom=height-insetBottom;
        float safeWidth=safeRight-safeLeft, safeHeight=safeBottom-safeTop;
        ControlLayoutV2.Placement d=layout.placement(ControlLayoutV2.Element.D_PAD);
        float dSize=(directionMode==DirectionControlMode.DPAD?144f:128f)*density*d.scale();
        float dCx=clamp(safeLeft+d.centerX()*safeWidth,safeLeft+dSize/2f,safeRight-dSize/2f);
        float dCy=clamp(safeTop+d.centerY()*safeHeight,safeTop+dSize/2f,safeBottom-dSize/2f);
        Bounds dpad=new Bounds(dCx-dSize/2f,dCy-dSize/2f,dCx+dSize/2f,dCy+dSize/2f);
        EnumMap<Control,Target> values=new EnumMap<>(Control.class);
        float arm=48f*density*d.scale();
        values.put(Control.UP,new Target(Control.UP,new Bounds(dpad.centerX()-arm/2f,dpad.top,dpad.centerX()+arm/2f,dpad.centerY()),Shape.ROUNDED_SQUARE));
        values.put(Control.DOWN,new Target(Control.DOWN,new Bounds(dpad.centerX()-arm/2f,dpad.centerY(),dpad.centerX()+arm/2f,dpad.bottom),Shape.ROUNDED_SQUARE));
        values.put(Control.LEFT,new Target(Control.LEFT,new Bounds(dpad.left,dpad.centerY()-arm/2f,dpad.centerX(),dpad.centerY()+arm/2f),Shape.ROUNDED_SQUARE));
        values.put(Control.RIGHT,new Target(Control.RIGHT,new Bounds(dpad.centerX(),dpad.centerY()-arm/2f,dpad.right,dpad.centerY()+arm/2f),Shape.ROUNDED_SQUARE));
        addLayoutTarget(values,Control.A,ControlLayoutV2.Element.A,72f,72f,Shape.CIRCLE,layout,safeLeft,safeTop,safeWidth,safeHeight,density);
        addLayoutTarget(values,Control.B,ControlLayoutV2.Element.B,64f,64f,Shape.ROUNDED_SQUARE,layout,safeLeft,safeTop,safeWidth,safeHeight,density);
        addLayoutTarget(values,Control.SELECT,ControlLayoutV2.Element.SELECT,72f,48f,Shape.PILL,layout,safeLeft,safeTop,safeWidth,safeHeight,density);
        addLayoutTarget(values,Control.START,ControlLayoutV2.Element.START,72f,48f,Shape.PILL,layout,safeLeft,safeTop,safeWidth,safeHeight,density);
        return new GamepadHitMap(new Bounds(safeLeft,safeTop,safeRight,safeBottom),dpad,density,
                directionMode,deadZone,values);
    }

    private static void addLayoutTarget(EnumMap<Control,Target> values,Control control,
                                        ControlLayoutV2.Element element,float baseWidth,float baseHeight,
                                        Shape shape,ControlLayoutV2 layout,float left,float top,
                                        float width,float height,float density) {
        ControlLayoutV2.Placement p=layout.placement(element);
        float targetWidth=baseWidth*density*p.scale(),targetHeight=baseHeight*density*p.scale();
        float cx=clamp(left+p.centerX()*width,left+targetWidth/2f,left+width-targetWidth/2f);
        float cy=clamp(top+p.centerY()*height,top+targetHeight/2f,top+height-targetHeight/2f);
        values.put(control,centered(control,cx,cy,targetWidth,targetHeight,shape));
    }

    private static float clamp(float value,float minimum,float maximum){return Math.max(minimum,Math.min(maximum,value));}

    private static Target centered(Control control, float cx, float cy, float width,
                                   float height, Shape shape) {
        return new Target(control,
                new Bounds(cx - width / 2f, cy - height / 2f, cx + width / 2f, cy + height / 2f),
                shape);
    }

    public Target target(Control control) {
        Target target = targets.get(control);
        if (target == null) throw new IllegalArgumentException(control.name());
        return target;
    }
    public List<Target> controls() { return controls; }
    public Bounds dpadBounds() { return dpadBounds; }
    public DirectionControlMode directionMode() { return directionMode; }
    public float deadZone() { return deadZone; }

    /** True for either visual joystick style. */
    public boolean joystickMode() { return directionMode != DirectionControlMode.DPAD; }

    /** True only for the legacy moving/following joystick. */
    public boolean followingJoystickMode() {
        return directionMode == DirectionControlMode.JOYSTICK;
    }

    public boolean fixedJoystickMode() {
        return directionMode == DirectionControlMode.FIXED_JOYSTICK;
    }

    public Control buttonHit(float x, float y) {
        for (Control control : BUTTON_CONTROLS) {
            if (target(control).contains(x, y)) return control;
        }
        return Control.NONE;
    }

    public boolean canStartJoystick(float x, float y) {
        if (followingJoystickMode()) {
            return safeBounds.contains(x, y) && x < safeBounds.centerX();
        }
        if (!fixedJoystickMode()) return false;
        float dx = x - dpadBounds.centerX();
        float dy = y - dpadBounds.centerY();
        float captureRadius = joystickRadius() + 16f * density;
        return dx * dx + dy * dy <= captureRadius * captureRadius;
    }

    public boolean canStartDirection(float x, float y) {
        if (joystickMode()) return canStartJoystick(x, y);
        return dpadBounds.expanded(16f * density).contains(x, y);
    }

    public float joystickRadius() {
        return Math.min(dpadBounds.width(), dpadBounds.height()) / 2f;
    }

    public float joystickTravelRadius() { return joystickRadius() * .5625f; }

    public float clampJoystickCenterX(float x) {
        float minimum = safeBounds.left + joystickRadius();
        float maximum = Math.max(minimum, safeBounds.centerX() - joystickRadius());
        return clamp(x, minimum, maximum);
    }

    public float clampJoystickCenterY(float y) {
        float minimum = safeBounds.top + joystickRadius();
        float maximum = Math.max(minimum, safeBounds.bottom - joystickRadius());
        return clamp(y, minimum, maximum);
    }

    public Control hit(float x, float y) {
        Control button = buttonHit(x, y);
        if (button != Control.NONE) return button;
        if (joystickMode()) {
            float dx = x - dpadBounds.centerX();
            float dy = y - dpadBounds.centerY();
            float captureRadius = fixedJoystickMode()
                    ? joystickRadius() + 16f * density : joystickRadius() * 1.18f;
            if (Math.hypot(dx, dy) > captureRadius) return Control.NONE;
        }
        int direction = directionBits(x, y, 0);
        if (direction == InputBits.UP) return Control.UP;
        if (direction == InputBits.DOWN) return Control.DOWN;
        if (direction == InputBits.LEFT) return Control.LEFT;
        if (direction == InputBits.RIGHT) return Control.RIGHT;
        return Control.NONE;
    }

    /** Returns one or two adjacent directions. The small release margin prevents edge chatter. */
    public int directionBits(float x, float y, int previousBits) {
        if (joystickMode()) {
            return joystickDirectionBits(
                    dpadBounds.centerX(), dpadBounds.centerY(), x, y, previousBits);
        }
        float release = previousBits == 0 ? 0f : 8f * density;
        Bounds active = dpadBounds.expanded(release);
        if (!active.contains(x, y)) return 0;
        float dx = x - dpadBounds.centerX();
        float dy = y - dpadBounds.centerY();
        float threshold = (previousBits == 0 ? 18f : 12f) * density;
        int bits = 0;
        if (dx <= -threshold) bits |= InputBits.LEFT;
        else if (dx >= threshold) bits |= InputBits.RIGHT;
        if (dy <= -threshold) bits |= InputBits.UP;
        else if (dy >= threshold) bits |= InputBits.DOWN;
        return bits;
    }

    public int joystickDirectionBits(float centerX, float centerY,
                                     float x, float y, int previousBits) {
        float dx = x - centerX;
        float dy = y - centerY;
        float normalized = (float) Math.hypot(dx, dy) / joystickRadius();
        float threshold = previousBits == 0 ? deadZone : Math.max(.08f, deadZone - .06f);
        if (normalized < threshold) return 0;
        float ax = Math.abs(dx);
        float ay = Math.abs(dy);
        int bits = 0;
        if (ax >= ay * .55f) bits |= dx < 0 ? InputBits.LEFT : InputBits.RIGHT;
        if (ay >= ax * .55f) bits |= dy < 0 ? InputBits.UP : InputBits.DOWN;
        return bits;
    }

    public float distanceBetween(Target first, Target second) {
        float dx = first.centerX() - second.centerX();
        float dy = first.centerY() - second.centerY();
        float centerDistance = (float) Math.sqrt(dx * dx + dy * dy);
        return Math.max(0f, centerDistance - Math.max(first.width(), first.height()) / 2f
                - Math.max(second.width(), second.height()) / 2f);
    }

    public List<String> validate() {
        List<String> errors = new ArrayList<>();
        for (Target target : controls) {
            if (!safeBounds.contains(target.bounds)) errors.add(target.control + " outside safe rect");
        }
        if (distanceBetween(target(Control.A), target(Control.B)) < 24f * density) {
            errors.add("A and B too close");
        }
        return Collections.unmodifiableList(errors);
    }

    private static String join(List<String> values) {
        StringBuilder result = new StringBuilder();
        for (String value : values) {
            if (result.length() > 0) result.append("; ");
            result.append(value);
        }
        return result.toString();
    }
}
