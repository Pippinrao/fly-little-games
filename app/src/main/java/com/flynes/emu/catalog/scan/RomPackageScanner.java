package com.flynes.emu.catalog.scan;

import com.flynes.emu.catalog.CanonicalGame;
import com.flynes.emu.catalog.DomainValidation;
import com.flynes.emu.catalog.EntryOutcome;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.catalog.PackageOutcome;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomHashes;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.RomVariant;
import com.flynes.emu.catalog.ScanIssue;
import com.flynes.emu.catalog.ScanResult;
import com.flynes.emu.catalog.StableIds;
import com.flynes.emu.catalog.TitleCandidate;
import com.flynes.emu.catalog.ZipEntryNameDecoder;
import com.flynes.emu.catalog.ZipNameEncoding;

import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.Charset;
import java.nio.charset.CodingErrorAction;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.zip.CRC32;

/** Deterministic, provider-neutral scanner for raw and full multi-entry ZIP packages. */
public final class RomPackageScanner {
    private static final int ZIP_LOCAL_0 = 'P';
    private static final int ZIP_LOCAL_1 = 'K';
    private static final byte[] GAME_BOY_LOGO = new byte[]{
            (byte) 0xCE, (byte) 0xED, 0x66, 0x66, (byte) 0xCC, 0x0D, 0x00, 0x0B,
            0x03, 0x73, 0x00, (byte) 0x83, 0x00, 0x0C, 0x00, 0x0D,
            0x00, 0x08, 0x11, 0x1F, (byte) 0x88, (byte) 0x89, 0x00, 0x0E,
            (byte) 0xDC, (byte) 0xCC, 0x6E, (byte) 0xE6, (byte) 0xDD, (byte) 0xDD,
            (byte) 0xD9, (byte) 0x99, (byte) 0xBB, (byte) 0xBB, 0x67, 0x63,
            0x6E, 0x0E, (byte) 0xEC, (byte) 0xCC, (byte) 0xDD, (byte) 0xDC,
            (byte) 0x99, (byte) 0x9F, (byte) 0xBB, (byte) 0xB9, 0x33, 0x3E};

    private final ScanLimits limits;
    private final CanonicalIdResolver canonicalIds;

    public RomPackageScanner(ScanLimits limits) {
        this(limits, (payloadSha256, format) -> StableIds.provisionalGameId(payloadSha256));
    }

    public RomPackageScanner(ScanLimits limits, CanonicalIdResolver canonicalIds) {
        this.limits = DomainValidation.requireNonNull(limits, "scan limits");
        this.canonicalIds = DomainValidation.requireNonNull(
                canonicalIds, "canonical ID resolver");
    }

    public ScanResult scan(RomSource source, List<PackageCandidate> candidates) {
        DomainValidation.requireNonNull(source, "ROM source");
        DomainValidation.requireNonNull(candidates, "package candidates");
        ArrayList<CandidateEnvelope> ordered = new ArrayList<>(candidates.size());
        Map<String, Integer> idCounts = new HashMap<>();
        for (PackageCandidate candidate : candidates) {
            DomainValidation.requireNonNull(candidate, "package candidate");
            String packageId = StableIds.packageId(source.id(), candidate.stableDocumentKey());
            ordered.add(new CandidateEnvelope(packageId, candidate));
            Integer count = idCounts.get(packageId);
            idCounts.put(packageId, count == null ? 1 : count + 1);
        }
        ordered.sort(Comparator
                .comparing(CandidateEnvelope::packageId)
                .thenComparing(item -> item.candidate().displayFilename()));

        Collector collector = new Collector();
        if (!source.isUsable()) {
            for (CandidateEnvelope candidate : ordered) {
                collector.packageOutcomes.add(new PackageOutcome(
                        candidate.packageId(),
                        PackageOutcome.Status.ERROR,
                        PackageOutcome.Reason.SOURCE_UNAVAILABLE));
            }
            collector.issues.add(new ScanIssue(
                    ScanIssue.Code.SOURCE_UNAVAILABLE,
                    ScanIssue.Severity.FATAL,
                    source.id(),
                    null));
            return collector.finish();
        }

        for (CandidateEnvelope envelope : ordered) {
            if (idCounts.get(envelope.packageId()) > 1) {
                collector.packageOutcomes.add(new PackageOutcome(
                        envelope.packageId(),
                        PackageOutcome.Status.ERROR,
                        PackageOutcome.Reason.DUPLICATE_DOCUMENT_KEY));
                continue;
            }
            scanOne(source, envelope, collector);
        }
        return collector.finish();
    }

    private void scanOne(RomSource source, CandidateEnvelope envelope, Collector collector) {
        byte[] physicalBytes;
        try {
            InputStream opened = envelope.candidate().opener().open();
            if (opened == null) {
                collector.errorPackage(
                        source.id(), envelope.packageId(), PackageOutcome.Reason.OPEN_FAILED,
                        ScanIssue.Code.IO_ERROR);
                return;
            }
            try (InputStream input = opened) {
                physicalBytes = BoundedZipArchive.readBounded(input, limits.maxPackageBytes());
            }
        } catch (BoundedZipArchive.ArchiveException failure) {
            collector.errorPackage(
                    source.id(),
                    envelope.packageId(),
                    mapPackageReason(failure.code()),
                    ScanIssue.Code.OVERSIZE);
            return;
        } catch (SecurityException failure) {
            collector.packageOutcomes.add(new PackageOutcome(
                    envelope.packageId(),
                    PackageOutcome.Status.ERROR,
                    PackageOutcome.Reason.SOURCE_UNAVAILABLE));
            collector.issues.add(new ScanIssue(
                    ScanIssue.Code.PERMISSION_REVOKED,
                    ScanIssue.Severity.FATAL,
                    source.id(),
                    envelope.packageId()));
            return;
        } catch (IOException failure) {
            collector.errorPackage(
                    source.id(), envelope.packageId(), PackageOutcome.Reason.IO_ERROR,
                    ScanIssue.Code.IO_ERROR);
            return;
        }

        String physicalSha256 = digest("SHA-256", physicalBytes);
        if (isZipMagic(physicalBytes)) {
            scanZip(source, envelope, physicalBytes, physicalSha256, collector);
        } else {
            scanRaw(source, envelope, physicalBytes, physicalSha256, collector);
        }
    }

    private void scanRaw(
            RomSource source,
            CandidateEnvelope envelope,
            byte[] payload,
            String physicalSha256,
            Collector collector) {
        String entryId = StableIds.entryOutcomeId(
                envelope.packageId(), StableIds.RAW_LOCATOR);
        if (isExecutableName(envelope.candidate().displayFilename())
                || isExecutableName(envelope.candidate().contentLocator())) {
            collector.entryOutcomes.add(new EntryOutcome(
                    envelope.packageId(), entryId,
                    EntryOutcome.Status.SKIPPED, EntryOutcome.Reason.EXECUTABLE));
            collector.packageOutcomes.add(new PackageOutcome(
                    envelope.packageId(), PackageOutcome.Status.SKIPPED,
                    PackageOutcome.Reason.NO_SUPPORTED_PAYLOADS));
            return;
        }
        if (payload.length > limits.maxPayloadBytes()) {
            collector.entryOutcomes.add(new EntryOutcome(
                    envelope.packageId(), entryId,
                    EntryOutcome.Status.ERROR,
                    EntryOutcome.Reason.PAYLOAD_LIMIT_EXCEEDED));
            collector.packageOutcomes.add(new PackageOutcome(
                    envelope.packageId(),
                    PackageOutcome.Status.ERROR,
                    PackageOutcome.Reason.PAYLOAD_LIMIT_EXCEEDED));
            return;
        }
        RomPayloadParser.Parsed parsed = RomPayloadParser.parse(payload);
        if (parsed == null) {
            EntryOutcome.Reason reason = unsupportedReason(payload);
            collector.entryOutcomes.add(new EntryOutcome(
                    envelope.packageId(), entryId,
                    EntryOutcome.Status.SKIPPED, reason));
            collector.packageOutcomes.add(new PackageOutcome(
                    envelope.packageId(),
                    PackageOutcome.Status.SKIPPED,
                    reason == EntryOutcome.Reason.UNKNOWN_FORMAT
                            ? PackageOutcome.Reason.UNKNOWN_FORMAT
                            : PackageOutcome.Reason.NO_SUPPORTED_PAYLOADS));
            return;
        }

        RomHashes hashes = hashes(payload, physicalSha256);
        RomVariant variant;
        try {
            variant = variant(
                    envelope,
                    hashes,
                    parsed,
                    null,
                    null,
                    null);
        } catch (CanonicalResolutionException invalidOverride) {
            collector.entryOutcomes.add(new EntryOutcome(
                    envelope.packageId(), entryId,
                    EntryOutcome.Status.ERROR,
                    EntryOutcome.Reason.CANONICAL_ID_RESOLUTION_FAILED));
            collector.packageOutcomes.add(new PackageOutcome(
                    envelope.packageId(),
                    PackageOutcome.Status.ERROR,
                    PackageOutcome.Reason.CANONICAL_ID_RESOLUTION_FAILED));
            collector.issues.add(new ScanIssue(
                    ScanIssue.Code.CANONICAL_ID_RESOLUTION_FAILED,
                    ScanIssue.Severity.WARNING,
                    source.id(),
                    envelope.packageId()));
            return;
        }
        PhysicalPackage physicalPackage = new PhysicalPackage(
                envelope.packageId(),
                source,
                envelope.candidate().contentLocator(),
                envelope.candidate().displayFilename(),
                PackageFormat.RAW,
                physicalSha256,
                Collections.singletonList(variant));
        collector.packages.add(physicalPackage);
        collector.packageOutcomes.add(new PackageOutcome(
                envelope.packageId(), PackageOutcome.Status.INDEXED,
                PackageOutcome.Reason.INDEXED));
        collector.entryOutcomes.add(new EntryOutcome(
                envelope.packageId(), entryId,
                EntryOutcome.Status.INDEXED,
                parsed.compatibility().state() == com.flynes.emu.catalog.CompatibilityState.INVALID
                        ? EntryOutcome.Reason.INVALID_ROM : EntryOutcome.Reason.INDEXED));
    }

    private void scanZip(
            RomSource source,
            CandidateEnvelope envelope,
            byte[] physicalBytes,
            String physicalSha256,
            Collector collector) {
        BoundedZipArchive.Archive archive;
        try {
            archive = BoundedZipArchive.fromBytes(physicalBytes, limits);
        } catch (BoundedZipArchive.ArchiveException failure) {
            collector.errorPackage(
                    source.id(),
                    envelope.packageId(),
                    mapPackageReason(failure.code()),
                    ScanIssue.Code.INVALID_PACKAGE);
            return;
        }

        Collector staged = new Collector();
        ArrayList<RomVariant> variants = new ArrayList<>();
        Map<String, Integer> rawNameCounts = new HashMap<>();
        Map<String, Set<String>> caseFoldedLocators = new HashMap<>();
        boolean payloadError = false;
        boolean canonicalResolutionError = false;
        for (BoundedZipArchive.Entry entry : archive.entries()) {
            String rawLocator = rawLocator(entry);
            String entryId = StableIds.entryOutcomeId(envelope.packageId(), rawLocator);
            String rawNameHex = entry.identity().rawNameHex();
            Integer rawCount = rawNameCounts.get(rawNameHex);
            rawNameCounts.put(rawNameHex, rawCount == null ? 1 : rawCount + 1);

            DecodedEntryName decoded;
            try {
                decoded = decodeEntryName(entry);
            } catch (IllegalArgumentException malformedName) {
                staged.entryOutcomes.add(new EntryOutcome(
                        envelope.packageId(), entryId,
                        EntryOutcome.Status.SKIPPED,
                        EntryOutcome.Reason.INVALID_PATH));
                continue;
            }
            String folded = decoded.path().toLowerCase(Locale.ROOT);
            Set<String> locators = caseFoldedLocators.get(folded);
            if (locators == null) {
                locators = new HashSet<>();
                caseFoldedLocators.put(folded, locators);
            }
            locators.add(rawLocator);
            if (decoded.unicodePathRejected()) {
                staged.issues.add(new ScanIssue(
                        ScanIssue.Code.UNICODE_PATH_REJECTED,
                        ScanIssue.Severity.WARNING,
                        source.id(),
                        envelope.packageId()));
            }
            if (entry.isDirectory()) {
                staged.entryOutcomes.add(new EntryOutcome(
                        envelope.packageId(), entryId,
                        EntryOutcome.Status.SKIPPED, EntryOutcome.Reason.DIRECTORY));
                continue;
            }
            if (!isSafeRelativePath(decoded.path())
                    || !isSafeRelativePath(decoded.rawPath())) {
                staged.entryOutcomes.add(new EntryOutcome(
                        envelope.packageId(), entryId,
                        EntryOutcome.Status.SKIPPED, EntryOutcome.Reason.INVALID_PATH));
                continue;
            }
            if (endsWithAsciiIgnoreCase(entry.identity().rawNameBytes(), ".exe")
                    || isExecutableName(decoded.path())) {
                staged.entryOutcomes.add(new EntryOutcome(
                        envelope.packageId(), entryId,
                        EntryOutcome.Status.SKIPPED, EntryOutcome.Reason.EXECUTABLE));
                continue;
            }
            if (entry.uncompressedSize() > limits.maxPayloadBytes()) {
                payloadError = true;
                staged.entryOutcomes.add(new EntryOutcome(
                        envelope.packageId(), entryId,
                        EntryOutcome.Status.ERROR,
                        EntryOutcome.Reason.PAYLOAD_LIMIT_EXCEEDED));
                continue;
            }
            byte[] payload;
            try {
                payload = entry.readPayload();
            } catch (BoundedZipArchive.ArchiveException failure) {
                collector.errorPackage(
                        source.id(), envelope.packageId(), mapPackageReason(failure.code()),
                        ScanIssue.Code.INVALID_PACKAGE);
                return;
            }
            RomPayloadParser.Parsed parsed = RomPayloadParser.parse(payload);
            if (parsed == null) {
                staged.entryOutcomes.add(new EntryOutcome(
                        envelope.packageId(), entryId,
                        EntryOutcome.Status.SKIPPED,
                        unsupportedReason(payload)));
                continue;
            }
            RomHashes hashes = hashes(payload, physicalSha256);
            try {
                variants.add(variant(
                        envelope,
                        hashes,
                        parsed,
                        decoded.path(),
                        entry.identity(),
                        decoded.encoding()));
            } catch (CanonicalResolutionException invalidOverride) {
                canonicalResolutionError = true;
                staged.entryOutcomes.add(new EntryOutcome(
                        envelope.packageId(), entryId,
                        EntryOutcome.Status.ERROR,
                        EntryOutcome.Reason.CANONICAL_ID_RESOLUTION_FAILED));
                continue;
            }
            staged.entryOutcomes.add(new EntryOutcome(
                    envelope.packageId(), entryId,
                    EntryOutcome.Status.INDEXED,
                    parsed.compatibility().state()
                            == com.flynes.emu.catalog.CompatibilityState.INVALID
                            ? EntryOutcome.Reason.INVALID_ROM : EntryOutcome.Reason.INDEXED));
        }

        if (containsCountAboveOne(rawNameCounts)) {
            staged.issues.add(new ScanIssue(
                    ScanIssue.Code.DUPLICATE_ENTRY_NAME,
                    ScanIssue.Severity.WARNING,
                    source.id(),
                    envelope.packageId()));
        }
        if (containsSetAboveOne(caseFoldedLocators)) {
            staged.issues.add(new ScanIssue(
                    ScanIssue.Code.CASE_COLLISION,
                    ScanIssue.Severity.WARNING,
                    source.id(),
                    envelope.packageId()));
        }
        if (canonicalResolutionError) {
            staged.issues.add(new ScanIssue(
                    ScanIssue.Code.CANONICAL_ID_RESOLUTION_FAILED,
                    ScanIssue.Severity.WARNING,
                    source.id(),
                    envelope.packageId()));
        }
        variants.sort(Comparator.comparing(RomVariant::id));
        if (!variants.isEmpty()) {
            staged.packages.add(new PhysicalPackage(
                    envelope.packageId(),
                    source,
                    envelope.candidate().contentLocator(),
                    envelope.candidate().displayFilename(),
                    PackageFormat.ZIP,
                    physicalSha256,
                    variants));
            staged.packageOutcomes.add(new PackageOutcome(
                    envelope.packageId(), PackageOutcome.Status.INDEXED,
                    PackageOutcome.Reason.INDEXED));
        } else if (canonicalResolutionError) {
            staged.packageOutcomes.add(new PackageOutcome(
                    envelope.packageId(), PackageOutcome.Status.ERROR,
                    PackageOutcome.Reason.CANONICAL_ID_RESOLUTION_FAILED));
        } else if (payloadError) {
            staged.packageOutcomes.add(new PackageOutcome(
                    envelope.packageId(), PackageOutcome.Status.ERROR,
                    PackageOutcome.Reason.PAYLOAD_LIMIT_EXCEEDED));
        } else {
            staged.packageOutcomes.add(new PackageOutcome(
                    envelope.packageId(), PackageOutcome.Status.SKIPPED,
                    PackageOutcome.Reason.NO_SUPPORTED_PAYLOADS));
        }
        collector.merge(staged);
    }

    private RomVariant variant(
            CandidateEnvelope envelope,
            RomHashes hashes,
            RomPayloadParser.Parsed parsed,
            String entryPath,
            com.flynes.emu.catalog.ZipEntryIdentity entryIdentity,
            ZipNameEncoding nameEncoding) throws CanonicalResolutionException {
        String locator = entryIdentity == null
                ? StableIds.RAW_LOCATOR
                : entryIdentity.rawNameHex() + "@" + entryIdentity.localHeaderOffset();
        String variantId = StableIds.variantId(
                envelope.packageId(), locator, hashes.payloadSha256());
        String canonicalId = resolveCanonicalId(hashes.payloadSha256(), parsed.format());
        ArrayList<TitleCandidate> titles = new ArrayList<>(2);
        titles.add(classifiedTitle(
                titleFromPath(envelope.candidate().displayFilename()),
                TitleCandidate.Origin.OUTER_FILENAME));
        if (entryPath != null) {
            titles.add(classifiedTitle(
                    titleFromPath(entryPath),
                    TitleCandidate.Origin.ZIP_ENTRY_NAME));
        }
        CanonicalGame canonicalGame = new CanonicalGame(
                canonicalId, titles, Collections.<String>emptyList());
        return new RomVariant(
                variantId,
                canonicalGame,
                entryPath,
                parsed.format(),
                parsed.compatibility(),
                hashes,
                parsed.analysis(),
                entryIdentity,
                nameEncoding);
    }

    private String resolveCanonicalId(String payloadSha256, com.flynes.emu.catalog.RomFormat format)
            throws CanonicalResolutionException {
        String resolved;
        try {
            resolved = canonicalIds.canonicalGameId(payloadSha256, format);
        } catch (RuntimeException resolverFailure) {
            throw new CanonicalResolutionException();
        }
        if (!isValidCanonicalId(resolved)) {
            throw new CanonicalResolutionException();
        }
        return resolved;
    }

    private static boolean isValidCanonicalId(String value) {
        if (DomainValidation.isBlank(value) || value.length() > 256) {
            return false;
        }
        for (int index = 0; index < value.length(); index++) {
            char character = value.charAt(index);
            boolean valid = character >= 'a' && character <= 'z'
                    || character >= 'A' && character <= 'Z'
                    || character >= '0' && character <= '9'
                    || character == ':'
                    || character == '.'
                    || character == '_'
                    || character == '-';
            if (!valid || index == 0 && !isAsciiLetterOrDigit(character)) {
                return false;
            }
        }
        return true;
    }

    private static boolean isAsciiLetterOrDigit(char value) {
        return value >= 'a' && value <= 'z'
                || value >= 'A' && value <= 'Z'
                || value >= '0' && value <= '9';
    }

    private static TitleCandidate classifiedTitle(String value, TitleCandidate.Origin origin) {
        return new TitleCandidate(
                value,
                titleLanguage(value),
                origin,
                TitleCandidate.Confidence.LOW,
                TitleCandidate.ReviewState.NEEDS_REVIEW);
    }

    private static TitleCandidate.Language titleLanguage(String value) {
        boolean latin = false;
        for (int offset = 0; offset < value.length();) {
            int codePoint = value.codePointAt(offset);
            Character.UnicodeScript script = Character.UnicodeScript.of(codePoint);
            if (script == Character.UnicodeScript.HAN) {
                return TitleCandidate.Language.ZH_HANS;
            }
            latin |= Character.isLetter(codePoint)
                    && script == Character.UnicodeScript.LATIN;
            offset += Character.charCount(codePoint);
        }
        return latin ? TitleCandidate.Language.EN : TitleCandidate.Language.UNKNOWN;
    }

    private static DecodedEntryName decodeEntryName(BoundedZipArchive.Entry entry) {
        byte[] rawName = entry.rawName();
        ZipNameEncoding fallback;
        if ((entry.flags() & 0x0800) != 0) {
            fallback = ZipNameEncoding.CP437;
        } else {
            String gbCandidate = decodeStrictOrNull(rawName, Charset.forName("GB18030"));
            String cp437Candidate = decodeStrictOrNull(rawName, Charset.forName("IBM437"));
            fallback = isLikelyGb18030(gbCandidate, cp437Candidate)
                    ? ZipNameEncoding.GB18030 : ZipNameEncoding.CP437;
        }
        ZipEntryNameDecoder.DecodedName rawDecoded = ZipEntryNameDecoder.decode(
                rawName, entry.flags(), new byte[0], fallback);
        ZipEntryNameDecoder.DecodedName decoded = ZipEntryNameDecoder.decode(
                rawName, entry.flags(), entry.centralExtra(), fallback);
        return new DecodedEntryName(
                decoded.displayPath(),
                rawDecoded.displayPath(),
                decoded.encoding(),
                decoded.unicodePathRejected());
    }

    private static String decodeStrictOrNull(byte[] bytes, Charset charset) {
        try {
            return charset.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes))
                    .toString();
        } catch (CharacterCodingException failure) {
            return null;
        }
    }

    private static boolean isLikelyGb18030(String value, String cp437Candidate) {
        if (value == null) {
            return false;
        }
        int hanCount = 0;
        for (int offset = 0; offset < value.length();) {
            int codePoint = value.codePointAt(offset);
            Character.UnicodeScript script = Character.UnicodeScript.of(codePoint);
            if (script == Character.UnicodeScript.HAN) {
                hanCount++;
            }
            offset += Character.charCount(codePoint);
        }
        if (hanCount >= 2) {
            return true;
        }
        if (hanCount != 1 || cp437Candidate == null) {
            return false;
        }
        for (int offset = 0; offset < cp437Candidate.length();) {
            int codePoint = cp437Candidate.codePointAt(offset);
            Character.UnicodeBlock block = Character.UnicodeBlock.of(codePoint);
            if (block == Character.UnicodeBlock.BOX_DRAWING
                    || block == Character.UnicodeBlock.BLOCK_ELEMENTS) {
                return true;
            }
            offset += Character.charCount(codePoint);
        }
        return false;
    }

    private static boolean isSafeRelativePath(String path) {
        if (DomainValidation.isBlank(path)
                || path.indexOf('\0') >= 0
                || path.startsWith("/")
                || path.startsWith("\\")) {
            return false;
        }
        if (path.length() >= 3
                && Character.isLetter(path.charAt(0))
                && path.charAt(1) == ':'
                && (path.charAt(2) == '/' || path.charAt(2) == '\\')) {
            return false;
        }
        String[] segments = path.replace('\\', '/').split("/", -1);
        for (String segment : segments) {
            if (segment.length() == 0 || segment.equals(".") || segment.equals("..")) {
                return false;
            }
        }
        return true;
    }

    private static EntryOutcome.Reason unsupportedReason(byte[] payload) {
        if (isZipMagic(payload)) {
            return EntryOutcome.Reason.NESTED_ARCHIVE;
        }
        if (payload.length >= 2 && payload[0] == 'M' && payload[1] == 'Z') {
            return EntryOutcome.Reason.EXECUTABLE;
        }
        if (isGameBoy(payload)) {
            return EntryOutcome.Reason.GAME_BOY;
        }
        return isText(payload)
                ? EntryOutcome.Reason.SIDECAR : EntryOutcome.Reason.UNKNOWN_FORMAT;
    }

    private static boolean isZipMagic(byte[] bytes) {
        return bytes.length >= 4
                && (bytes[0] & 0xFF) == ZIP_LOCAL_0
                && (bytes[1] & 0xFF) == ZIP_LOCAL_1
                && ((bytes[2] == 3 && bytes[3] == 4)
                || (bytes[2] == 5 && bytes[3] == 6)
                || (bytes[2] == 7 && bytes[3] == 8));
    }

    private static boolean isGameBoy(byte[] payload) {
        if (payload.length < 0x104 + GAME_BOY_LOGO.length) {
            return false;
        }
        for (int index = 0; index < GAME_BOY_LOGO.length; index++) {
            if (payload[0x104 + index] != GAME_BOY_LOGO[index]) {
                return false;
            }
        }
        return true;
    }

    private static boolean isText(byte[] payload) {
        if (payload.length == 0) {
            return true;
        }
        int inspected = Math.min(payload.length, 4096);
        for (int index = 0; index < inspected; index++) {
            int value = payload[index] & 0xFF;
            if (value == 0) {
                return false;
            }
            if (value < 0x20 && value != '\n' && value != '\r' && value != '\t') {
                return false;
            }
        }
        return true;
    }

    private static RomHashes hashes(byte[] payload, String physicalSha256) {
        CRC32 crc32 = new CRC32();
        crc32.update(payload);
        return new RomHashes(
                digest("SHA-1", payload),
                digest("SHA-256", payload),
                physicalSha256,
                String.format(Locale.ROOT, "%08X", crc32.getValue()));
    }

    private static String digest(String algorithm, byte[] bytes) {
        try {
            StringBuilder hex = new StringBuilder();
            for (byte value : MessageDigest.getInstance(algorithm).digest(bytes)) {
                hex.append(String.format(Locale.ROOT, "%02X", value & 0xFF));
            }
            return hex.toString();
        } catch (NoSuchAlgorithmException impossible) {
            throw new IllegalStateException(algorithm + " is required", impossible);
        }
    }

    private static String titleFromPath(String value) {
        String normalized = value.replace('\\', '/');
        int slash = normalized.lastIndexOf('/');
        String basename = slash >= 0 ? normalized.substring(slash + 1) : normalized;
        int dot = basename.lastIndexOf('.');
        String title = dot > 0 ? basename.substring(0, dot) : basename;
        return DomainValidation.isBlank(title) ? value : title;
    }

    private static String rawLocator(BoundedZipArchive.Entry entry) {
        return entry.identity().rawNameHex() + "@" + entry.identity().localHeaderOffset();
    }

    private static boolean isExecutableName(String value) {
        int query = value.indexOf('?');
        if (query >= 0) {
            value = value.substring(0, query);
        }
        int fragment = value.indexOf('#');
        if (fragment >= 0) {
            value = value.substring(0, fragment);
        }
        return value.toLowerCase(Locale.ROOT).endsWith(".exe");
    }

    private static boolean endsWithAsciiIgnoreCase(byte[] value, String suffix) {
        if (value.length < suffix.length()) {
            return false;
        }
        int start = value.length - suffix.length();
        for (int index = 0; index < suffix.length(); index++) {
            int actual = value[start + index] & 0xFF;
            int expected = suffix.charAt(index);
            if (actual >= 'A' && actual <= 'Z') {
                actual += 'a' - 'A';
            }
            if (actual != expected) {
                return false;
            }
        }
        return true;
    }

    private static boolean containsCountAboveOne(Map<String, Integer> counts) {
        for (Integer count : counts.values()) {
            if (count > 1) {
                return true;
            }
        }
        return false;
    }

    private static boolean containsSetAboveOne(Map<String, Set<String>> values) {
        for (Set<String> value : values.values()) {
            if (value.size() > 1) {
                return true;
            }
        }
        return false;
    }

    private static PackageOutcome.Reason mapPackageReason(BoundedZipArchive.Code code) {
        return switch (code) {
            case PACKAGE_LIMIT_EXCEEDED -> PackageOutcome.Reason.PACKAGE_LIMIT_EXCEEDED;
            case ENTRY_LIMIT_EXCEEDED -> PackageOutcome.Reason.ZIP_ENTRY_LIMIT_EXCEEDED;
            case INFLATED_LIMIT_EXCEEDED -> PackageOutcome.Reason.ZIP_INFLATED_LIMIT_EXCEEDED;
            case PAYLOAD_LIMIT_EXCEEDED -> PackageOutcome.Reason.PAYLOAD_LIMIT_EXCEEDED;
            case NAME_LIMIT_EXCEEDED -> PackageOutcome.Reason.ZIP_NAME_LIMIT_EXCEEDED;
            case RATIO_LIMIT_EXCEEDED -> PackageOutcome.Reason.ZIP_RATIO_LIMIT_EXCEEDED;
            case INVALID_ZIP, ENCRYPTED, UNSUPPORTED_COMPRESSION, ENTRY_MISSING ->
                    PackageOutcome.Reason.INVALID_ZIP;
        };
    }

    private record CandidateEnvelope(String packageId, PackageCandidate candidate) {
    }

    private record DecodedEntryName(
            String path,
            String rawPath,
            ZipNameEncoding encoding,
            boolean unicodePathRejected) {
    }

    /** Deliberately carries no resolver-provided message, path, or URI. */
    private static final class CanonicalResolutionException extends Exception {
    }

    private static final class Collector {
        final ArrayList<PhysicalPackage> packages = new ArrayList<>();
        final ArrayList<PackageOutcome> packageOutcomes = new ArrayList<>();
        final ArrayList<EntryOutcome> entryOutcomes = new ArrayList<>();
        final ArrayList<ScanIssue> issues = new ArrayList<>();

        void errorPackage(
                String sourceId,
                String packageId,
                PackageOutcome.Reason reason,
                ScanIssue.Code issueCode) {
            packageOutcomes.add(new PackageOutcome(
                    packageId, PackageOutcome.Status.ERROR, reason));
            issues.add(new ScanIssue(
                    issueCode, ScanIssue.Severity.WARNING, sourceId, packageId));
        }

        void merge(Collector staged) {
            packages.addAll(staged.packages);
            packageOutcomes.addAll(staged.packageOutcomes);
            entryOutcomes.addAll(staged.entryOutcomes);
            issues.addAll(staged.issues);
        }

        ScanResult finish() {
            packages.sort(Comparator.comparing(PhysicalPackage::id));
            packageOutcomes.sort(Comparator
                    .comparing(PackageOutcome::packageId)
                    .thenComparing(PackageOutcome::status)
                    .thenComparing(PackageOutcome::reason));
            entryOutcomes.sort(Comparator
                    .comparing(EntryOutcome::packageId)
                    .thenComparing(EntryOutcome::entryId)
                    .thenComparing(EntryOutcome::status)
                    .thenComparing(EntryOutcome::reason));
            issues.sort(Comparator
                    .comparing(ScanIssue::sourceId, Comparator.nullsFirst(String::compareTo))
                    .thenComparing(ScanIssue::packageId, Comparator.nullsFirst(String::compareTo))
                    .thenComparing(ScanIssue::code)
                    .thenComparing(ScanIssue::severity));
            return new ScanResult(packages, packageOutcomes, entryOutcomes, issues);
        }
    }
}
