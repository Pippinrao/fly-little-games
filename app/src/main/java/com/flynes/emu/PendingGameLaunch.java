package com.flynes.emu;

import com.flynes.emu.launch.LaunchRequest;

import java.util.concurrent.atomic.AtomicReference;

/** One-shot in-process handoff; exact request metadata stays paired with its verified payload. */
public final class PendingGameLaunch {
    private static final AtomicReference<Payload> PENDING = new AtomicReference<>();

    private PendingGameLaunch() {}

    static void stage(LaunchRequest request, byte[] bytes) {
        if (request == null || bytes == null || bytes.length == 0) {
            throw new IllegalArgumentException("pending launch must contain request and bytes");
        }
        stage(request, bytes, null);
    }

    static void stage(LaunchRequest request, byte[] bytes,
            com.flynes.emu.catalog.CanonicalGame title) {
        if (request == null || bytes == null || bytes.length == 0) {
            throw new IllegalArgumentException("pending launch must contain request and bytes");
        }
        PENDING.set(new Payload(request, bytes, title));
    }

    public static Payload consume() { return PENDING.getAndSet(null); }

    public record Payload(LaunchRequest request, byte[] bytes,
            com.flynes.emu.catalog.CanonicalGame title) {}
}
