package com.flynes.emu.catalog;

import static org.junit.Assert.assertThrows;

import com.flynes.emu.launch.LaunchRequest;

import org.junit.Test;

import java.util.List;

public final class RomCompatibilityContractTest {
    private static final RomHashes HASHES = new RomHashes(
            "0000000000000000000000000000000000000000",
            "0000000000000000000000000000000000000000000000000000000000000000",
            "0000000000000000000000000000000000000000000000000000000000000000",
            "00000000");
    private static final CanonicalGame GAME = new CanonicalGame(
            "game:test", "Test", "", List.of());

    @Test
    public void formatAndCompatibilityPairsAreValidatedAtEveryConstructionBoundary() {
        CompatibilityDecision fdsUnsupported = new CompatibilityDecision(
                CompatibilityState.UNSUPPORTED,
                CompatibilityReason.FDS_BIOS_API_NOT_IMPLEMENTED);
        CompatibilityDecision fdsInvalid = new CompatibilityDecision(
                CompatibilityState.INVALID,
                CompatibilityReason.FDS_TRUNCATED);
        CompatibilityDecision unifUnsupported = new CompatibilityDecision(
                CompatibilityState.UNSUPPORTED,
                CompatibilityReason.UNIF_PRODUCT_DISABLED);

        variant(RomFormat.INES, CompatibilityDecision.playableNes());
        variant(RomFormat.NES2, CompatibilityDecision.playableNes());
        variant(RomFormat.FDS, fdsUnsupported);
        variant(RomFormat.FDS, fdsInvalid);
        variant(RomFormat.UNIF, unifUnsupported);

        assertThrows(IllegalArgumentException.class,
                () -> variant(RomFormat.FDS, CompatibilityDecision.playableNes()));
        assertThrows(IllegalArgumentException.class,
                () -> variant(RomFormat.INES, fdsUnsupported));
        assertThrows(IllegalArgumentException.class,
                () -> variant(RomFormat.UNIF, fdsInvalid));
        assertThrows(IllegalArgumentException.class, () -> variant(
                RomFormat.UNKNOWN,
                new CompatibilityDecision(
                        CompatibilityState.INVALID, CompatibilityReason.UNKNOWN_FORMAT)));

        assertThrows(IllegalArgumentException.class, () -> new GameVariant(
                "game:test", "variant", "package", "source", "memory://package",
                "disk.fds", null, PackageFormat.RAW, RomFormat.FDS,
                CompatibilityDecision.playableNes(), HASHES, RomAnalysis.basic(0),
                null, null, RomSource.PermissionState.NOT_REQUIRED,
                RomSource.Availability.AVAILABLE));
        assertThrows(IllegalArgumentException.class, () -> new LaunchRequest(
                "game:test", "variant", "source", "memory://package", null,
                PackageFormat.RAW, RomFormat.FDS, CompatibilityState.PLAYABLE,
                HASHES, null, null));
    }

    private static RomVariant variant(
            RomFormat format, CompatibilityDecision compatibility) {
        return new RomVariant(
                "variant", GAME, null, format, compatibility, HASHES);
    }
}
