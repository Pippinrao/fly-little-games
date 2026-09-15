package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import android.content.ContentProvider;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.ContextWrapper;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.rule.provider.ProviderTestRule;

import com.flynes.emu.app.FlyCatalogCommands;
import com.flynes.emu.app.NativeCatalogEntry;
import com.flynes.emu.app.NativeSourceStatus;
import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.StableIds;
import com.flynes.emu.catalog.persistence.CatalogPackage;
import com.flynes.emu.catalog.persistence.CatalogRepository;
import com.flynes.emu.catalog.persistence.CatalogState;
import com.flynes.emu.catalog.persistence.CatalogStateStore;
import com.flynes.emu.catalog.persistence.SourceCatalogState;
import com.flynes.emu.launch.ExactRomLoader;
import com.flynes.emu.launch.LaunchCoordinator;
import com.flynes.emu.launch.LaunchResult;

import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.CRC32;

/**
 * On-device regression cover for the 2026-09-13 cold-start crash.
 *
 * <p>Package locators live in a process-local map while the native catalog is persisted, so after
 * a restart every package needed its document locator rebuilt from the SAF tree. The old rebuild
 * invented {@code <treeLocator>/<file>}, which is a tree URI with an appended path; the storage
 * provider answered {@code IllegalArgumentException: Invalid URI} and the launch thread died.
 *
 * <p>These tests run against a real {@code ContentResolver}, real {@code DocumentsContract} and a
 * provider that rejects exactly what {@code DocumentsContract.getDocumentId} rejects.
 */
@RunWith(AndroidJUnit4.class)
public final class AndroidCatalogLaunchRegressionTest {
    private static final String AUTHORITY = "launchregression";
    private static final String TREE_LOCATOR = "content://" + AUTHORITY + "/tree/root";
    /** Canonical tree-relative path of a nested library, as the native catalog requires. */
    private static final String RELATIVE_PATH = "NES/game.nes";
    /** Shape that crashed the device: a tree locator with an appended relative path. */
    private static final String CRASH_LOCATOR = TREE_LOCATOR + "/" + RELATIVE_PATH;
    private static final String DOCUMENT_ID = "root/" + RELATIVE_PATH;
    private static final String DERIVED_LOCATOR =
            TREE_LOCATOR + "/document/root%2FNES%2Fgame.nes";
    private static final String SOURCE_ID = StableIds.safSourceId(TREE_LOCATOR);
    private static final String VARIANT_ID = "variant-e2e";
    private static final byte[] ROM = romBytes();

    @Rule public final ProviderTestRule provider = new ProviderTestRule.Builder(
            RomTreeProvider.class, AUTHORITY).build();

    @Test
    public void nativeZipScanReopensANonFirstEntryAfterRestart() throws Exception {
        File root = new File(ApplicationProvider.<Context>getApplicationContext().getCacheDir(),
                "native-zip-launch-" + java.util.UUID.randomUUID());
        File data = new File(root, "data");
        File cache = new File(root, "cache");
        assertTrue(data.mkdirs());
        assertTrue(cache.mkdirs());
        File zip = new File(root, "游戏.zip");
        try (var output = new java.util.zip.ZipOutputStream(new FileOutputStream(zip))) {
            output.putNextEntry(new java.util.zip.ZipEntry("readme.txt"));
            output.write(new byte[]{1, 2, 3});
            output.closeEntry();
            output.putNextEntry(new java.util.zip.ZipEntry("NES/游戏.nes"));
            output.write(ROM);
            output.closeEntry();
        }
        try (var app = com.flynes.emu.app.FlyNesApp.create(data.getPath(), cache.getPath());
             var pfd = ParcelFileDescriptor.open(zip, ParcelFileDescriptor.MODE_READ_ONLY)) {
            assertEquals(0, app.scanBegin(uuid(), FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY));
            assertEquals(0, app.scanAddFile("游戏.zip", "游戏.zip", pfd.getFd(), null));
            assertEquals(0, app.scanCommit(FlyCatalogCommands.SCAN_FULL));
        }
        try (var restarted = com.flynes.emu.app.FlyNesApp.create(data.getPath(), cache.getPath())) {
            Map<String, String> backing = new LinkedHashMap<>();
            AndroidUuidSafMap map = new AndroidUuidSafMap(backing::get, backing::put, backing::remove);
            map.put(uuid(), TREE_LOCATOR);
            CatalogState state = NativeCatalogProjector.project(restarted.catalogEntries(),
                    restarted.sourceStatuses(), Map.of(), 0, map, new AndroidPackageLocatorMap(),
                    AndroidDocumentLocators::documentUriFor,
                    // This projection only carries the user-directory row created above.
                    com.flynes.emu.catalog.BuiltinGames.empty());
            GameCatalog catalog = new GameCatalog();
            new CatalogRepository(state, new MemoryStateStore(), catalog);
            var variant = catalog.canonicalEntries().get(0).variants().get(0);
            ExactRomLoader loader = new ExactRomLoader((sourceId, uri) -> new java.io.FileInputStream(zip));
            assertArrayEquals(ROM, loader.load(com.flynes.emu.launch.LaunchRequest.forVariant(variant)));
        }
    }

    @Test
    public void theStorageProviderRejectsATreeLocatorWithAnAppendedPath() {
        try (InputStream ignored = provider.getResolver().openInputStream(
                Uri.parse(CRASH_LOCATOR))) {
            fail("provider must reject a tree URI with an appended path");
        } catch (IllegalArgumentException expected) {
            assertTrue(String.valueOf(expected.getMessage()),
                    expected.getMessage().startsWith("Invalid URI"));
        } catch (IOException unexpected) {
            fail("expected the provider fault, not " + unexpected);
        }
    }

    @Test
    public void derivesAnOpenableDocumentLocatorFromTheTreeLocator() throws Exception {
        String derived = AndroidDocumentLocators.documentUriFor(TREE_LOCATOR, RELATIVE_PATH);

        assertEquals(DERIVED_LOCATOR, derived);
        try (InputStream input = provider.getResolver().openInputStream(Uri.parse(derived))) {
            assertArrayEquals(ROM, readAll(input));
        }
        assertEquals(DOCUMENT_ID, DocumentsContract.getDocumentId(Uri.parse(derived)));
    }

    @Test
    public void derivesTheCanonicalTreeRelativePathFromAChildDocumentId() {
        assertEquals(RELATIVE_PATH,
                AndroidDocumentLocators.relativePathFor(TREE_LOCATOR, DOCUMENT_ID));
        assertEquals("game.nes",
                AndroidDocumentLocators.relativePathFor(TREE_LOCATOR, "root/game.nes"));
        assertNull(AndroidDocumentLocators.relativePathFor(TREE_LOCATOR, "other/game.nes"));
        assertNull("a display name is not a document id",
                AndroidDocumentLocators.relativePathFor(TREE_LOCATOR, "game.nes"));
        assertNull("escaping the tree root must never be addressable",
                AndroidDocumentLocators.relativePathFor(TREE_LOCATOR, "root/../game.nes"));
        assertNull(AndroidDocumentLocators.relativePathFor("not-a-tree", DOCUMENT_ID));
        assertNull(AndroidDocumentLocators.documentUriFor(TREE_LOCATOR, "../game.nes"));
    }

    @Test
    public void coldStartProjectionLaunchesTheRomFromTheRebuiltLocator() {
        CatalogState projected = coldStartProjection();
        CatalogPackage projectedPackage = safPackage(projected);
        assertEquals(DERIVED_LOCATOR, projectedPackage.physicalPackage().sourceUri());
        assertEquals(CatalogPackage.Freshness.FRESH, projectedPackage.freshness());

        Run run = launch(projected);

        assertEquals(run.result().message(), LaunchResult.Code.SUCCESS, run.result().code());
        assertArrayEquals(ROM, run.staged());
    }

    @Test
    public void aCatalogCarryingTheCrashLocatorFailsInsteadOfKillingTheLaunchThread() {
        CatalogState poisoned = withLocator(coldStartProjection(), CRASH_LOCATOR);
        assertEquals(CRASH_LOCATOR, safPackage(poisoned).physicalPackage().sourceUri());

        Run run = launch(poisoned);

        assertEquals(LaunchResult.Code.SOURCE_OPEN_FAILED, run.result().code());
        AndroidCatalogStreamOpener.SourceOpenException failure = assertThrows(
                AndroidCatalogStreamOpener.SourceOpenException.class,
                () -> run.opener().open(SOURCE_ID, CRASH_LOCATOR));
        assertEquals(AndroidCatalogStreamOpener.FailureCode.LOCATOR_INVALID, failure.code());
    }

    private record Run(LaunchResult result, byte[] staged, AndroidCatalogStreamOpener opener) {
    }

    private Run launch(CatalogState state) {
        Context context = new ProviderContext(
                ApplicationProvider.getApplicationContext(), provider.getResolver());
        GameCatalog catalog = new GameCatalog();
        CatalogRepository repository = new CatalogRepository(
                state, new MemoryStateStore(), catalog);
        AndroidCatalogStreamOpener opener = new AndroidCatalogStreamOpener(
                context, repository, TREE_LOCATOR::equals);
        byte[][] staged = new byte[1][];
        LaunchCoordinator coordinator = new LaunchCoordinator(
                catalog,
                new ExactRomLoader(opener),
                (request, romBytes) -> staged[0] = romBytes,
                request -> { });
        return new Run(coordinator.launch(VARIANT_ID), staged[0], opener);
    }

    /** Rebuilds what a cold start projects: the process-local locator map is empty. */
    private static CatalogState coldStartProjection() {
        Map<String, String> backing = new LinkedHashMap<>();
        AndroidUuidSafMap uuidMap = new AndroidUuidSafMap(backing::get, backing::put, backing::remove);
        byte[] uuid = uuid();
        uuidMap.put(uuid, TREE_LOCATOR);
        return NativeCatalogProjector.project(
                List.of(nativeEntry(uuid)),
                List.of(new NativeSourceStatus(uuid, FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY,
                        FlyCatalogCommands.SCAN_FULL, 1)),
                Map.of(), 0, uuidMap, new AndroidPackageLocatorMap(),
                AndroidDocumentLocators::documentUriFor,
                // This projection only carries user-directory rows, so no bundled
                // game needs a trusted title here.
                com.flynes.emu.catalog.BuiltinGames.empty());
    }

    private static NativeCatalogEntry nativeEntry(byte[] uuid) {
        return new NativeCatalogEntry(
                uuid, ROM.length, ROM.length, ROM.length, 16384, 0, 0, 0, 0,
                digest("SHA-1", ROM), digest("SHA-256", ROM), digest("SHA-256", ROM), crc32(ROM),
                FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY,
                1, 0, 1, 1, 1, 0,
                "canonical-e2e", VARIANT_ID, "game.nes", RELATIVE_PATH, new byte[0], -1);
    }

    /** Rewrites the platform locator of every SAF package, as the old build persisted it. */
    private static CatalogState withLocator(CatalogState state, String locator) {
        LinkedHashMap<String, SourceCatalogState> sources = new LinkedHashMap<>();
        for (SourceCatalogState source : state.sources().values()) {
            LinkedHashMap<String, CatalogPackage> packages = new LinkedHashMap<>();
            for (Map.Entry<String, CatalogPackage> item : source.packages().entrySet()) {
                PhysicalPackage physical = item.getValue().physicalPackage();
                if (physical.source().type() == RomSource.Type.SAF_TREE) {
                    physical = new PhysicalPackage(physical.id(), physical.source(), locator,
                            physical.originalFilename(), physical.packageFormat(),
                            physical.physicalPackageSha256(), physical.variants());
                }
                packages.put(item.getKey(),
                        new CatalogPackage(physical, CatalogPackage.Freshness.FRESH));
            }
            sources.put(source.source().id(), new SourceCatalogState(source.source(), packages,
                    source.packageOutcomes(), source.entryOutcomes(), source.issues(),
                    source.lastScanCompleteness(), source.lastScanToken()));
        }
        return new CatalogState(CatalogState.CURRENT_SCHEMA, state.revision() + 1,
                state.builtinSourceId(), sources, state.userStates(),
                state.lastPlayedSequence());
    }

    private static CatalogPackage safPackage(CatalogState state) {
        return state.sources().values().stream()
                .filter(item -> item.source().type() == RomSource.Type.SAF_TREE)
                .findFirst().orElseThrow().packages().values().iterator().next();
    }

    private static byte[] uuid() {
        byte[] uuid = new byte[16];
        for (int index = 0; index < uuid.length; index++) uuid[index] = (byte) (index + 1);
        return uuid;
    }

    /** Minimal iNES image: 16 byte header plus one 16 KiB PRG bank. */
    private static byte[] romBytes() {
        byte[] rom = new byte[16400];
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1A;
        rom[4] = 1;
        for (int index = 16; index < rom.length; index++) {
            rom[index] = (byte) (index * 31);
        }
        return rom;
    }

    private static byte[] digest(String algorithm, byte[] bytes) {
        try {
            return MessageDigest.getInstance(algorithm).digest(bytes);
        } catch (Exception unavailable) {
            throw new IllegalStateException(unavailable);
        }
    }

    private static byte[] crc32(byte[] bytes) {
        CRC32 crc = new CRC32();
        crc.update(bytes);
        long value = crc.getValue();
        return new byte[]{
                (byte) (value >>> 24), (byte) (value >>> 16),
                (byte) (value >>> 8), (byte) value};
    }

    private static byte[] readAll(InputStream input) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[4096];
        while (true) {
            int count = input.read(buffer);
            if (count < 0) break;
            if (count > 0) output.write(buffer, 0, count);
        }
        return output.toByteArray();
    }

    /**
     * {@link ProviderTestRule} publishes its provider through its own resolver only, so the
     * catalog runtime has to be handed a context that resolves against it.
     */
    private static final class ProviderContext extends ContextWrapper {
        private final ContentResolver resolver;

        ProviderContext(Context base, ContentResolver resolver) {
            super(base);
            this.resolver = resolver;
        }

        @Override public Context getApplicationContext() {
            return this;
        }

        @Override public ContentResolver getContentResolver() {
            return resolver;
        }
    }

    private static final class MemoryStateStore implements CatalogStateStore {        private byte[] bytes;

        @Override public byte[] read() {
            return bytes == null ? null : bytes.clone();
        }

        @Override public void writeAtomically(byte[] encoded) {
            bytes = encoded.clone();
        }
    }

    /**
     * Emulates the storage provider contract that killed the process: only a URI carrying a real
     * document ID can be opened, and {@link DocumentsContract#getDocumentId} is the judge.
     */
    public static final class RomTreeProvider extends ContentProvider {
        private File rom;

        @Override public boolean onCreate() {
            // ProviderTestRule wraps the provider context and does not forward getCacheDir().
            Context context = ApplicationProvider.getApplicationContext();
            File file = new File(context.getCacheDir(), "launch-regression-game.nes");
            try (FileOutputStream output = new FileOutputStream(file)) {
                output.write(ROM);
            } catch (IOException failure) {
                throw new IllegalStateException(failure);
            }
            rom = file;
            return true;
        }

        @Override public ParcelFileDescriptor openFile(Uri uri, String mode)
                throws FileNotFoundException {
            String documentId;
            try {
                documentId = DocumentsContract.getDocumentId(uri);
            } catch (IllegalArgumentException malformed) {
                throw new IllegalArgumentException("Invalid URI: " + uri);
            }
            if (!DOCUMENT_ID.equals(documentId)) {
                throw new IllegalArgumentException("Invalid URI: " + uri);
            }
            return ParcelFileDescriptor.open(rom, ParcelFileDescriptor.MODE_READ_ONLY);
        }

        @Override public String getType(Uri uri) {
            return "application/octet-stream";
        }

        @Override public Cursor query(Uri uri, String[] projection, String selection,
                String[] selectionArgs, String sortOrder) {
            return null;
        }

        @Override public Uri insert(Uri uri, ContentValues values) {
            return null;
        }

        @Override public int delete(Uri uri, String selection, String[] selectionArgs) {
            return 0;
        }

        @Override public int update(Uri uri, ContentValues values, String selection,
                String[] selectionArgs) {
            return 0;
        }
    }
}
