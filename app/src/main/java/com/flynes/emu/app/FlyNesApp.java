package com.flynes.emu.app;

import com.flynes.emu.catalog.persistence.CanonicalUserState;
import com.flynes.emu.settings.ControlLayoutRepository;
import com.flynes.emu.settings.FlySettingsSnapshot;
import com.flynes.emu.settings.NativeSettingsStore;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/** JNI owner of one fly_app_t. Unit tests use fakes; production loads libnescore. */
public final class FlyNesApp implements FlyCatalogCommands, NativeSettingsStore.Backend,
        ControlLayoutRepository.Backend, AutoCloseable {
    static {
        System.loadLibrary("nescore");
    }

    public static final int SCAN_FILE_FLAG_EXPECTED_PHYSICAL_SHA256 = 1;
    public static final int RESULT_OK = 0;
    public static final int RESULT_BUFFER_TOO_SMALL = -5;

    private long app;
    private long scan;

    private FlyNesApp(long app) {
        this.app = app;
    }

    public static FlyNesApp create(String dataRoot, String cacheRoot) {
        Objects.requireNonNull(dataRoot, "data root");
        Objects.requireNonNull(cacheRoot, "cache root");
        long[] out = new long[1];
        int result = nativeCreate(utf8(dataRoot), utf8(cacheRoot), out);
        if (result != RESULT_OK || out[0] == 0L) {
            throw new IllegalStateException("fly_app_create failed: " + result);
        }
        return new FlyNesApp(out[0]);
    }

    @Override public int scanBegin(byte[] sourceUuid, int sourceScope) {
        abortScan();
        long[] out = new long[1];
        int result = nativeScanBegin(app, sourceUuid, sourceScope, out);
        if (result == RESULT_OK) scan = out[0];
        return result;
    }

    @Override public int scanAddFile(
            String relativePath, String displayName, int borrowedFd, byte[] expectedPhysicalSha256) {
        return scanAddFile(relativePath, displayName, borrowedFd, expectedPhysicalSha256, new int[3]);
    }

    public int scanAddFile(String relativePath, String displayName, int borrowedFd,
            byte[] expectedPhysicalSha256, int[] outcome) {
        if (outcome == null || outcome.length < 3) throw new IllegalArgumentException("scan outcome");
        int flags = expectedPhysicalSha256 != null && expectedPhysicalSha256.length == 32
                ? SCAN_FILE_FLAG_EXPECTED_PHYSICAL_SHA256 : 0;
        return nativeScanAddFile(scan, utf8(relativePath), utf8(displayName), borrowedFd, flags,
                expectedPhysicalSha256, outcome);
    }

    @Override public int scanCommit(int completeness) {
        int result = nativeScanCommit(scan, completeness);
        scan = 0L;
        return result;
    }

    @Override public void scanAbort() {
        abortScan();
    }

    @Override public int favoriteSet(String canonicalId, boolean favorite) {
        return nativeFavoriteSet(app, utf8(canonicalId), favorite ? 1 : 0);
    }

    @Override public int markPlayed(String canonicalId) {
        return nativeMarkPlayed(app, utf8(canonicalId));
    }

    public CanonicalUserState userState(String canonicalId) {
        long[] out = new long[4];
        int result = nativeUserStateGet(app, utf8(canonicalId), out);
        if (result != RESULT_OK) {
            throw new IllegalStateException("fly_catalog_user_state_get failed: " + result);
        }
        return new CanonicalUserState(out[0] != 0, out[2], out[3], Math.toIntExact(out[1]));
    }

    @Override public FlySettingsSnapshot get() {
        int[] ints = new int[14];
        float[] floats = new float[5];
        byte[] locale = new byte[129];
        byte[] lastPlayed = new byte[4097];
        int[] lengths = new int[2];
        int result = nativeSettingsGet(app, ints, floats, locale, lastPlayed, lengths);
        if (result != RESULT_OK) {
            throw new IllegalStateException("fly_settings_get failed: " + result);
        }
        return new FlySettingsSnapshot(
                ints[0], ints[1], ints[2], ints[3], ints[4], ints[5], ints[6], ints[7], ints[8],
                floats[0], floats[1], floats[2], floats[3], floats[4],
                ints[9], ints[10], ints[11], ints[12], ints[13],
                cString(locale, lengths[0]), cString(lastPlayed, lengths[1]));
    }

    @Override public boolean apply(FlySettingsSnapshot snapshot) {
        Objects.requireNonNull(snapshot, "snapshot");
        int[] ints = {
                snapshot.aspectMode(), snapshot.videoQualityPreset(), snapshot.customRefreshPolicy(),
                snapshot.customTemporalMode(), snapshot.customSpatialMode(),
                snapshot.customPostEffect(), snapshot.adaptiveProtection(), snapshot.layoutPreset(),
                snapshot.directionMode(), snapshot.hapticLevel(), snapshot.distinctAbHaptics(),
                snapshot.audioEnabled(), snapshot.audioFocusPolicy(), snapshot.autosaveEnabled()
        };
        float[] floats = {
                snapshot.buttonScale(), snapshot.verticalOffset(), snapshot.controlOpacity(),
                snapshot.joystickScale(), snapshot.deadZone()
        };
        return nativeSettingsApply(app, ints, floats, utf8(snapshot.localeTag()),
                utf8(snapshot.lastPlayedId())) == RESULT_OK;
    }

    @Override public String controlLayoutGet() {
        int[] required = new int[1];
        int sized = nativeControlLayoutGet(app, null, required);
        if (sized != RESULT_OK && sized != RESULT_BUFFER_TOO_SMALL) {
            throw new IllegalStateException("fly_control_layout_get failed: " + sized);
        }
        byte[] buffer = new byte[Math.max(1, required[0])];
        int result = nativeControlLayoutGet(app, buffer, required);
        if (result != RESULT_OK) {
            throw new IllegalStateException("fly_control_layout_get failed: " + result);
        }
        return cString(buffer, required[0]);
    }

    @Override public boolean controlLayoutApply(String utf8) {
        return nativeControlLayoutApply(app, utf8(utf8)) == RESULT_OK;
    }

    public List<NativeCatalogEntry> catalogEntries() {
        long[] snapshotOut = new long[1];
        int captured = nativeCatalogCapture(app, snapshotOut);
        if (captured != RESULT_OK || snapshotOut[0] == 0L) {
            throw new IllegalStateException("fly_catalog_snapshot failed: " + captured);
        }
        long snapshot = snapshotOut[0];
        try {
            long[] countOut = new long[1];
            int counted = nativeCatalogCount(snapshot, countOut);
            if (counted != RESULT_OK) {
                throw new IllegalStateException("fly_catalog_snapshot_count failed: " + counted);
            }
            ArrayList<NativeCatalogEntry> entries = new ArrayList<>();
            for (long index = 0; index < countOut[0]; index++) {
                entries.add(readEntry(snapshot, index));
            }
            return entries;
        } finally {
            nativeCatalogRelease(snapshot);
        }
    }

    public long catalogGeneration() {
        long[] snapshotOut = new long[1];
        int captured = nativeCatalogCapture(app, snapshotOut);
        if (captured != RESULT_OK || snapshotOut[0] == 0L) {
            throw new IllegalStateException("fly_catalog_snapshot failed: " + captured);
        }
        try {
            long[] generation = new long[1];
            int result = nativeCatalogGeneration(snapshotOut[0], generation);
            if (result != RESULT_OK) {
                throw new IllegalStateException("fly_catalog_snapshot_generation failed: " + result);
            }
            return generation[0];
        } finally {
            nativeCatalogRelease(snapshotOut[0]);
        }
    }

    public List<NativeSourceStatus> sourceStatuses() {
        long[] countOut = new long[1];
        int counted = nativeSourceCount(app, countOut);
        if (counted != RESULT_OK) {
            throw new IllegalStateException("fly_source_status_count failed: " + counted);
        }
        ArrayList<NativeSourceStatus> statuses = new ArrayList<>();
        for (long index = 0; index < countOut[0]; index++) {
            byte[] uuid = new byte[16];
            int[] fields = new int[3];
            int result = nativeSourceGet(app, index, uuid, fields);
            if (result != RESULT_OK) {
                throw new IllegalStateException("fly_source_status_get failed: " + result);
            }
            statuses.add(new NativeSourceStatus(uuid, fields[0], fields[1], fields[2]));
        }
        return statuses;
    }

    @Override public void close() {
        abortScan();
        if (app != 0L) {
            nativeDestroy(app);
            app = 0L;
        }
    }

    private NativeCatalogEntry readEntry(long snapshot, long index) {
        Object[] uuidAndHashes = new Object[6];
        long[] sizes = new long[6];
        int[] enums = new int[10];
        Object[] texts = new Object[4];
        int result = nativeCatalogGet(snapshot, index, uuidAndHashes, sizes, enums, texts);
        if (result != RESULT_OK) {
            throw new IllegalStateException("fly_catalog_snapshot_get failed: " + result);
        }
        return new NativeCatalogEntry(
                (byte[]) uuidAndHashes[0], sizes[0], sizes[1], sizes[2], sizes[3], sizes[4],
                enums[0], enums[1], enums[2],
                (byte[]) uuidAndHashes[1], (byte[]) uuidAndHashes[2], (byte[]) uuidAndHashes[3],
                (byte[]) uuidAndHashes[4],
                enums[3], enums[4], enums[5], enums[6], enums[7], enums[8], enums[9],
                (String) texts[0], (String) texts[1], (String) texts[2], (String) texts[3],
                (byte[]) uuidAndHashes[5], Math.toIntExact(sizes[5]),
                decodeTitle(nativeCatalogTitleGet(snapshot, index)));
    }

    /** Resolves cached ROM fingerprints without reading a package or depending on UI language. */
    public static NativeGameTitle resolveGameTitle(byte[] sha256, String fallbackName) {
        if (sha256 != null && sha256.length != 32) throw new IllegalArgumentException("SHA-256");
        return decodeTitle(nativeGameTitleResolve(sha256, utf8(fallbackName)));
    }

    private static NativeGameTitle decodeTitle(String[] fields) {
        if (fields == null || fields.length != 5) {
            throw new IllegalStateException("game title query failed");
        }
        return new NativeGameTitle(fields[0], fields[1], fields[2],
                fields[3].isEmpty() ? List.of() : List.of(fields[3].split("\\n")),
                Integer.parseInt(fields[4]));
    }

    private static native String[] nativeCatalogTitleGet(long snapshot, long index);
    private static native String[] nativeGameTitleResolve(byte[] sha256, byte[] fallbackName);

    private void abortScan() {
        if (scan != 0L) {
            nativeScanAbort(scan);
            scan = 0L;
        }
    }

    private static byte[] utf8(String value) {
        return (value == null ? "" : value).getBytes(StandardCharsets.UTF_8);
    }

    private static String cString(byte[] bytes, int requiredIncludingNul) {
        int length = Math.max(0, requiredIncludingNul - 1);
        length = Math.min(length, bytes.length);
        while (length > 0 && bytes[length - 1] == 0) length--;
        return new String(bytes, 0, length, StandardCharsets.UTF_8);
    }

    private static native int nativeCreate(byte[] dataRoot, byte[] cacheRoot, long[] outHandle);
    private static native void nativeDestroy(long app);
    private static native int nativeScanBegin(long app, byte[] uuid, int scope, long[] outScan);
    private static native int nativeScanAddFile(
            long scan, byte[] relativePath, byte[] displayName, int borrowedFd, int flags,
            byte[] expectedSha, int[] outcomeOut);
    private static native int nativeScanCommit(long scan, int completeness);
    private static native void nativeScanAbort(long scan);
    private static native int nativeFavoriteSet(long app, byte[] canonicalId, int favorite);
    private static native int nativeMarkPlayed(long app, byte[] canonicalId);
    private static native int nativeUserStateGet(long app, byte[] canonicalId, long[] out);
    private static native int nativeSettingsGet(
            long app, int[] ints, float[] floats, byte[] locale, byte[] lastPlayed, int[] lengths);
    private static native int nativeSettingsApply(
            long app, int[] ints, float[] floats, byte[] locale, byte[] lastPlayed);
    private static native int nativeCatalogCapture(long app, long[] outSnapshot);
    private static native int nativeCatalogCount(long snapshot, long[] out);
    private static native int nativeCatalogGeneration(long snapshot, long[] out);
    private static native int nativeCatalogGet(
            long snapshot, long index, Object[] uuidAndHashes, long[] sizes, int[] enums,
            Object[] texts);
    private static native void nativeCatalogRelease(long snapshot);
    private static native int nativeSourceCount(long app, long[] out);
    private static native int nativeSourceGet(long app, long index, byte[] uuid, int[] fields);
    private static native int nativeControlLayoutGet(long app, byte[] out, int[] required);
    private static native int nativeControlLayoutApply(long app, byte[] utf8);
}
