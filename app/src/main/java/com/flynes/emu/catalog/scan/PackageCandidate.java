package com.flynes.emu.catalog.scan;

import com.flynes.emu.catalog.DomainValidation;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;

/** Pure provider-neutral input. The stable document key is hashed and never copied to results. */
public record PackageCandidate(
        String stableDocumentKey,
        String displayFilename,
        ContentOpener opener) {

    public PackageCandidate {
        stableDocumentKey = DomainValidation.requireNonBlank(
                stableDocumentKey, "stable document key");
        displayFilename = DomainValidation.requireNonBlank(
                displayFilename, "display filename");
        opener = DomainValidation.requireNonNull(opener, "package content opener");
    }

    public static PackageCandidate bytes(
            String stableDocumentKey, String displayFilename, byte[] content) {
        DomainValidation.requireNonNull(content, "package content");
        byte[] owned = content.clone();
        return new PackageCandidate(
                stableDocumentKey,
                displayFilename,
                () -> new ByteArrayInputStream(owned));
    }

    @FunctionalInterface
    public interface ContentOpener {
        InputStream open() throws IOException, SecurityException;
    }
}
