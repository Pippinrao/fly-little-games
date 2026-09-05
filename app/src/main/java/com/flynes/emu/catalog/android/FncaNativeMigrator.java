package com.flynes.emu.catalog.android;

import com.flynes.emu.app.FlyCatalogCommands;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.persistence.CanonicalUserState;
import com.flynes.emu.catalog.persistence.CatalogPackage;
import com.flynes.emu.catalog.persistence.CatalogState;
import com.flynes.emu.catalog.persistence.SourceCatalogState;
import com.flynes.emu.catalog.persistence.SourceScanResult;

import java.util.Map;
import java.util.Objects;
import java.util.UUID;

/** Replays FNCA catalog state through the shared scan/user ABI. Old FNCA is kept until success. */
public final class FncaNativeMigrator {
    public record OpenedFile(
            String relativePath, String displayName, int borrowedFd, byte[] expectedPhysicalSha256,
            AutoCloseable closer) {
        public OpenedFile {
            relativePath = Objects.requireNonNull(relativePath, "relative path");
            displayName = Objects.requireNonNull(displayName, "display name");
            expectedPhysicalSha256 = expectedPhysicalSha256 == null
                    ? null : expectedPhysicalSha256.clone();
        }

        public OpenedFile(
                String relativePath, String displayName, int borrowedFd, byte[] expectedPhysicalSha256) {
            this(relativePath, displayName, borrowedFd, expectedPhysicalSha256, null);
        }

        public void close() {
            if (closer == null) return;
            try {
                closer.close();
            } catch (Exception ignored) {
            }
        }
    }

    @FunctionalInterface
    public interface PackageOpener {
        OpenedFile open(PhysicalPackage pkg) throws Exception;
    }

    public boolean migrate(
            CatalogState state,
            AndroidUuidSafMap map,
            FlyCatalogCommands commands,
            PackageOpener opener,
            AndroidRetryableMigrationLog log) {
        Objects.requireNonNull(state, "state");
        Objects.requireNonNull(map, "map");
        Objects.requireNonNull(commands, "commands");
        Objects.requireNonNull(opener, "opener");
        Objects.requireNonNull(log, "log");
        if (log.succeeded(AndroidRetryableMigrationLog.Kind.CATALOG_FNCA)) return true;
        try {
            for (SourceCatalogState sourceState : state.sources().values()) {
                replaySource(sourceState, map, commands, opener);
            }
            replayUsers(state.userStates(), commands);
            log.record(AndroidRetryableMigrationLog.Kind.CATALOG_FNCA, true, "persisted flycat01");
            return true;
        } catch (Exception failure) {
            commands.scanAbort();
            String detail = failure.getMessage() == null ? failure.getClass().getSimpleName()
                    : failure.getMessage();
            log.record(AndroidRetryableMigrationLog.Kind.CATALOG_FNCA, false, detail);
            return false;
        }
    }

    private static void replaySource(
            SourceCatalogState sourceState,
            AndroidUuidSafMap map,
            FlyCatalogCommands commands,
            PackageOpener opener) throws Exception {
        RomSource source = sourceState.source();
        byte[] uuid = uuidFor(source, map);
        int scope = source.type() == RomSource.Type.BUILTIN
                ? FlyCatalogCommands.SOURCE_SCOPE_BUILTIN
                : FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY;
        requireOk(commands.scanBegin(uuid, scope), "scanBegin");
        boolean openFailed = false;
        try {
            for (CatalogPackage item : sourceState.packages().values()) {
                if (item.freshness() != CatalogPackage.Freshness.FRESH) continue;
                OpenedFile opened = opener.open(item.physicalPackage());
                try {
                    requireOk(commands.scanAddFile(
                            opened.relativePath(), opened.displayName(), opened.borrowedFd(),
                            opened.expectedPhysicalSha256()), "scanAddFile");
                } finally {
                    opened.close();
                }
            }
        } catch (Exception failure) {
            openFailed = true;
            throw failure;
        } finally {
            if (openFailed) commands.scanAbort();
        }
        requireOk(commands.scanCommit(completeness(sourceState.lastScanCompleteness())),
                "scanCommit");
    }

    private static void replayUsers(
            Map<String, CanonicalUserState> users, FlyCatalogCommands commands) throws Exception {
        for (Map.Entry<String, CanonicalUserState> item : users.entrySet()) {
            CanonicalUserState user = item.getValue();
            if (user.favorite()) {
                requireOk(commands.favoriteSet(item.getKey(), true), "favoriteSet");
            }
            for (int play = 0; play < user.playCount(); play++) {
                requireOk(commands.markPlayed(item.getKey()), "markPlayed");
            }
        }
    }

    private static byte[] uuidFor(RomSource source, AndroidUuidSafMap map) {
        if (source.type() == RomSource.Type.BUILTIN) return map.builtinUuid();
        byte[] existing = map.uuidForLocator(source.uri());
        if (existing != null) return existing;
        byte[] generated = uuidBytes(UUID.randomUUID());
        map.put(generated, source.uri());
        return generated;
    }

    private static byte[] uuidBytes(UUID value) {
        byte[] bytes = new byte[16];
        long high = value.getMostSignificantBits();
        long low = value.getLeastSignificantBits();
        for (int index = 0; index < 8; index++) {
            bytes[index] = (byte) (high >>> (8 * (7 - index)));
            bytes[8 + index] = (byte) (low >>> (8 * (7 - index)));
        }
        return bytes;
    }

    private static int completeness(SourceScanResult.Completeness value) {
        return switch (value) {
            case PARTIAL -> FlyCatalogCommands.SCAN_PARTIAL;
            case FATAL -> FlyCatalogCommands.SCAN_FATAL;
            case FULL -> FlyCatalogCommands.SCAN_FULL;
        };
    }

    private static void requireOk(int result, String step) throws Exception {
        if (result != FlyCatalogCommands.OK) {
            throw new Exception(step + " failed: " + result);
        }
    }
}
