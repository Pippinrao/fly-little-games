package com.flynes.emu.input;

import java.util.Collections;
import java.util.EnumMap;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;

/** Versioned layout in normalized safe-area coordinates. */
public final class ControlLayoutV2 {
    public static final int VERSION = 2;
    public enum Element { D_PAD, A, B, SELECT, START }
    public enum Direction { LANDSCAPE }

    public static final class Placement {
        private final float centerX, centerY, scale;
        public Placement(float centerX, float centerY, float scale) {
            if (!finite(centerX) || !finite(centerY) || !finite(scale)
                    || centerX < 0f || centerX > 1f || centerY < 0f || centerY > 1f
                    || scale < .5f || scale > 1.8f) throw new IllegalArgumentException("invalid placement");
            this.centerX = centerX; this.centerY = centerY; this.scale = scale;
        }
        public float centerX() { return centerX; }
        public float centerY() { return centerY; }
        public float scale() { return scale; }
        @Override public boolean equals(Object o) {
            if (!(o instanceof Placement)) return false;
            Placement p = (Placement)o;
            return Float.compare(centerX,p.centerX)==0 && Float.compare(centerY,p.centerY)==0
                    && Float.compare(scale,p.scale)==0;
        }
        @Override public int hashCode() { return Objects.hash(centerX, centerY, scale); }
    }

    private final float opacity;
    private final Direction direction;
    private final Map<Element, Placement> placements;

    private ControlLayoutV2(float opacity, Direction direction, Map<Element, Placement> placements) {
        if (!finite(opacity) || opacity < .4f || opacity > 1f || direction == null
                || placements == null || placements.size() != Element.values().length)
            throw new IllegalArgumentException("invalid layout");
        EnumMap<Element, Placement> copy = new EnumMap<>(Element.class);
        for (Element element : Element.values()) {
            Placement placement = placements.get(element);
            if (placement == null) throw new IllegalArgumentException("missing " + element);
            copy.put(element, placement);
        }
        this.opacity = opacity; this.direction = direction;
        this.placements = Collections.unmodifiableMap(copy);
    }

    public static ControlLayoutV2 recommended() {
        EnumMap<Element, Placement> values = new EnumMap<>(Element.class);
        values.put(Element.D_PAD, new Placement(.09f, .78f, 1f));
        values.put(Element.A, new Placement(.94f, .64f, 1f));
        values.put(Element.B, new Placement(.87f, .86f, 1f));
        values.put(Element.SELECT, new Placement(.09f, .28f, 1f));
        values.put(Element.START, new Placement(.94f, .28f, 1f));
        return new ControlLayoutV2(.52f, Direction.LANDSCAPE, values);
    }

    public float opacity() { return opacity; }
    public Direction direction() { return direction; }
    public Placement placement(Element element) { return placements.get(element); }
    public Map<Element, Placement> placements() { return placements; }
    public ControlLayoutV2 move(Element element, float x, float y) {
        EnumMap<Element, Placement> copy = new EnumMap<>(placements);
        Placement p = placement(element); copy.put(element, new Placement(x,y,p.scale));
        return new ControlLayoutV2(opacity,direction,copy);
    }
    public ControlLayoutV2 resize(Element element, float scale) {
        EnumMap<Element, Placement> copy = new EnumMap<>(placements);
        Placement p = placement(element); copy.put(element,new Placement(p.centerX,p.centerY,scale));
        return new ControlLayoutV2(opacity,direction,copy);
    }
    public ControlLayoutV2 withOpacity(float value) { return new ControlLayoutV2(value,direction,placements); }

    public String encode() {
        StringBuilder out = new StringBuilder("v2|").append(f(opacity)).append('|').append(direction.name());
        for (Element element : Element.values()) {
            Placement p = placement(element);
            out.append('|').append(element.name()).append(',').append(f(p.centerX))
                    .append(',').append(f(p.centerY)).append(',').append(f(p.scale));
        }
        return out.toString();
    }
    public static ControlLayoutV2 decode(String value) {
        if (value == null) throw new IllegalArgumentException("missing layout");
        String[] parts = value.split("\\|", -1);
        if (parts.length != 8 || !"v2".equals(parts[0])) throw new IllegalArgumentException("unknown layout version");
        float opacity = parse(parts[1]);
        Direction direction = Direction.valueOf(parts[2]);
        EnumMap<Element, Placement> placements = new EnumMap<>(Element.class);
        for (int i=3;i<parts.length;i++) {
            String[] fields=parts[i].split(",",-1);
            if(fields.length!=4) throw new IllegalArgumentException("invalid placement");
            Element e=Element.valueOf(fields[0]);
            if(placements.put(e,new Placement(parse(fields[1]),parse(fields[2]),parse(fields[3])))!=null)
                throw new IllegalArgumentException("duplicate placement");
        }
        return new ControlLayoutV2(opacity,direction,placements);
    }
    public static ControlLayoutV2 decodeOrRecommended(String value) {
        try { return decode(value); } catch (RuntimeException ignored) { return recommended(); }
    }
    private static float parse(String value) {
        float parsed=Float.parseFloat(value);
        if(!finite(parsed)) throw new IllegalArgumentException("non-finite");
        return parsed;
    }
    private static boolean finite(float value) { return !Float.isNaN(value) && !Float.isInfinite(value); }
    private static String f(float value) { return String.format(Locale.US,"%.4f",value); }
    @Override public boolean equals(Object o) {
        if(!(o instanceof ControlLayoutV2)) return false; ControlLayoutV2 l=(ControlLayoutV2)o;
        return Float.compare(opacity,l.opacity)==0 && direction==l.direction && placements.equals(l.placements);
    }
    @Override public int hashCode(){return Objects.hash(opacity,direction,placements);}
}
