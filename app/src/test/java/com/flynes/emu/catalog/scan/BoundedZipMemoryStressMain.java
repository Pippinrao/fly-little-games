package com.flynes.emu.catalog.scan;

import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.GameVariant;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.ScanResult;
import com.flynes.emu.launch.ExactRomLoader;
import com.flynes.emu.launch.LaunchRequest;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.util.Collections;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Runs in a separately bounded JVM; invoked only by {@link BoundedZipMemoryStressTest}. */
public final class BoundedZipMemoryStressMain {
    private BoundedZipMemoryStressMain() {
    }

    public static void main(String[] arguments) throws Exception {
        byte[] payload = new byte[(int) ScanLimits.defaults().maxPayloadBytes() - 4096];
        payload[0] = 'N'; payload[1] = 'E'; payload[2] = 'S'; payload[3] = 0x1a;
        payload[4] = 1;
        byte[] archive = storedZip(payload);
        payload = null;
        System.gc();

        RomSource source = new RomSource(
                "stress-source", RomSource.Type.SAF_TREE, "source://stress",
                RomSource.PermissionState.GRANTED, RomSource.Availability.AVAILABLE);
        PackageCandidate candidate = PackageCandidate.bytes("stress-doc", "stress.zip", archive);
        ScanResult result = new RomPackageScanner(ScanLimits.defaults()).scan(
                source, Collections.singletonList(candidate));
        if (result.packages().size() != 1) {
            throw new AssertionError("scanner did not index the stress package: " + result);
        }
        candidate = null;
        System.gc();

        GameCatalog catalog = new GameCatalog();
        catalog.applyScanResult(result);
        GameVariant variant = catalog.canonicalEntries().get(0).variants().get(0);
        LaunchRequest request = LaunchRequest.forVariant(variant);
        byte[] stableArchive = archive;
        byte[] loaded = new ExactRomLoader(
                (sourceId, locator) -> new ByteArrayInputStream(stableArchive)).load(request);
        if (loaded.length != ScanLimits.defaults().maxPayloadBytes() - 4096) {
            throw new AssertionError("loader returned the wrong stress payload");
        }
        System.out.println("STRESS_OK");
    }

    private static byte[] storedZip(byte[] payload) throws Exception {
        CRC32 crc = new CRC32();
        crc.update(payload);
        ByteArrayOutputStream bytes = new ByteArrayOutputStream(payload.length + 256);
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            ZipEntry entry = new ZipEntry("game.nes");
            entry.setMethod(ZipEntry.STORED);
            entry.setSize(payload.length);
            entry.setCompressedSize(payload.length);
            entry.setCrc(crc.getValue());
            zip.putNextEntry(entry);
            zip.write(payload);
            zip.closeEntry();
        }
        return bytes.toByteArray();
    }
}
