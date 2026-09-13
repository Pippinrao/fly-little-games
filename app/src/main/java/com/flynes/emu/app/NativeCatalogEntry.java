package com.flynes.emu.app;

import java.util.Arrays;
import java.util.Objects;

public record NativeCatalogEntry(
        byte[] sourceUuid,
        long payloadSize,
        long physicalSize,
        long expectedBytes,
        long prgBytes,
        long chrBytes,
        int mapper,
        int submapper,
        int diskSides,
        byte[] payloadSha1,
        byte[] payloadSha256,
        byte[] physicalSha256,
        byte[] payloadCrc32,
        int sourceScope,
        int packageFormat,
        int romFormat,
        int compatibilityState,
        int compatibilityReason,
        int freshness,
        int flags,
        String canonicalId,
        String variantId,
        String displayName,
        String relativePath,
        byte[] zipRawName,
        int zipLocalHeaderOffset,
        NativeGameTitle gameTitle) {
    public NativeCatalogEntry(
        byte[] sourceUuid,
        long payloadSize,
        long physicalSize,
        long expectedBytes,
        long prgBytes,
        long chrBytes,
        int mapper,
        int submapper,
        int diskSides,
        byte[] payloadSha1,
        byte[] payloadSha256,
        byte[] physicalSha256,
        byte[] payloadCrc32,
        int sourceScope,
        int packageFormat,
        int romFormat,
        int compatibilityState,
        int compatibilityReason,
        int freshness,
        int flags,
        String canonicalId,
        String variantId,
        String displayName,
        String relativePath,
        byte[] zipRawName,
        int zipLocalHeaderOffset) {
        this(sourceUuid, payloadSize, physicalSize, expectedBytes, prgBytes, chrBytes, mapper, submapper, diskSides, payloadSha1, payloadSha256, physicalSha256, payloadCrc32, sourceScope, packageFormat, romFormat, compatibilityState, compatibilityReason, freshness, flags, canonicalId, variantId, displayName, relativePath, zipRawName, zipLocalHeaderOffset, NativeGameTitle.UNKNOWN);
    }

    public NativeCatalogEntry {
        gameTitle = Objects.requireNonNull(gameTitle, "game title");
        sourceUuid = Objects.requireNonNull(sourceUuid, "source uuid").clone();
        payloadSha1 = Objects.requireNonNull(payloadSha1, "payload sha1").clone();
        payloadSha256 = Objects.requireNonNull(payloadSha256, "payload sha256").clone();
        physicalSha256 = Objects.requireNonNull(physicalSha256, "physical sha256").clone();
        payloadCrc32 = Objects.requireNonNull(payloadCrc32, "payload crc32").clone();
        canonicalId = Objects.requireNonNull(canonicalId, "canonical id");
        variantId = Objects.requireNonNull(variantId, "variant id");
        displayName = Objects.requireNonNull(displayName, "display name");
        relativePath = Objects.requireNonNull(relativePath, "relative path");
        zipRawName = Objects.requireNonNull(zipRawName, "raw ZIP name").clone();
    }

    @Override public byte[] sourceUuid() { return sourceUuid.clone(); }
    @Override public byte[] payloadSha1() { return payloadSha1.clone(); }
    @Override public byte[] payloadSha256() { return payloadSha256.clone(); }
    @Override public byte[] physicalSha256() { return physicalSha256.clone(); }
    @Override public byte[] payloadCrc32() { return payloadCrc32.clone(); }
    @Override public byte[] zipRawName() { return zipRawName.clone(); }

    @Override public boolean equals(Object other) {
        if (this == other) return true;
        if (!(other instanceof NativeCatalogEntry that)) return false;
        return payloadSize == that.payloadSize && physicalSize == that.physicalSize
                && expectedBytes == that.expectedBytes && prgBytes == that.prgBytes
                && chrBytes == that.chrBytes && mapper == that.mapper
                && submapper == that.submapper && diskSides == that.diskSides
                && sourceScope == that.sourceScope && packageFormat == that.packageFormat
                && romFormat == that.romFormat && compatibilityState == that.compatibilityState
                && compatibilityReason == that.compatibilityReason && freshness == that.freshness
                && flags == that.flags
                && zipLocalHeaderOffset == that.zipLocalHeaderOffset
                && Arrays.equals(zipRawName, that.zipRawName)
                && Arrays.equals(sourceUuid, that.sourceUuid)
                && Arrays.equals(payloadSha1, that.payloadSha1)
                && Arrays.equals(payloadSha256, that.payloadSha256)
                && Arrays.equals(physicalSha256, that.physicalSha256)
                && Arrays.equals(payloadCrc32, that.payloadCrc32)
                && canonicalId.equals(that.canonicalId) && variantId.equals(that.variantId)
                && displayName.equals(that.displayName) && relativePath.equals(that.relativePath)
                && gameTitle.equals(that.gameTitle);
    }

    @Override public int hashCode() {
        int result = Objects.hash(payloadSize, physicalSize, expectedBytes, prgBytes, chrBytes,
                mapper, submapper, diskSides, sourceScope, packageFormat, romFormat,
                compatibilityState, compatibilityReason, freshness, flags, canonicalId,
                variantId, displayName, relativePath, zipLocalHeaderOffset, gameTitle);
        result = 31 * result + Arrays.hashCode(zipRawName);
        result = 31 * result + Arrays.hashCode(sourceUuid);
        result = 31 * result + Arrays.hashCode(payloadSha1);
        result = 31 * result + Arrays.hashCode(payloadSha256);
        result = 31 * result + Arrays.hashCode(physicalSha256);
        result = 31 * result + Arrays.hashCode(payloadCrc32);
        return result;
    }
}
