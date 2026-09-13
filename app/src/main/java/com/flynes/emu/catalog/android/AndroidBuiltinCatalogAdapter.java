package com.flynes.emu.catalog.android;

import android.content.Context;

import com.flynes.emu.catalog.BuiltinGames;
import com.flynes.emu.catalog.CanonicalGame;
import com.flynes.emu.catalog.EntryOutcome;
import com.flynes.emu.catalog.PackageOutcome;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.RomVariant;
import com.flynes.emu.catalog.ScanIssue;
import com.flynes.emu.catalog.ScanResult;
import com.flynes.emu.catalog.TitleCandidate;
import com.flynes.emu.catalog.scan.PackageCandidate;
import com.flynes.emu.catalog.scan.RomPackageScanner;
import com.flynes.emu.catalog.scan.ScanLimits;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

/**
 * Licensed built-in ROMs, routed through the same bounded scanner as user
 * packages.
 *
 * <p>The list of games is not code: it is read from the shared manifest
 * (content/assets/builtin-games.json), so adding a bundled game never requires a
 * change here or on any other platform.
 */
public final class AndroidBuiltinCatalogAdapter {

    /** Root of the bundled ROM assets; every game appends its own filename. */
    public static final String ASSET_ROOT = "asset:///" + BuiltinGames.ASSET_DIR;

    public static final RomSource SOURCE = new RomSource(
            "builtin", RomSource.Type.BUILTIN, ASSET_ROOT, RomSource.PermissionState.NOT_REQUIRED);

    /** Locator for one bundled ROM. */
    public static String assetLocator(String assetFilename) {
        return ASSET_ROOT + assetFilename;
    }

    /** Opens one bundled asset by its path relative to the assets root. */
    public interface RomAssets {
        InputStream open(String assetPath) throws IOException;
    }

    private final BuiltinGames games;
    private final RomAssets assets;

    public AndroidBuiltinCatalogAdapter(Context context) throws IOException {
        this(BuiltinGames.fromAssets(context), assetPath -> context.getAssets().open(assetPath));
    }

    public AndroidBuiltinCatalogAdapter(BuiltinGames games, RomAssets assets) {
        if (games == null) throw new NullPointerException("games");
        if (assets == null) throw new NullPointerException("assets");
        this.games = games;
        this.assets = assets;
    }

    /** The games this adapter will scan, in manifest order. */
    public List<BuiltinGames.Entry> games() {
        return games.all();
    }

    /**
     * Scans every bundled game. Each game is scanned on its own so the hash to
     * canonical-id mapping is exact rather than guessed from content.
     */
    public ScanResult scan() {
        ArrayList<PhysicalPackage> packages = new ArrayList<>();
        ArrayList<PackageOutcome> packageOutcomes = new ArrayList<>();
        ArrayList<EntryOutcome> entryOutcomes = new ArrayList<>();
        ArrayList<ScanIssue> issues = new ArrayList<>();

        for (BuiltinGames.Entry game : games.all()) {
            CanonicalGame canonicalGame = canonicalGame(game);
            RomPackageScanner scanner = new RomPackageScanner(
                    ScanLimits.defaults(), (hash, format) -> canonicalGame.id());
            PackageCandidate candidate = new PackageCandidate(
                    "builtin-" + game.assetFilename,
                    game.assetFilename,
                    assetLocator(game.assetFilename),
                    () -> assets.open(game.assetPath()));

            ScanResult scanned = scanner.scan(SOURCE, Collections.singletonList(candidate));
            for (PhysicalPackage item : scanned.packages()) {
                ArrayList<RomVariant> variants = new ArrayList<>();
                for (RomVariant variant : item.variants()) {
                    variants.add(new RomVariant(
                            variant.id(), canonicalGame, variant.entryPath(), variant.romFormat(),
                            variant.compatibilityDecision(), variant.hashes(), variant.analysis(),
                            variant.zipEntryIdentity(), variant.zipNameEncoding()));
                }
                packages.add(new PhysicalPackage(
                        item.id(), item.source(), item.sourceUri(), item.originalFilename(),
                        item.packageFormat(), item.physicalPackageSha256(), variants));
            }
            packageOutcomes.addAll(scanned.packageOutcomes());
            entryOutcomes.addAll(scanned.entryOutcomes());
            issues.addAll(scanned.issues());
        }

        return new ScanResult(packages, packageOutcomes, entryOutcomes, issues);
    }

    private static CanonicalGame canonicalGame(BuiltinGames.Entry game) {
        return new CanonicalGame(game.canonicalId, Arrays.asList(
                new TitleCandidate(
                        game.titleEn, TitleCandidate.Language.EN,
                        TitleCandidate.Origin.BUILTIN_MANIFEST,
                        TitleCandidate.Confidence.VERIFIED,
                        TitleCandidate.ReviewState.VERIFIED),
                new TitleCandidate(
                        game.titleZhHans, TitleCandidate.Language.ZH_HANS,
                        TitleCandidate.Origin.BUILTIN_MANIFEST,
                        TitleCandidate.Confidence.VERIFIED,
                        TitleCandidate.ReviewState.VERIFIED)),
                Collections.emptyList());
    }
}
