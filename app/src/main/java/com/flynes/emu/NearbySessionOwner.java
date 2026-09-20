package com.flynes.emu;

import com.flynes.emu.catalog.android.AndroidCatalogRuntime;
import com.flynes.emu.nearby.NearbyContentProvider;
import com.flynes.emu.nearby.NearbyContentPreparation;
import com.flynes.emu.nearby.NearbyExactContentLoader;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.LongFunction;

/**
 * Process-scoped owner of the public V2 session engine. Activities read
 * {@link Snapshot} instead of keeping a local connected/confirmed boolean.
 */
public final class NearbySessionOwner implements AutoCloseable {
    public static final int LINK_UNAVAILABLE = 0;
    public static final int LINK_IDLE = 1;
    public static final int LINK_INVITING = 3;
    public static final int LINK_CONNECTED_LOBBY = 8;
    public static final int GAME_NOT_STARTED = 0;
    public static final int V2_OK = 0;
    public static final int V2_ACCEPTED = 1;

    static { System.loadLibrary("nescore"); }

    private final NearbyOwnerHandle handle;
    private final NearbyContentPreparation preparation;

    private NearbySessionOwner(long handle) { this(handle, null, null); }

    private NearbySessionOwner(long handle, NearbyContentProvider provider, NearbyExactContentLoader loader) {
        this.handle = new NearbyOwnerHandle(handle);
        preparation = provider == null ? null : new NearbyContentPreparation(provider, loader,
                new NearbyContentPreparation.Bridge() {
                    @Override public NearbyContentPreparation.Admission begin(byte[] ref) {
                        return withHandle(value -> {
                            long[] ticket = new long[1];
                            int result = nativeBeginContentPreparation(value, ref, ticket);
                            return new NearbyContentPreparation.Admission(result, ticket[0]);
                        });
                    }
                    @Override public int install(long ticket, byte[] ref, byte[] hash, byte[] bytes) {
                        return withHandle(value -> nativeCompleteContentPreparation(value, ticket, ref, hash, bytes));
                    }
                    @Override public void cancel(long ticket) {
                        withHandle(value -> nativeCancelContentPreparation(value, ticket));
                    }
                });
    }

    private <T> T withHandle(LongFunction<T> action) {
        try (NearbyOwnerHandle.Lease lease = handle.acquire()) { return action.apply(lease.value()); }
    }

    public static NearbySessionOwner create() {
        long[] out = new long[1];
        int result = nativeCreate(out);
        if (result != 0 || out[0] == 0L) {
            throw new IllegalStateException("fly_session_create_v2 failed: " + result);
        }
        return new NearbySessionOwner(out[0]);
    }

    /** Lazy production composition. Provider captures current metadata at its first query. */
    public static NearbySessionOwner create(AndroidCatalogRuntime catalog) {
        if (catalog == null) throw new NullPointerException("catalog runtime");
        NearbyContentProvider provider = new NearbyContentProvider(
                catalog.gameCatalog(), catalog.streamOpener()::validateAccess);
        long[] out = new long[1];
        int result = nativeCreateWithProvider(provider, out);
        if (result != V2_OK || out[0] == 0) {
            provider.close();
            throw new IllegalStateException("fly_session_create_v2 failed: " + result);
        }
        try {
            return new NearbySessionOwner(out[0], provider, catalog.nearbyContentLoader());
        } catch (RuntimeException | Error failure) {
            try { nativeDestroy(out[0]); }
            catch (RuntimeException | Error cleanup) { failure.addSuppressed(cleanup); }
            try { provider.close(); }
            catch (RuntimeException | Error cleanup) { failure.addSuppressed(cleanup); }
            throw failure;
        }
    }

    public int v2CreateCount() {
        return withHandle(NearbySessionOwner::nativeV2CreateCount);
    }

    public int submitAction(int actionKind, byte[] code) {
        if (actionKind == 42) throw new IllegalArgumentException("use selectContent with the displayed source reference");
        if (preparation != null && (actionKind == 1 || actionKind == 7 || actionKind == 8 ||
                actionKind == 19 || actionKind == 20 || actionKind == 25 || actionKind == 30 || actionKind == 36))
            preparation.cancel();
        return withHandle(value -> nativeSubmitAction(value, actionKind, code));
    }

    /** Binds exactly one displayed source; metadata selection does not load or start a ROM. */
    public int selectContent(byte[] sourceChoiceRef) {
        if (sourceChoiceRef == null || sourceChoiceRef.length != 16)
            throw new IllegalArgumentException("source choice reference must be 16 bytes");
        if (preparation != null) preparation.cancel();
        return withHandle(value -> nativeSelectContent(value, sourceChoiceRef.clone()));
    }

    /** Completes only after exact IO preparation and this request's actual SELECT receipt. */
    public CompletionStage<Integer> prepareAndSelectContent(byte[] sourceChoiceRef) {
        if (preparation == null) return CompletableFuture.completedFuture(-18);
        return preparation.prepareAndSelect(sourceChoiceRef);
    }

    public GameChoice[] gameChoices() {
        GameChoice[] choices = withHandle(NearbySessionOwner::nativeGameChoices);
        if (choices == null) throw new IllegalStateException("content view unavailable");
        return choices;
    }

    public static final class GameChoice {
        private final byte[] ref;
        private final byte[] contentId;
        public final boolean selectable;
        public final String displayName;
        public final String reasonKey;
        private GameChoice(byte[] ref, byte[] contentId, boolean selectable, byte[] name, byte[] reason) {
            this.ref = ref.clone(); this.contentId = contentId.clone();
            this.selectable = selectable;
            this.displayName = new String(name, java.nio.charset.StandardCharsets.UTF_8);
            this.reasonKey = new String(reason, java.nio.charset.StandardCharsets.UTF_8);
        }
        public byte[] sourceChoiceRef() { return ref.clone(); }
        public byte[] contentId() { return contentId.clone(); }
    }

    /** Confirm exactly the displayed configuration; this does not start a game. */
    public int confirmGameConfig(byte[] pendingConfigId, long pendingConfigRevision) {
        return withHandle(value -> nativeConfirmGameConfig(value, pendingConfigId, pendingConfigRevision));
    }

    public Snapshot snapshot() {
        long[] fields = new long[12];
        byte[] reason = new byte[64];
        byte[] pendingConfigId = new byte[32];
        int result = withHandle(value -> nativeSnapshot(value, fields, reason, pendingConfigId));
        if (result != 0) {
            throw new IllegalStateException("session snapshot failed: " + result);
        }
        return new Snapshot((int) fields[0], (int) fields[1], (int) fields[2],
                (int) fields[3], (int) fields[4], new String(reason,
                java.nio.charset.StandardCharsets.UTF_8).trim(), pendingConfigId, fields[5],
                fields[6], (int) fields[7], (int) fields[8], fields[9] != 0,
                fields[10] != 0, (int) fields[11]);
    }

    public String quicProviderType() {
        return withHandle(NearbySessionOwner::nativeQuicProviderType);
    }

    public boolean quicReady() {
        return withHandle(NearbySessionOwner::nativeQuicReady) != 0;
    }

    public String quicListenAddress() {
        return withHandle(NearbySessionOwner::nativeQuicListenAddress);
    }

    public int quicEngineListen() {
        return withHandle(NearbySessionOwner::nativeQuicEngineListen);
    }

    public int quicControlRoundtripFrom(NearbySessionOwner connector, long[] facts) {
        if (connector == null || facts == null || facts.length < 5)
            throw new IllegalArgumentException("roundtrip needs peer owner and 5 facts");
        try (NearbyOwnerHandle.Lease listener = handle.acquire();
             NearbyOwnerHandle.Lease peer = connector.handle.acquire()) {
            return nativeQuicControlRoundtrip(listener.value(), peer.value(), facts);
        }
    }

    public int armTestTimer(long timerId, long delayMs) {
        return withHandle(value -> nativeArmTestTimer(value, timerId, delayMs));
    }

    public int cancelTestTimer(long timerId) {
        return withHandle(value -> nativeCancelTestTimer(value, timerId));
    }

    public int testTimerFires(long timerId) {
        return withHandle(value -> nativeTestTimerFires(value, timerId));
    }

    public boolean waitTestTimer(long timerId, long timeoutMs) {
        return withHandle(value -> nativeWaitTestTimer(value, timerId, timeoutMs)) != 0;
    }

    @Override public void close() {
        handle.close(() -> { if (preparation != null) preparation.close(); }, NearbySessionOwner::nativeDestroy);
    }

    public static final class Snapshot {
        public final int abiVersion;
        public final int linkState;
        public final int gameState;
        public final int pendingConfigLocalConfirmed;
        public final int pendingConfigPeerConfirmed;
        public final String primaryReasonKey;
        public final long pendingConfigRevision;
        public final long lastActionRequestId;
        public final int lastActionResult;
        public final int lastActionOutcome;
        public final boolean shutdownComplete;
        /** Local metadata-provider facts; they never imply connection or game authorization. */
        public final boolean contentQueryAttempted;
        public final int lastContentQueryResult;
        private final byte[] pendingConfigId;

        Snapshot(int abiVersion, int linkState, int gameState,
                 int pendingConfigLocalConfirmed, int pendingConfigPeerConfirmed,
                 String primaryReasonKey, byte[] pendingConfigId, long pendingConfigRevision,
                 long lastActionRequestId, int lastActionResult, int lastActionOutcome,
                 boolean shutdownComplete) {
            this(abiVersion, linkState, gameState, pendingConfigLocalConfirmed, pendingConfigPeerConfirmed,
                    primaryReasonKey, pendingConfigId, pendingConfigRevision, lastActionRequestId,
                    lastActionResult, lastActionOutcome, shutdownComplete, false, V2_OK);
        }

        Snapshot(int abiVersion, int linkState, int gameState,
                 int pendingConfigLocalConfirmed, int pendingConfigPeerConfirmed,
                 String primaryReasonKey, byte[] pendingConfigId, long pendingConfigRevision,
                 long lastActionRequestId, int lastActionResult, int lastActionOutcome,
                 boolean shutdownComplete, boolean contentQueryAttempted, int lastContentQueryResult) {
            this.abiVersion = abiVersion;
            this.linkState = linkState;
            this.gameState = gameState;
            this.pendingConfigLocalConfirmed = pendingConfigLocalConfirmed;
            this.pendingConfigPeerConfirmed = pendingConfigPeerConfirmed;
            this.primaryReasonKey = primaryReasonKey;
            this.pendingConfigId = pendingConfigId.clone();
            this.pendingConfigRevision = pendingConfigRevision;
            this.lastActionRequestId = lastActionRequestId;
            this.lastActionResult = lastActionResult;
            this.lastActionOutcome = lastActionOutcome;
            this.shutdownComplete = shutdownComplete;
            this.contentQueryAttempted = contentQueryAttempted;
            this.lastContentQueryResult = lastContentQueryResult;
        }

        public byte[] pendingConfigId() { return pendingConfigId.clone(); }

        public boolean canConfirmGameConfig() {
            if (pendingConfigLocalConfirmed != 0 || pendingConfigRevision == 0) return false;
            for (byte value : pendingConfigId) {
                if (value != 0) return true;
            }
            return false;
        }
    }

    private static native int nativeCreate(long[] outHandle);
    private static native int nativeCreateWithProvider(NearbyContentProvider provider, long[] outHandle);
    private static native int nativeSelectContent(long handle, byte[] sourceChoiceRef);
    private static native int nativeBeginContentPreparation(long handle, byte[] ref, long[] ticket);
    private static native int nativeCompleteContentPreparation(long handle, long ticket, byte[] ref, byte[] hash, byte[] bytes);
    private static native int nativeCancelContentPreparation(long handle, long ticket);
    private static native GameChoice[] nativeGameChoices(long handle);
    private static native void nativeDestroy(long handle);
    private static native int nativeV2CreateCount(long handle);
    private static native int nativeSubmitAction(long handle, int actionKind, byte[] code);
    private static native int nativeSnapshot(long handle, long[] fields, byte[] reason,
                                             byte[] pendingConfigId);
    private static native int nativeConfirmGameConfig(long handle, byte[] pendingConfigId,
                                                       long pendingConfigRevision);
    private static native String nativeQuicProviderType(long handle);
    private static native int nativeQuicReady(long handle);
    private static native String nativeQuicListenAddress(long handle);
    private static native int nativeQuicEngineListen(long handle);
    private static native int nativeQuicControlRoundtrip(
            long listenerHandle, long connectorHandle, long[] facts);
    private static native int nativeArmTestTimer(long handle, long timerId, long delayMs);
    private static native int nativeCancelTestTimer(long handle, long timerId);
    private static native int nativeTestTimerFires(long handle, long timerId);
    private static native int nativeWaitTestTimer(long handle, long timerId, long timeoutMs);
}
