package com.flynes.emu.catalog.android;

import android.content.Context;

import com.flynes.emu.catalog.CanonicalGame;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.RomVariant;
import com.flynes.emu.catalog.ScanResult;
import com.flynes.emu.catalog.TitleCandidate;
import com.flynes.emu.catalog.scan.PackageCandidate;
import com.flynes.emu.catalog.scan.RomPackageScanner;
import com.flynes.emu.catalog.scan.ScanLimits;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Licensed built-in ROM adapter routed through the same bounded scanner as user packages. */
public final class AndroidBuiltinCatalogAdapter {
    public static final String ASSET_PATH = "roms/from_below.nes";
    public static final String ASSET_LOCATOR = "asset:///" + ASSET_PATH;
    public static final RomSource SOURCE = new RomSource(
            "builtin", RomSource.Type.BUILTIN, ASSET_LOCATOR,
            RomSource.PermissionState.NOT_REQUIRED);

    private static final CanonicalGame FROM_BELOW = new CanonicalGame(
            "builtin:from-below",
            Collections.singletonList(new TitleCandidate(
                    "From Below", TitleCandidate.Language.EN,
                    TitleCandidate.Origin.BUILTIN_MANIFEST,
                    TitleCandidate.Confidence.VERIFIED,
                    TitleCandidate.ReviewState.VERIFIED)),
            Collections.emptyList());

    private final Context context;

    public AndroidBuiltinCatalogAdapter(Context context) {
        if (context == null) throw new NullPointerException("context");
        this.context = context.getApplicationContext();
    }

    public ScanResult scan() {
        RomPackageScanner scanner = new RomPackageScanner(
                ScanLimits.defaults(), (hash, format) -> FROM_BELOW.id());
        ScanResult scanned = scanner.scan(SOURCE, Collections.singletonList(new PackageCandidate(
                "builtin-from-below-v1", "from_below.nes", ASSET_LOCATOR,
                this::openAsset)));
        ArrayList<PhysicalPackage> packages = new ArrayList<>();
        for (PhysicalPackage item : scanned.packages()) {
            ArrayList<RomVariant> variants = new ArrayList<>();
            for (RomVariant variant : item.variants()) {
                variants.add(new RomVariant(
                        variant.id(), FROM_BELOW, variant.entryPath(), variant.romFormat(),
                        variant.compatibilityDecision(), variant.hashes(), variant.analysis(),
                        variant.zipEntryIdentity(), variant.zipNameEncoding()));
            }
            packages.add(new PhysicalPackage(
                    item.id(), item.source(), item.sourceUri(), item.originalFilename(),
                    item.packageFormat(), item.physicalPackageSha256(), variants));
        }
        return new ScanResult(
                packages, scanned.packageOutcomes(), scanned.entryOutcomes(), scanned.issues());
    }

    private InputStream openAsset() throws IOException {
        return context.getAssets().open(ASSET_PATH);
    }
}
