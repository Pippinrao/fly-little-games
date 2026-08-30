package com.flynes.emu.session;

import java.util.Arrays;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.Supplier;
import java.util.function.LongConsumer;

public final class EmulationSession {
    private final CoreFacade core;
    private final Executor executor;
    private final ExecutorService ownedExecutor;
    private volatile SessionState state = SessionState.EMPTY;

    public EmulationSession(CoreFacade core) {
        this.core = Objects.requireNonNull(core, "core");
        ownedExecutor = Executors.newSingleThreadExecutor(runnable -> {
            Thread thread = new Thread(runnable, "FlyNES-Session");
            thread.setDaemon(true);
            return thread;
        });
        executor = ownedExecutor;
    }

    EmulationSession(CoreFacade core, Executor executor) {
        this.core = Objects.requireNonNull(core, "core");
        this.executor = Objects.requireNonNull(executor, "executor");
        ownedExecutor = null;
    }

    public SessionState state() {
        return state;
    }

    public CompletableFuture<SessionResult> load(byte[] rom) {
        byte[] ownedRom = Arrays.copyOf(Objects.requireNonNull(rom, "rom"), rom.length);
        return submit(() -> {
            if (state != SessionState.EMPTY) {
                return illegal("load", state);
            }
            state = SessionState.LOADING;
            if (!core.create()) {
                state = SessionState.ERROR;
                return SessionResult.failure(-1, "core creation failed");
            }
            int result = core.loadRom(ownedRom);
            if (result < 0) {
                core.destroy();
                state = SessionState.ERROR;
                return SessionResult.failure(result, "ROM load failed");
            }
            state = SessionState.READY;
            return SessionResult.success();
        });
    }

    public CompletableFuture<SessionResult> start() {
        return submit(() -> {
            if (state != SessionState.READY) {
                return illegal("start", state);
            }
            state = SessionState.RUNNING;
            return SessionResult.success();
        });
    }

    public CompletableFuture<SessionResult> pause() {
        return submit(() -> {
            if (state != SessionState.RUNNING) {
                return illegal("pause", state);
            }
            core.setInput(0);
            state = SessionState.PAUSED;
            return SessionResult.success();
        });
    }

    public CompletableFuture<SessionResult> resume() {
        return submit(() -> {
            if (state != SessionState.PAUSED) {
                return illegal("resume", state);
            }
            state = SessionState.RUNNING;
            return SessionResult.success();
        });
    }

    public CompletableFuture<SessionResult> stop() {
        return submit(() -> {
            if (state == SessionState.EMPTY) {
                return SessionResult.success();
            }
            state = SessionState.STOPPING;
            core.setInput(0);
            core.destroy();
            state = SessionState.EMPTY;
            return SessionResult.success();
        });
    }

    public void setInput(int mask) {
        setInput(mask, null);
    }

    public void setInput(int mask, LongConsumer generationListener) {
        executor.execute(() -> {
            if (state == SessionState.RUNNING) {
                long generation = core.setInputVersioned(mask & 0xFF);
                if (generationListener != null && generation > 0L)
                    generationListener.accept(generation);
            }
        });
    }

    public void closeExecutor() {
        if (ownedExecutor != null) {
            ownedExecutor.shutdownNow();
        }
    }

    private CompletableFuture<SessionResult> submit(Supplier<SessionResult> transition) {
        return CompletableFuture.supplyAsync(transition, executor);
    }

    private static SessionResult illegal(String action, SessionState state) {
        return SessionResult.failure(-2, action + " is illegal from " + state);
    }
}
