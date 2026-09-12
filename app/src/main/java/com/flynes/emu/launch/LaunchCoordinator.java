package com.flynes.emu.launch;

import com.flynes.emu.catalog.DomainValidation;
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
        Optional<GameCatalog.LaunchResolution> resolved =
                catalog.resolveVariantForLaunch(variantId);
        if (!resolved.isPresent()) {
            return LaunchResult.failure(
                    LaunchResult.Code.VARIANT_NOT_FOUND, null, "variant was not found");
        }
        GameCatalog.LaunchResolution resolution = resolved.get();
        GameVariant variant = resolution.variant();
        if (!variant.compatibility().isPlayable()
                || !variant.isLaunchable()) {
            return LaunchResult.failure(
                    LaunchResult.Code.NOT_PLAYABLE,
                    null,
                    "variant is not playable from an authorized source");
        }

        LaunchRequest request;
        try {
            request = requestFor(variant);
        } catch (IllegalArgumentException failure) {
            return LaunchResult.failure(
                    LaunchResult.Code.INVALID_REQUEST, null, failure.getMessage());
        }

        byte[] payload;
        try {
            payload = loader.load(request);
        } catch (ExactRomLoader.LoadException failure) {
            return LaunchResult.failure(map(failure.code()), request, failure.getMessage());
        } catch (RuntimeException failure) {
            // Platform providers raise unchecked faults for malformed locators. Reporting them
            // keeps the failure on the launch thread instead of killing the process.
            return LaunchResult.failure(
                    LaunchResult.Code.SOURCE_OPEN_FAILED,
                    request,
                    failureMessage(failure, "unexpected ROM source failure"));
        }

        LaunchRequest loadedRequest = request;
        byte[] loadedPayload = payload;
        return catalog.serializeLaunch(() -> commitLaunch(
                resolution, loadedRequest, loadedPayload));
    }

    private LaunchResult commitLaunch(
            GameCatalog.LaunchResolution resolution,
            LaunchRequest loadedRequest,
            byte[] payload) {
        LaunchRequest[] committedRequest = new LaunchRequest[1];
        Optional<GameVariant> committedVariant;
        try {
            committedVariant = catalog.commitSuccessfulLaunch(
                    resolution,
                    currentVariant -> {
                        LaunchRequest currentRequest = requestFor(currentVariant);
                        committedRequest[0] = currentRequest;
                        try {
                            sessionGateway.stageAndReplace(currentRequest, payload);
                        } catch (RomSessionGateway.SessionException failure) {
                            throw failure;
                        } catch (RuntimeException failure) {
                            throw new RomSessionGateway.SessionException(
                                    failureMessage(failure, "unexpected ROM session failure"),
                                    failure);
                        }
                    });
        } catch (RomSessionGateway.SessionException failure) {
            LaunchRequest failedRequest = committedRequest[0] == null
                    ? loadedRequest
                    : committedRequest[0];
            return LaunchResult.failure(
                    LaunchResult.Code.SESSION_FAILED,
                    failedRequest,
                    failureMessage(failure, "ROM session update failed"));
        } catch (IllegalArgumentException failure) {
            return LaunchResult.failure(
                    LaunchResult.Code.INVALID_REQUEST,
                    loadedRequest,
                    failureMessage(failure, "current launch request is invalid"));
        }
        if (!committedVariant.isPresent()) {
            return LaunchResult.failure(
                    LaunchResult.Code.CATALOG_CHANGED,
                    loadedRequest,
                    "exact loaded variant is no longer launchable in the catalog");
        }

        LaunchRequest currentRequest = committedRequest[0];
        if (currentRequest == null) {
            return LaunchResult.failure(
                    LaunchResult.Code.INVALID_REQUEST,
                    loadedRequest,
                    "catalog launch commit did not produce a request");
        }
        try {
            launchHistory.recordSuccessfulLaunch(currentRequest);
        } catch (LaunchHistory.HistoryException | RuntimeException failure) {
            return LaunchResult.failure(
                    LaunchResult.Code.HISTORY_FAILED,
                    currentRequest,
                    failureMessage(failure, "launch history update failed"));
        }
        return LaunchResult.success(currentRequest);
    }

    private static LaunchRequest requestFor(GameVariant variant) {
        return LaunchRequest.forVariant(variant);
    }

    /*
     * The catalog commit above deliberately ends before history I/O. The catalog-owned launch
     * sequence still orders histories across coordinators, while scans/favorites/search remain
     * independent of slow history storage.
     */

    private static String failureMessage(Throwable failure, String fallback) {
        String message = failure.getMessage();
        return message == null || DomainValidation.isBlank(message) ? fallback : message;
    }

    private static LaunchResult.Code map(ExactRomLoader.ErrorCode code) {
        return switch (code) {
            case SOURCE_OPEN_FAILED -> LaunchResult.Code.SOURCE_OPEN_FAILED;
            case IO_ERROR -> LaunchResult.Code.IO_ERROR;
            case INVALID_ZIP -> LaunchResult.Code.INVALID_ZIP;
            case ZIP_SOURCE_LIMIT_EXCEEDED -> LaunchResult.Code.ZIP_SOURCE_LIMIT_EXCEEDED;
            case ZIP_ENTRY_LIMIT_EXCEEDED -> LaunchResult.Code.ZIP_ENTRY_LIMIT_EXCEEDED;
            case ZIP_INFLATED_LIMIT_EXCEEDED -> LaunchResult.Code.ZIP_INFLATED_LIMIT_EXCEEDED;
            case ZIP_NAME_LIMIT_EXCEEDED -> LaunchResult.Code.INVALID_ZIP;
            case ZIP_RATIO_LIMIT_EXCEEDED -> LaunchResult.Code.INVALID_ZIP;
            case ZIP_ENTRY_MISSING -> LaunchResult.Code.ZIP_ENTRY_MISSING;
            case ZIP_ENTRY_DUPLICATE -> LaunchResult.Code.ZIP_ENTRY_DUPLICATE;
            case ZIP_ENTRY_IS_DIRECTORY -> LaunchResult.Code.ZIP_ENTRY_IS_DIRECTORY;
            case PAYLOAD_TOO_LARGE -> LaunchResult.Code.PAYLOAD_TOO_LARGE;
            case EXECUTABLE_REJECTED -> LaunchResult.Code.EXECUTABLE_REJECTED;
            case HASH_MISMATCH -> LaunchResult.Code.HASH_MISMATCH;
        };
    }
}
