package com.flynes.emu.gamecenter;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.persistence.SourceScanResult;

import org.junit.Test;

import java.nio.ByteBuffer;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public final class GameCenterSnapshotCodecTest {
    @Test public void projectionDecodeValidatesEnvelopeButDefersEmbeddedCatalogBytes()
            throws Exception {
        GameCenterSnapshot base = synthetic(2_224, 9L, "99".repeat(32), 10L);
        GameCenterSnapshot value = new GameCenterSnapshot(
                base.schemaVersion(), base.nativeGeneration(), base.builtinManifestSha256(),
                base.sourceEpoch(), base.rows(), base.sources(), new byte[1_000_000]);
        byte[] encoded = GameCenterSnapshotCodec.encode(value);

        GameCenterSnapshot projected = GameCenterSnapshotCodec.decodeProjection(encoded);

        assertEquals(value.schemaVersion(), projected.schemaVersion());
        assertEquals(value.nativeGeneration(), projected.nativeGeneration());
        assertEquals(value.rows(), projected.rows());
        assertEquals(value.sources(), projected.sources());
        assertArrayEquals(new byte[0], projected.catalogStateBytes());
    }

    @Test public void startupEnvelopeCarriesTotalCountButOnlyDecodesTheVisibleWindow()
            throws Exception {
        GameCenterSnapshot value = synthetic(2_224, 11L, "aa".repeat(32), 12L);

        byte[] encoded = GameCenterSnapshotCodec.encodeStartup(value, 20);
        GameCenterSnapshot startup = GameCenterSnapshotCodec.decodeStartup(encoded);

        assertEquals(2_224, startup.rows().size());
        assertEquals(value.rows().get(0), startup.rows().get(0));
        assertEquals(value.rows().get(19), startup.rows().get(19));
        assertEquals("deferred-20", startup.rows().get(20).canonicalId());
        assertEquals(0, startup.catalogStateBytes().length);
    }

    @Test public void roundTrips2224RowsDeterministically() throws Exception {
        GameCenterSnapshot value = synthetic(2224, 41L, "00".repeat(32), 7L);

        byte[] first = GameCenterSnapshotCodec.encode(value);
        byte[] second = GameCenterSnapshotCodec.encode(value);

        assertArrayEquals(first, second);
        assertEquals(value, GameCenterSnapshotCodec.decode(first));
    }

    @Test public void checksumFailureNeverProducesRows() throws Exception {
        byte[] bytes = GameCenterSnapshotCodec.encode(
                synthetic(3, 1L, "11".repeat(32), 1L));
        bytes[bytes.length / 2] ^= 1;

        GameCenterSnapshotCodec.CodecException failure = assertThrows(
                GameCenterSnapshotCodec.CodecException.class,
                () -> GameCenterSnapshotCodec.decode(bytes));

        assertEquals(GameCenterSnapshotCodec.ErrorCode.CHECKSUM_MISMATCH, failure.code());
    }

    @Test public void truncationIsRejectedBeforeParsingRows() throws Exception {
        byte[] encoded = GameCenterSnapshotCodec.encode(
                synthetic(2, 2L, "22".repeat(32), 2L));

        GameCenterSnapshotCodec.CodecException failure = assertThrows(
                GameCenterSnapshotCodec.CodecException.class,
                () -> GameCenterSnapshotCodec.decode(Arrays.copyOf(encoded, 20)));

        assertEquals(GameCenterSnapshotCodec.ErrorCode.TRUNCATED, failure.code());
    }

    @Test public void checksumValidUnknownSchemaIsRejected() throws Exception {
        byte[] encoded = GameCenterSnapshotCodec.encode(
                synthetic(1, 3L, "33".repeat(32), 3L));
        ByteBuffer.wrap(encoded).putInt(4, GameCenterSnapshot.CURRENT_SCHEMA + 1);
        replaceChecksum(encoded);

        GameCenterSnapshotCodec.CodecException failure = assertThrows(
                GameCenterSnapshotCodec.CodecException.class,
                () -> GameCenterSnapshotCodec.decode(encoded));

        assertEquals(GameCenterSnapshotCodec.ErrorCode.UNKNOWN_VERSION, failure.code());
    }

    @Test public void oversizedStringIsRejectedByEncoder() {
        GameCenterSnapshot value = snapshotWithRows(List.of(row("id", "x".repeat(64 * 1024 + 1))));

        GameCenterSnapshotCodec.CodecException failure = assertThrows(
                GameCenterSnapshotCodec.CodecException.class,
                () -> GameCenterSnapshotCodec.encode(value));

        assertEquals(GameCenterSnapshotCodec.ErrorCode.BOUNDS, failure.code());
    }

    @Test public void modelRejectsDuplicateCanonicalAndSourceIds() {
        GameCenterSnapshot.Row duplicate = row("same", "same");
        assertThrows(IllegalArgumentException.class,
                () -> snapshotWithRows(List.of(duplicate, duplicate)));

        GameCenterSnapshot.SourceRow source = source("same-source");
        assertThrows(IllegalArgumentException.class,
                () -> new GameCenterSnapshot(GameCenterSnapshot.CURRENT_SCHEMA, 1,
                        "44".repeat(32), 1, List.of(row("one", "one")),
                        List.of(source, source), new byte[]{1}));
    }

    @Test public void modelOwnsMutableInputsAndPayloadAccessorReturnsCopy() {
        ArrayList<GameCenterSnapshot.Row> rows = new ArrayList<>();
        rows.add(row("owned", "Owned"));
        byte[] state = new byte[]{7, 8, 9};
        GameCenterSnapshot snapshot = new GameCenterSnapshot(
                GameCenterSnapshot.CURRENT_SCHEMA, 5, "55".repeat(32), 6,
                rows, List.of(source("builtin")), state);

        rows.clear();
        state[0] = 0;
        byte[] returned = snapshot.catalogStateBytes();
        returned[1] = 0;

        assertEquals(1, snapshot.rows().size());
        assertArrayEquals(new byte[]{7, 8, 9}, snapshot.catalogStateBytes());
    }

    @Test public void catalogStatePayloadMayExceedRowCountBound() throws Exception {
        byte[] catalog = new byte[128 * 1024];
        Arrays.fill(catalog, (byte) 0x5a);
        GameCenterSnapshot snapshot = new GameCenterSnapshot(
                GameCenterSnapshot.CURRENT_SCHEMA, 7, "77".repeat(32), 8,
                List.of(row("large-state", "Large State")),
                List.of(source("builtin")), catalog);

        GameCenterSnapshot decoded = GameCenterSnapshotCodec.decode(
                GameCenterSnapshotCodec.encode(snapshot));

        assertArrayEquals(catalog, decoded.catalogStateBytes());
    }

    private static GameCenterSnapshot synthetic(
            int count, long generation, String fingerprint, long sourceEpoch) {
        ArrayList<GameCenterSnapshot.Row> rows = new ArrayList<>(count);
        for (int index = 0; index < count; index++) {
            String id = String.format("content-%04d", index);
            rows.add(new GameCenterSnapshot.Row(id, "Game " + index, "游戏 " + index,
                    "", "game-" + index + ".nes", "game " + index + " 游戏 " + index,
                    index < 7, (index & 7) == 0, index, index / 2, 1, true,
                    100 - index % 101));
        }
        return new GameCenterSnapshot(GameCenterSnapshot.CURRENT_SCHEMA, generation,
                fingerprint, sourceEpoch, rows, List.of(source("builtin")),
                new byte[]{1, 3, 3, 7});
    }

    private static GameCenterSnapshot snapshotWithRows(List<GameCenterSnapshot.Row> rows) {
        return new GameCenterSnapshot(GameCenterSnapshot.CURRENT_SCHEMA, 1,
                "66".repeat(32), 1, rows, List.of(source("builtin")), new byte[]{1});
    }

    private static GameCenterSnapshot.Row row(String id, String title) {
        return new GameCenterSnapshot.Row(id, title, "", "", id + ".nes", title,
                false, false, 0, 0, 1, true, 0);
    }

    private static GameCenterSnapshot.SourceRow source(String id) {
        return new GameCenterSnapshot.SourceRow(id, RomSource.Type.BUILTIN,
                RomSource.PermissionState.NOT_REQUIRED, RomSource.Availability.AVAILABLE,
                SourceScanResult.Completeness.FULL, 1, 7);
    }

    private static void replaceChecksum(byte[] encoded) throws Exception {
        int checksumLength = 32;
        byte[] digest = MessageDigest.getInstance("SHA-256")
                .digest(Arrays.copyOf(encoded, encoded.length - checksumLength));
        System.arraycopy(digest, 0, encoded, encoded.length - checksumLength, checksumLength);
    }
}
