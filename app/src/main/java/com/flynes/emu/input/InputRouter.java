package com.flynes.emu.input;

import java.util.EnumMap;
import java.util.Objects;
import java.util.function.Consumer;
import java.util.function.IntConsumer;

public final class InputRouter {
    public interface TimestampedInputConsumer {
        void accept(int mask, long eventElapsedRealtimeNs);
    }
    public enum Source { TOUCH, KEYBOARD, GAMEPAD, ACCESSIBILITY }
    public enum AppAction { OPEN_PAUSE, CLOSE_PAUSE, OPEN_LIBRARY, OPEN_SETTINGS }

    private final EnumMap<Source, Integer> masks = new EnumMap<>(Source.class);
    private final TimestampedInputConsumer nesListener;
    private final Consumer<AppAction> appListener;
    private int currentMask;

    public InputRouter(IntConsumer nesListener, Consumer<AppAction> appListener) {
        this((mask, eventTime) -> Objects.requireNonNull(nesListener,
                "nesListener").accept(mask), appListener);
    }

    private InputRouter(TimestampedInputConsumer nesListener, Consumer<AppAction> appListener) {
        this.nesListener = Objects.requireNonNull(nesListener, "nesListener");
        this.appListener = Objects.requireNonNull(appListener, "appListener");
        for (Source source : Source.values()) {
            masks.put(source, 0);
        }
    }

    public synchronized void setMask(Source source, int mask) {
        setMask(source, mask, System.nanoTime());
    }

    public static InputRouter timestamped(TimestampedInputConsumer nesListener,
                                          Consumer<AppAction> appListener) {
        return new InputRouter(nesListener, appListener);
    }

    public synchronized void setMask(Source source, int mask, long eventElapsedRealtimeNs) {
        masks.put(Objects.requireNonNull(source, "source"), mask & 0xFF);
        int merged = 0;
        for (int value : masks.values()) {
            merged |= value;
        }
        if (merged != currentMask) {
            currentMask = merged;
            nesListener.accept(merged, eventElapsedRealtimeNs);
        }
    }

    public synchronized int currentMask() {
        return currentMask;
    }

    public void cancel(Source source) {
        setMask(source, 0);
    }

    public synchronized void cancelAll() {
        for (Source source : Source.values()) {
            masks.put(source, 0);
        }
        currentMask = 0;
        nesListener.accept(0, System.nanoTime());
    }

    public void dispatch(AppAction action) {
        appListener.accept(Objects.requireNonNull(action, "action"));
    }
}
