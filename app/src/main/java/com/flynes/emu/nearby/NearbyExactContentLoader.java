package com.flynes.emu.nearby;

import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.GameVariant;
import com.flynes.emu.launch.ExactRomLoader;
import com.flynes.emu.launch.LaunchRequest;

import java.util.Objects;

/**
 * Loads only the selected catalog variant, without starting a game or recording play history.
 * Call from the caller's IO worker: this synchronous operation opens and reads a bounded package.
 * The returned content records a moment-in-time catalog/access check, not a session authorization
 * or a lasting lease on storage permissions. A later owner must bind it to its own generation.
 */
public final class NearbyExactContentLoader {
    private final GameCatalog catalog;
    private final ExactRomLoader loader;
    private final ReadAccessValidator validator;

    public NearbyExactContentLoader(GameCatalog catalog, ExactRomLoader loader,
            ReadAccessValidator validator) {
        this.catalog = Objects.requireNonNull(catalog, "catalog");
        this.loader = Objects.requireNonNull(loader, "exact ROM loader");
        this.validator = Objects.requireNonNull(validator, "read access validator");
    }

    public LoadedContent load(String selectedVariantId) throws ContentException {
        GameCatalog.LaunchResolution resolution = catalog.resolveVariantForLaunch(selectedVariantId)
                .orElseThrow(() -> new ContentException(FailureCode.VARIANT_NOT_FOUND, null));
        GameVariant selected = resolution.variant();
        if (!selected.isLaunchable()) throw new ContentException(FailureCode.NOT_PLAYABLE, null);
        try {
            validator.validate(selected.sourceId(), selected.sourceUri());
            byte[] bytes = loader.load(LaunchRequest.forVariant(selected));
            GameVariant current = catalog.revalidateExactContent(resolution,
                    variant -> validator.validate(variant.sourceId(), variant.sourceUri()))
                    .orElseThrow(() -> new ContentException(FailureCode.CATALOG_CHANGED, null));
            return new LoadedContent(current, bytes);
        } catch (ExactRomLoader.LoadException failure) {
            throw new ContentException(FailureCode.LOAD_FAILED, failure);
        } catch (SecurityException failure) {
            throw new ContentException(FailureCode.SOURCE_ACCESS_DENIED, failure);
        }
    }

    @FunctionalInterface
    public interface ReadAccessValidator {
        /** Current source/read-grant check only; do not read ROMs or mutate catalog/owner state. */
        void validate(String sourceId, String sourceUri) throws SecurityException;
    }

    public enum FailureCode {
        VARIANT_NOT_FOUND, NOT_PLAYABLE, CATALOG_CHANGED, SOURCE_ACCESS_DENIED, LOAD_FAILED
    }

    public static final class ContentException extends Exception {
        private final FailureCode code;
        private ContentException(FailureCode code, Throwable cause) {
            super(code.name(), cause);
            this.code = code;
        }
        public FailureCode code() { return code; }
    }

    public static final class LoadedContent {
        private final GameVariant variant;
        private final byte[] bytes;
        private LoadedContent(GameVariant variant, byte[] bytes) {
            this.variant = variant;
            this.bytes = bytes.clone();
        }
        public GameVariant variant() { return variant; }
        public byte[] bytes() { return bytes.clone(); }
    }
}
