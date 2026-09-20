package com.flynes.emu.nearby;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.Arrays;
import java.util.Objects;

/** Bounded exact-content preparation, separate from engine admission and connection state. */
public final class NearbyContentPreparation implements AutoCloseable {
    private static final int OK = 0;
    private static final int INVALID_ARGUMENT = -1;
    private static final int STALE = -3;
    private static final int INVALID_STATE = -4;
    private static final int PERMISSION_DENIED = -6;
    private static final int IO_FAILED = -12;
    private static final int UNAVAILABLE = -18;

    public record Admission(int result, long ticket) {}
    public interface Bridge {
        Admission begin(byte[] sourceChoiceRef);
        /** Returns zero only after the corresponding real SELECT has been applied. */
        int install(long ticket, byte[] sourceChoiceRef, byte[] contentHash, byte[] bytes);
        /** Idempotent invalidation; implementations release any native lease before returning. */
        void cancel(long ticket);
    }

    // Serializes native admission, not ROM IO. Provider/native callbacks must never enter here.
    private final Object admission = new Object();
    private final Object monitor = new Object();
    private final NearbyContentProvider provider;
    private final NearbyExactContentLoader loader;
    private final Bridge bridge;
    private final Thread worker;
    private Request current;
    private Request pending;
    private Request running;
    private boolean closed;

    public NearbyContentPreparation(NearbyContentProvider provider,
            NearbyExactContentLoader loader, Bridge bridge) {
        this.provider = Objects.requireNonNull(provider);
        this.loader = Objects.requireNonNull(loader);
        this.bridge = Objects.requireNonNull(bridge);
        worker = new Thread(this::run, "nearby-content-io");
        worker.setDaemon(true);
        worker.start();
    }

    /** Only in-flight repeated clicks coalesce; a terminal result never authorizes a new round. */
    public CompletionStage<Integer> prepareAndSelect(byte[] ref) {
        byte[] requested = ref == null ? null : ref.clone();
        Request replaced = null;
        CompletableFuture<Integer> result = null;
        int rejection = UNAVAILABLE;
        synchronized (admission) {
            boolean stopped;
            synchronized (monitor) { stopped = closed; }
            if (!stopped) {
                NearbyContentProvider.Selection selection = null;
                if (requested == null || requested.length != 16) rejection = INVALID_ARGUMENT;
                else {
                    try {
                        selection = provider.resolveSelection(requested);
                        rejection = STALE;
                    } catch (RuntimeException unavailable) { rejection = UNAVAILABLE; }
                }
                synchronized (monitor) {
                    if (selection != null && current != null && !current.future.isDone()
                            && Arrays.equals(current.selection.sourceChoiceRef(), requested)) {
                        result = current.future;
                    }
                }
                if (result == null) {
                    replaced = detachCurrent();
                    cancelTicket(replaced);
                    if (selection != null) {
                        Admission accepted;
                        try { accepted = bridge.begin(selection.sourceChoiceRef()); }
                        catch (RuntimeException unavailable) { accepted = new Admission(UNAVAILABLE, 0); }
                        if (accepted != null && accepted.result() == OK && accepted.ticket() > 0) {
                            Request request = new Request(selection, accepted.ticket());
                            // Caller cancellation of the returned future also invalidates the ticket.
                            request.future.whenComplete((value, failure) -> {
                                if (request.future.isCancelled()) cancelRequest(request);
                            });
                            synchronized (monitor) {
                                current = request;
                                pending = request;
                                monitor.notifyAll();
                            }
                            result = request.future;
                        } else rejection = accepted == null ? UNAVAILABLE
                                : accepted.result() < 0 ? accepted.result() : INVALID_STATE;
                    }
                }
            }
        }
        // CompletableFuture callbacks may synchronously call owner.close: never complete under a lock.
        cancelFuture(replaced);
        return result != null ? result : CompletableFuture.completedFuture(rejection);
    }

    public void cancel() { cancelOrClose(false); }

    @Override public void close() { cancelOrClose(true); }

    private void cancelOrClose(boolean closing) {
        Request cancelled;
        synchronized (admission) {
            synchronized (monitor) {
                if (closing) closed = true;
                monitor.notifyAll();
            }
            cancelled = detachCurrent();
            cancelTicket(cancelled);
        }
        cancelFuture(cancelled);
    }

    private void cancelRequest(Request request) {
        synchronized (admission) {
            synchronized (monitor) { if (current != request) return; }
            cancelTicket(detachCurrent());
        }
    }

    // Caller owns admission. Interrupt only the one running worker; never spawn replacement readers.
    private Request detachCurrent() {
        synchronized (monitor) {
            Request detached = current;
            current = null;
            pending = null;
            if (running != null) worker.interrupt();
            monitor.notifyAll();
            return detached;
        }
    }

    private void cancelTicket(Request request) {
        if (request == null) return;
        // A closing owner may already have sealed handle admission; no future completion here.
        try { bridge.cancel(request.ticket); }
        catch (RuntimeException unavailable) { /* the owner bridge must fail closed */ }
    }

    private static void cancelFuture(Request request) {
        if (request != null && !request.future.isDone()) request.future.cancel(false);
    }

    private void run() {
        while (true) {
            Request request;
            synchronized (monitor) {
                while (pending == null && !closed) {
                    try { monitor.wait(); }
                    catch (InterruptedException cancelled) { /* reevaluate the bounded pending slot */ }
                }
                if (closed) return;
                request = pending;
                pending = null;
                running = request;
                Thread.interrupted(); // old operation's cooperative cancellation must not poison this read
            }
            byte[] bytes = null;
            int terminal = STALE;
            try {
                if (provider.isCurrent(request.selection)) {
                    NearbyExactContentLoader.LoadedContent loaded =
                            loader.load(request.selection.variant().variantId());
                    if (request.selection.variant().equals(loaded.variant())
                            && provider.isCurrent(request.selection)) {
                        bytes = loaded.bytes();
                        terminal = OK;
                    }
                }
            } catch (NearbyExactContentLoader.ContentException failure) {
                terminal = switch (failure.code()) {
                    case SOURCE_ACCESS_DENIED -> PERMISSION_DENIED;
                    case LOAD_FAILED -> IO_FAILED;
                    case NOT_PLAYABLE -> INVALID_STATE;
                    case VARIANT_NOT_FOUND, CATALOG_CHANGED -> STALE;
                };
            } catch (RuntimeException failure) { terminal = UNAVAILABLE; }

            boolean deliver = false;
            synchronized (admission) {
                boolean active;
                synchronized (monitor) {
                    active = !closed && current == request && !request.future.isCancelled();
                }
                if (active) {
                    try {
                        if (terminal == OK) {
                            terminal = provider.isCurrent(request.selection)
                                    ? bridge.install(request.ticket, request.selection.sourceChoiceRef(),
                                            request.selection.contentHash(), bytes)
                                    : STALE;
                            if (terminal > OK) terminal = INVALID_STATE;
                        }
                    } catch (RuntimeException failure) { terminal = UNAVAILABLE; }
                    if (terminal != OK) {
                        synchronized (monitor) { current = null; }
                        cancelTicket(request);
                    }
                    deliver = true;
                }
                synchronized (monitor) { running = null; }
            }
            if (deliver) request.future.complete(terminal);
        }
    }

    private static final class Request {
        final NearbyContentProvider.Selection selection;
        final long ticket;
        final CompletableFuture<Integer> future = new CompletableFuture<>();
        Request(NearbyContentProvider.Selection selection, long ticket) {
            this.selection = selection;
            this.ticket = ticket;
        }
    }
}
