package com.flynes.emu.catalog;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/** Privacy-safe persistent IDs. Display names and raw provider identifiers never appear in IDs. */
public final class StableIds {
    public static final String RAW_LOCATOR = "RAW";

    private StableIds() {
    }

    public static String packageId(String sourceId, String stableDocumentKey) {
        return "pkg:" + digest(sourceId, stableDocumentKey);
    }

    public static String safSourceId(String treeLocator) {
        return "source:" + digest("SAF_TREE", treeLocator);
    }

    public static String variantId(
            String packageId, String exactRawLocator, String payloadSha256) {
        return "variant:" + digest(
                packageId,
                exactRawLocator,
                RomHashes.normalizedSha256(payloadSha256, "payload SHA-256"));
    }

    public static String provisionalGameId(String payloadSha256) {
        return "game:" + RomHashes.normalizedSha256(payloadSha256, "payload SHA-256");
    }

    public static String entryOutcomeId(String packageId, String exactRawLocator) {
        return "entry:" + digest(packageId, exactRawLocator);
    }

    private static String digest(String... parts) {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            for (String part : parts) {
                String checked = DomainValidation.requireNonBlank(part, "ID input");
                requireValidUnicode(checked);
                byte[] encoded = checked.getBytes(StandardCharsets.UTF_8);
                digest.update(ByteBuffer.allocate(4).putInt(encoded.length).array());
                digest.update(encoded);
            }
            return HexEncoding.upper(digest.digest());
        } catch (NoSuchAlgorithmException impossible) {
            throw new IllegalStateException("SHA-256 is required", impossible);
        }
    }

    private static void requireValidUnicode(String value) {
        // Java's UTF-8 encoder replaces unpaired surrogates, which would create ID collisions.
        for (int index = 0; index < value.length(); index++) {
            char current = value.charAt(index);
            if (Character.isHighSurrogate(current)) {
                if (index + 1 >= value.length()
                        || !Character.isLowSurrogate(value.charAt(index + 1))) {
                    throw new IllegalArgumentException("ID input must be valid Unicode");
                }
                index++;
            } else if (Character.isLowSurrogate(current)) {
                throw new IllegalArgumentException("ID input must be valid Unicode");
            }
        }
    }
}
