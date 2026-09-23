package com.flynes.emu.cover;

import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.Function;

/** Coalesces concurrent loads while giving each caller an independently cancellable view. */
public final class CoverLoadCoordinator<T> implements AutoCloseable {
    private final Function<String, T> memoryLookup;
    private final Function<String, T> decoder;
    private final Executor executor;
    private final Runnable closeAction;
    private final ConcurrentHashMap<String, CompletableFuture<T>> inFlight =
            new ConcurrentHashMap<>();
    private volatile boolean closed;

    public CoverLoadCoordinator(Function<String, T> memoryLookup, Function<String, T> decoder) {
        this(memoryLookup, decoder, newExecutor(), null);
    }

    CoverLoadCoordinator(Function<String, T> memoryLookup, Function<String, T> decoder,
            Executor executor, Runnable closeAction) {
        this.memoryLookup = Objects.requireNonNull(memoryLookup, "memory lookup");
        this.decoder = Objects.requireNonNull(decoder, "decoder");
        this.executor = Objects.requireNonNull(executor, "executor");
        this.closeAction = closeAction == null && executor instanceof ExecutorService service
                ? service::shutdownNow : Objects.requireNonNull(closeAction, "close action");
    }

    public CompletionStage<T> load(String canonicalId) {
        if (canonicalId == null || canonicalId.isBlank()) {
            throw new IllegalArgumentException("canonical id must not be blank");
        }
        T cached = memoryLookup.apply(canonicalId);
        if (cached != null) return CompletableFuture.completedFuture(cached);
        if (closed) return CompletableFuture.failedFuture(
                new IllegalStateException("cover loader is closed"));
        CompletableFuture<T> shared = inFlight.computeIfAbsent(canonicalId, id -> {
            CompletableFuture<T> created = CompletableFuture.supplyAsync(
                    () -> decoder.apply(id), executor);
            created.whenComplete((value, failure) -> inFlight.remove(id, created));
            return created;
        });
        return shared.thenApply(value -> value);
    }

    @Override public void close() {
        closed = true;
        closeAction.run();
    }

    private static ExecutorService newExecutor() {
        return Executors.newFixedThreadPool(2, runnable -> {
            Thread thread = new Thread(runnable, "flynes-cover-loader");
            thread.setDaemon(true);
            return thread;
        });
    }
}
