package com.flynes.emu.input;

import static org.junit.Assert.assertEquals;

import java.util.ArrayList;
import java.util.List;

import org.junit.Test;

public final class InputRouterTest {
    @Test
    public void mergesSourcesAndCancelAllPublishesZero() {
        List<Integer> masks = new ArrayList<>();
        InputRouter router = new InputRouter(masks::add, action -> { });

        router.setMask(InputRouter.Source.TOUCH, InputBits.RIGHT);
        router.setMask(InputRouter.Source.GAMEPAD, InputBits.A);

        assertEquals(InputBits.RIGHT | InputBits.A, router.currentMask());
        router.cancelAll();
        assertEquals(Integer.valueOf(0), masks.get(masks.size() - 1));
    }

    @Test
    public void appPauseNeverGeneratesNesStart() {
        List<InputRouter.AppAction> actions = new ArrayList<>();
        InputRouter router = new InputRouter(mask -> { }, actions::add);

        router.dispatch(InputRouter.AppAction.OPEN_PAUSE);

        assertEquals(0, router.currentMask());
        assertEquals(List.of(InputRouter.AppAction.OPEN_PAUSE), actions);
    }

    @Test
    public void cancelingTouchPreservesControllerInput() {
        InputRouter router = new InputRouter(mask -> { }, action -> { });
        router.setMask(InputRouter.Source.TOUCH, InputBits.B);
        router.setMask(InputRouter.Source.GAMEPAD, InputBits.LEFT);

        router.cancel(InputRouter.Source.TOUCH);

        assertEquals(InputBits.LEFT, router.currentMask());
    }
}
