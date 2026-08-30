package com.flynes.emu.catalog.scan;

import com.flynes.emu.catalog.DomainValidation;
import com.flynes.emu.catalog.StableIds;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;

/** Pure provider-neutral input. The stable document key is hashed and never copied to results. */
public record PackageCandidate(
        String stableDocumentKey,
        String displayFilename,
        String contentLocator,
        ContentOpener opener) {

    public PackageCandidate {
        stableDocumentKey = DomainValidation.requireNonBlank(
                stableDocumentKey, "stable document key");
        displayFilename = DomainValidation.requireNonBlank(
                displayFilename, "display filename");
        contentLocator = DomainValidation.requireNonBlank(
                contentLocator, "package content locator");
        opener = DomainValidation.requireNonNull(opener, "package content opener");
    }

    public static PackageCandidate bytes(
            String stableDocumentKey, String displayFilename, byte[] content) {
        DomainValidation.requireNonNull(content, "package content");
        byte[] owned = content.clone();
        String memoryLocator = "memory://" + StableIds.packageId(
                "memory", stableDocumentKey).substring("pkg:".length());
        return new PackageCandidate(
                stableDocumentKey,
                displayFilename,
                memoryLocator,
                () -> new ByteArrayInputStream(owned));
    }

    @FunctionalInterface
    public interface ContentOpener {
        InputStream open() throws IOException, SecurityException;
    }
}
