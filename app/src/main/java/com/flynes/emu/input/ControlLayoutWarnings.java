package com.flynes.emu.input;

import java.util.Collections;
import java.util.EnumSet;
import java.util.Set;

public final class ControlLayoutWarnings {
    public enum Code { TOO_SMALL, OVERLAP, GESTURE_ZONE, CENTRAL_PROTECTION }
    private final Set<Code> codes;
    private ControlLayoutWarnings(Set<Code> codes) { this.codes=Collections.unmodifiableSet(codes); }
    public boolean contains(Code code){return codes.contains(code);}
    public boolean isEmpty(){return codes.isEmpty();}
    public Set<Code> codes(){return codes;}
    public static ControlLayoutWarnings inspect(ControlLayoutV2 layout) {
        EnumSet<Code> result=EnumSet.noneOf(Code.class);
        for(ControlLayoutV2.Element e:ControlLayoutV2.Element.values()) {
            ControlLayoutV2.Placement p=layout.placement(e);
            if(p.scale()<.67f) result.add(Code.TOO_SMALL);
            if(p.centerX()<.04f||p.centerX()>.96f||p.centerY()<.06f||p.centerY()>.94f) result.add(Code.GESTURE_ZONE);
            if(p.centerX()>.15f&&p.centerX()<.85f) result.add(Code.CENTRAL_PROTECTION);
        }
        ControlLayoutV2.Element[] values=ControlLayoutV2.Element.values();
        for(int i=0;i<values.length;i++) for(int j=i+1;j<values.length;j++) {
            ControlLayoutV2.Placement a=layout.placement(values[i]), b=layout.placement(values[j]);
            float dx=a.centerX()-b.centerX(),dy=a.centerY()-b.centerY();
            float radius=.055f*(a.scale()+b.scale());
            if(dx*dx+dy*dy<radius*radius) result.add(Code.OVERLAP);
        }
        return new ControlLayoutWarnings(result);
    }
}
