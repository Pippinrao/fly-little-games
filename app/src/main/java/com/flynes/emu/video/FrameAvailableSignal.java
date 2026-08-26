package com.flynes.emu.video;

import java.util.concurrent.CopyOnWriteArrayList;

/** Emits completed core frame identities without retaining pixel storage. */
public final class FrameAvailableSignal {
    public interface Listener { void onFrameAvailable(long sequence); }
    private final CopyOnWriteArrayList<Listener> listeners = new CopyOnWriteArrayList<>();

    public void addListener(Listener listener) {
        if (listener == null) throw new IllegalArgumentException("listener is required");
        listeners.addIfAbsent(listener);
    }

    public void removeListener(Listener listener) { listeners.remove(listener); }

    public void signal(long sequence) {
        if (sequence < 0L) throw new IllegalArgumentException("sequence must be non-negative");
        for (Listener listener : listeners) listener.onFrameAvailable(sequence);
    }
}
