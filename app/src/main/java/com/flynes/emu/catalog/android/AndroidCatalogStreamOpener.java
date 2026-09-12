package com.flynes.emu.catalog.android;

import android.content.Context;
import android.net.Uri;

import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.persistence.CatalogPackage;
import com.flynes.emu.catalog.persistence.CatalogRepository;
import com.flynes.emu.catalog.persistence.SourceCatalogState;
import com.flynes.emu.catalog.source.DocumentLocatorShape;
import com.flynes.emu.launch.ExactRomLoader;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.util.function.Predicate;

/** Strict runtime opener: only a current catalog package locator can be reopened. */
public final class AndroidCatalogStreamOpener implements ExactRomLoader.StreamOpener {
    private final Context context;
    private final CatalogRepository repository;
    private final Predicate<String> persistedRead;

    public AndroidCatalogStreamOpener(
            Context context,
            CatalogRepository repository,
            Predicate<String> persistedRead) {
        if (context == null || repository == null || persistedRead == null) {
            throw new NullPointerException();
        }
        this.context = context.getApplicationContext();
        this.repository = repository;
        this.persistedRead = persistedRead;
    }

    @Override
    public InputStream open(String sourceId, String sourceUri) throws IOException {
        SourceCatalogState source = repository.state().sources().get(sourceId);
        if (source == null) throw failure(FailureCode.SOURCE_UNKNOWN);
        CatalogPackage exact = null;
        for (CatalogPackage item : source.packages().values()) {
            if (item.physicalPackage().sourceUri().equals(sourceUri)) {
                if (exact != null) throw failure(FailureCode.LOCATOR_AMBIGUOUS);
                exact = item;
            }
        }
        if (exact == null) throw failure(FailureCode.LOCATOR_UNKNOWN);
        if (exact.freshness() != CatalogPackage.Freshness.FRESH
                || !source.source().isUsable()) {
            throw failure(FailureCode.SOURCE_STALE);
        }
        if (source.source().type() == RomSource.Type.BUILTIN) {
            if (!sourceUri.startsWith("asset:///") || !sourceUri.equals(source.source().uri())) {
                throw failure(FailureCode.LOCATOR_UNKNOWN);
            }
            return context.getAssets().open(sourceUri.substring("asset:///".length()));
        }
        if (!persistedRead.test(source.source().uri())) {
            throw failure(FailureCode.PERMISSION_LOST, null);
        }
        if (!DocumentLocatorShape.isOpenableDocumentLocator(sourceUri)) {
            throw failure(FailureCode.LOCATOR_INVALID, null);
        }
        InputStream opened;
        try {
            opened = context.getContentResolver().openInputStream(Uri.parse(sourceUri));
        } catch (IllegalArgumentException malformed) {
            // Storage providers answer a non-document URI with IllegalArgumentException
            // ("Invalid URI"), not IOException; unwrapped it kills the launch thread.
            throw failure(FailureCode.LOCATOR_INVALID, malformed);
        } catch (SecurityException denied) {
            throw failure(FailureCode.PERMISSION_LOST, denied);
        }
        if (opened == null) throw new FileNotFoundException("provider returned null stream");
        return opened;
    }

    private static SourceOpenException failure(FailureCode code) {
        return new SourceOpenException(code, null);
    }

    private static SourceOpenException failure(FailureCode code, Throwable cause) {
        return new SourceOpenException(code, cause);
    }

    public enum FailureCode {
        SOURCE_UNKNOWN, LOCATOR_UNKNOWN, LOCATOR_AMBIGUOUS, SOURCE_STALE, PERMISSION_LOST,
        LOCATOR_INVALID
    }

    public static final class SourceOpenException extends SecurityException {
        private final FailureCode code;
        SourceOpenException(FailureCode code, Throwable cause) {
            super(code.name(), cause);
            this.code = code;
        }
        public FailureCode code() { return code; }
    }
}
