package com.flynes.emu.input;

import static org.junit.Assert.*;

import org.junit.Test;

public final class ControlLayoutV2Test {
    @Test public void codecRoundTripsDeterministically() {
        ControlLayoutV2 layout = ControlLayoutV2.recommended().move(
                ControlLayoutV2.Element.A, .91f, .55f).withOpacity(.63f);
        assertEquals(layout, ControlLayoutV2.decode(layout.encode()));
        assertEquals(layout.encode(), ControlLayoutV2.decode(layout.encode()).encode());
    }

    @Test public void malformedOrOldPayloadMigratesToVerifiedRecommendedLayout() {
        assertEquals(ControlLayoutV2.recommended(), ControlLayoutV2.decodeOrRecommended("v1|JOY"));
        assertEquals(ControlLayoutV2.recommended(), ControlLayoutV2.decodeOrRecommended("v2|NaN"));
        assertEquals(ControlLayoutV2.recommended(), ControlLayoutV2.decodeOrRecommended(null));
    }

    @Test public void warningsIdentifySmallOverlapGestureAndCentralProtection() {
        ControlLayoutV2 layout = ControlLayoutV2.recommended()
                .resize(ControlLayoutV2.Element.A, .5f)
                .move(ControlLayoutV2.Element.A, .50f, .50f)
                .move(ControlLayoutV2.Element.B, .50f, .50f)
                .move(ControlLayoutV2.Element.START, .99f, .50f);
        ControlLayoutWarnings warnings = ControlLayoutWarnings.inspect(layout);
        assertTrue(warnings.contains(ControlLayoutWarnings.Code.TOO_SMALL));
        assertTrue(warnings.contains(ControlLayoutWarnings.Code.OVERLAP));
        assertTrue(warnings.contains(ControlLayoutWarnings.Code.GESTURE_ZONE));
        assertTrue(warnings.contains(ControlLayoutWarnings.Code.CENTRAL_PROTECTION));
    }

    @Test public void hitMapAppliesNormalizedPositionsAndScale() {
        ControlLayoutV2 layout = ControlLayoutV2.recommended()
                .move(ControlLayoutV2.Element.A, .75f, .40f)
                .resize(ControlLayoutV2.Element.A, 1.2f);
        GamepadHitMap map = GamepadHitMap.fromLayout(2000, 1000, 2f, 100, 100, 20, 20, layout);
        assertEquals(100 + .75f * 1800f, map.target(GamepadHitMap.Control.A).centerX(), .1f);
        assertEquals(20 + .40f * 960f, map.target(GamepadHitMap.Control.A).centerY(), .1f);
        assertEquals(72f * 2f * 1.2f, map.target(GamepadHitMap.Control.A).width(), .1f);
    }

    @Test public void recommendedAndExtremeLayoutsRemainInsideAllSafeMatrices() {
        int[][] matrices={{1920,1080,0,0},{2340,1080,0,132},{1280,720,40,40},{1433,1080,0,0}};
        for(int[] m:matrices){
            GamepadHitMap map=GamepadHitMap.fromLayout(m[0],m[1],2f,m[2],m[3],0,0,ControlLayoutV2.recommended());
            for(GamepadHitMap.Target target:map.controls()){
                assertTrue(target.control()+" left",target.left()>=m[2]-.1f);
                assertTrue(target.control()+" right",target.right()<=m[0]-m[3]+.1f);
                assertTrue(target.control()+" top",target.top()>=-.1f);
                assertTrue(target.control()+" bottom",target.bottom()<=m[1]+.1f);
            }
        }
    }
}
