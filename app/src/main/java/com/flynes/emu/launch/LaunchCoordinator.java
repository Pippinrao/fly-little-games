package com.flynes.emu.launch;

import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.GameVariant;

import java.util.Objects;
import java.util.Optional;

public final class LaunchCoordinator {
    private final GameCatalog catalog;
    private final ExactRomLoader loader;
    private final RomSessionGateway sessionGateway;
    private final LaunchHistory launchHistory;

    public LaunchCoordinator(
            GameCatalog catalog,
            ExactRomLoader loader,
            RomSessionGateway sessionGateway,
            LaunchHistory launchHistory) {
        this.catalog = Objects.requireNonNull(catalog, "catalog");
        this.loader = Objects.requireNonNull(loader, "loader");
        this.sessionGateway = Objects.requireNonNull(sessionGateway, "session gateway");
        this.launchHistory = Objects.requireNonNull(launchHistory, "launch history");
    }

    public LaunchResult launch(String variantId) {
        Optional<GameVariant> resolved = catalog.resolveVariant(variantId);
        if (resolved.isEmpty()) {
            return LaunchResult.failure(
                    LaunchResult.Code.VARIANT_NOT_FOUND, null, "variant was not found");
        }
        GameVariant variant = resolved.get();
        if (!variant.compatibility().isPlayable()) {
            return LaunchResult.failure(
                    LaunchResult.Code.NOT_PLAYABLE, null, "variant is not explicitly playable");
        }

        LaunchRequest request;
        try {
            request = new LaunchRequest(
                    variant.canonicalGameId(),
                    variant.variantId(),
                    variant.sourceId(),
                    variant.sourceUri(),
                    variant.entryPath(),
                    variant.packageFormat(),
                    variant.romFormat(),
                    variant.compatibility(),
                    variant.identity());
        } catch (IllegalArgumentException failure) {
            return LaunchResult.failure(
                    LaunchResult.Code.INVALID_REQUEST, null, failure.getMessage());
        }

        byte[] payload;
        try {
            payload = loader.load(request);
        } catch (ExactRomLoader.LoadException failure) {
            return LaunchResult.failure(map(failure.code()), request, failure.getMessage());
        }

        try {
            sessionGateway.stageAndReplace(request, payload);
        } catch (RomSessionGateway.SessionException failure) {
            return LaunchResult.failure(
                    LaunchResult.Code.SESSION_FAILED, request, failure.getMessage());
        }

        catalog.recordSuccessfulLaunch(request.canonicalGameId());
        launchHistory.recordSuccessfulLaunch(request);
        return LaunchResult.success(request);
    }

    private static LaunchResult.Code map(ExactRomLoader.ErrorCode code) {
        return switch (code) {
            case SOURCE_OPEN_FAILED -> LaunchResult.Code.SOURCE_OPEN_FAILED;
            case IO_ERROR -> LaunchResult.Code.IO_ERROR;
            case INVALID_ZIP -> LaunchResult.Code.INVALID_ZIP;
            case ZIP_ENTRY_MISSING -> LaunchResult.Code.ZIP_ENTRY_MISSING;
            case ZIP_ENTRY_DUPLICATE -> LaunchResult.Code.ZIP_ENTRY_DUPLICATE;
            case ZIP_ENTRY_IS_DIRECTORY -> LaunchResult.Code.ZIP_ENTRY_IS_DIRECTORY;
            case PAYLOAD_TOO_LARGE -> LaunchResult.Code.PAYLOAD_TOO_LARGE;
            case EXECUTABLE_REJECTED -> LaunchResult.Code.EXECUTABLE_REJECTED;
            case SHA1_MISMATCH -> LaunchResult.Code.HASH_MISMATCH;
        };
    }
}
