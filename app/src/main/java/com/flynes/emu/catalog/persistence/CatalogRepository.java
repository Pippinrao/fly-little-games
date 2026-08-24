package com.flynes.emu.catalog.persistence;

import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomSource;

import java.io.IOException;
import java.util.ArrayList;
import java.util.LinkedHashMap;

/** Synchronized transaction boundary for durable catalog state and atomic catalog publication. */
public final class CatalogRepository {
    private CatalogState state;
    private final CatalogStateStore store;
    private final GameCatalog catalog;
    private final RomSource fixedBuiltinSource;

    public CatalogRepository(CatalogState initial, CatalogStateStore store, GameCatalog catalog) {
        if (initial == null || store == null || catalog == null) throw new NullPointerException();
        this.state = initial;
        this.store = store;
        this.catalog = catalog;
        this.fixedBuiltinSource = initial.sources().get(initial.builtinSourceId()).source();
        publish(initial);
    }

    public synchronized CatalogState state() { return state; }

    public synchronized void commitScan(SourceScanResult scan) throws RepositoryException {
        final CatalogState next;
        try { next = CatalogReconciler.reconcile(state, scan); }
        catch (CatalogReconciler.ReconcileException failure) {
            throw new RepositoryException(ErrorCode.STALE_OR_INVALID, failure);
        }
        commit(next);
    }

    public synchronized void addSource(RomSource source) throws RepositoryException {
        commit(state.withSource(source));
    }

    public synchronized void reauthorizeSource(RomSource source) throws RepositoryException {
        SourceCatalogState old = state.sources().get(source.id());
        if (state.builtinSourceId().equals(source.id())) {
            throw new RepositoryException(ErrorCode.BUILTIN_REMOVAL_REJECTED, null);
        }
        if (old == null || old.source().type() != source.type()) {
            throw new RepositoryException(ErrorCode.SOURCE_NOT_FOUND, null);
        }
        LinkedHashMap<String, SourceCatalogState> sources = new LinkedHashMap<>(state.sources());
        sources.put(source.id(), new SourceCatalogState(
                source, old.packages(), old.packageOutcomes(), old.entryOutcomes(), old.issues(),
                old.lastScanCompleteness(), old.lastScanToken()));
        commit(state.replace(state.revision() + 1, sources,
                state.userStates(), state.lastPlayedSequence()));
    }

    public synchronized void removeSource(String sourceId) throws RepositoryException {
        if (state.builtinSourceId().equals(sourceId)) {
            throw new RepositoryException(ErrorCode.BUILTIN_REMOVAL_REJECTED, null);
        }
        if (!state.sources().containsKey(sourceId)) {
            throw new RepositoryException(ErrorCode.SOURCE_NOT_FOUND, null);
        }
        LinkedHashMap<String, SourceCatalogState> sources = new LinkedHashMap<>(state.sources());
        sources.remove(sourceId);
        commit(state.replace(state.revision() + 1, sources,
                state.userStates(), state.lastPlayedSequence()));
    }

    public synchronized boolean setFavorite(String canonicalId, boolean favorite)
            throws RepositoryException {
        if (!containsCanonical(canonicalId)) return false;
        LinkedHashMap<String, CanonicalUserState> users = new LinkedHashMap<>(state.userStates());
        CanonicalUserState old = users.getOrDefault(canonicalId, CanonicalUserState.EMPTY);
        users.put(canonicalId, old.withFavorite(favorite));
        commit(state.replace(state.revision() + 1, state.sources(), users,
                state.lastPlayedSequence()));
        return true;
    }

    public synchronized boolean recordSuccessfulLaunch(String canonicalId)
            throws RepositoryException {
        if (!containsCanonical(canonicalId)) return false;
        long sequence = Math.addExact(state.lastPlayedSequence(), 1);
        LinkedHashMap<String, CanonicalUserState> users = new LinkedHashMap<>(state.userStates());
        CanonicalUserState old = users.getOrDefault(canonicalId, CanonicalUserState.EMPTY);
        users.put(canonicalId, old.launched(sequence));
        commit(state.replace(state.revision() + 1, state.sources(), users, sequence));
        return true;
    }

    public synchronized LoadResult load() throws RepositoryException {
        final byte[] encoded;
        try { encoded = store.read(); }
        catch (IOException failure) { throw new RepositoryException(ErrorCode.STORE_READ_FAILED, failure); }
        if (encoded == null) return new LoadResult(LoadStatus.NO_STATE, state, null);
        final CatalogState decoded;
        try { decoded = CatalogStateCodec.decode(encoded); }
        catch (CatalogStateCodec.CodecException corrupt) {
            return new LoadResult(LoadStatus.RECOVERY_NEEDED, state, corrupt.code());
        }
        SourceCatalogState decodedBuiltin = decoded.sources().get(decoded.builtinSourceId());
        if (!decoded.builtinSourceId().equals(fixedBuiltinSource.id())
                || decodedBuiltin == null
                || !sameStableSourceIdentity(decodedBuiltin.source(), fixedBuiltinSource)) {
            return new LoadResult(
                    LoadStatus.RECOVERY_NEEDED, state,
                    CatalogStateCodec.ErrorCode.INVALID_FIELD);
        }
        state = decoded;
        publish(decoded);
        return new LoadResult(LoadStatus.LOADED, decoded, null);
    }

    private void commit(CatalogState next) throws RepositoryException {
        try {
            new GameCatalog().publishPersistentState(
                    projectedPackages(next), next.userStates(), next.lastPlayedSequence());
        } catch (RuntimeException invalid) {
            throw new RepositoryException(ErrorCode.INVALID_STATE, invalid);
        }
        final byte[] encoded;
        try {
            encoded = CatalogStateCodec.encode(next);
            store.writeAtomically(encoded);
        } catch (IOException | CatalogStateCodec.CodecException failure) {
            throw new RepositoryException(ErrorCode.STORE_WRITE_FAILED, failure);
        }
        publish(next);
        state = next;
    }

    private void publish(CatalogState value) {
        catalog.publishPersistentState(
                projectedPackages(value), value.userStates(), value.lastPlayedSequence());
    }

    private static ArrayList<PhysicalPackage> projectedPackages(CatalogState value) {
        ArrayList<PhysicalPackage> packages = new ArrayList<>();
        for (SourceCatalogState source : value.sources().values()) {
            for (CatalogPackage item : source.packages().values()) {
                CatalogPackage projected = item;
                if (!source.source().isUsable()) {
                    projected = new CatalogPackage(
                            item.physicalPackage(), CatalogPackage.Freshness.PRESERVED_STALE);
                }
                packages.add(projected.projectedPackage());
            }
        }
        return packages;
    }

    private boolean containsCanonical(String id) {
        for (SourceCatalogState source : state.sources().values()) {
            for (CatalogPackage item : source.packages().values()) {
                for (com.flynes.emu.catalog.RomVariant variant
                        : item.physicalPackage().variants()) {
                    if (variant.canonicalGame().id().equals(id)) return true;
                }
            }
        }
        return false;
    }

    private static boolean sameStableSourceIdentity(RomSource first, RomSource second) {
        return first.id().equals(second.id())
                && first.type() == second.type()
                && first.uri().equals(second.uri())
                && first.permissionState() == second.permissionState();
    }

    public enum ErrorCode {
        STALE_OR_INVALID, SOURCE_NOT_FOUND, BUILTIN_REMOVAL_REJECTED,
        STORE_READ_FAILED, STORE_WRITE_FAILED, INVALID_STATE
    }
    public enum LoadStatus { NO_STATE, LOADED, RECOVERY_NEEDED }
    public record LoadResult(
            LoadStatus status, CatalogState state, CatalogStateCodec.ErrorCode recoveryReason) {}

    public static final class RepositoryException extends Exception {
        private final ErrorCode code;
        RepositoryException(ErrorCode code, Throwable cause) {
            super(code.name(), cause); this.code = code;
        }
        public ErrorCode code() { return code; }
    }
}
