package com.flynes.emu;

/**
 * A single entry in the game library: a ROM found on the device (SAF) or the
 * bundled From Below asset. Plain data holder with public fields, matching the
 * simple style of the rest of the app.
 */
public class GameEntry {

    /** Display name: filename minus extension, Chinese kept as-is. */
    public String name;
    /** SAF document URI, or file:///android_asset/... for the built-in asset. */
    public String uri;
    /** "assets" for the bundled homebrew, "saf" for user files. */
    public String source;
    /** File size in bytes, -1 when unknown. */
    public long size;
    /** iNES mapper number, -1 when unknown (zip / built-in). */
    public int mapper;
    /** PRG ROM size in KiB, -1 when unknown. */
    public int prgKb;
    /** CHR ROM size in KiB, -1 when unknown. */
    public int chrKb;
    /** True when the file is a .zip that contains a .nes. */
    public boolean zipped;
    /** Popularity score 0..100 from {@link Popularity}, 0 when unmatched. */
    public int popularity;
    /** Cached complete ROM content hash; unrelated to the archive hash or display language. */
    public String payloadSha256 = "";
    public String romName = "";
    public com.flynes.emu.app.NativeGameTitle titleMetadata =
            com.flynes.emu.app.NativeGameTitle.UNKNOWN;

    public GameEntry() {
    }

    public GameEntry(String name, String uri, String source, long size, int mapper,
                     int prgKb, int chrKb, boolean zipped, int popularity) {
        this.name = name;
        this.uri = uri;
        this.source = source;
        this.size = size;
        this.mapper = mapper;
        this.prgKb = prgKb;
        this.chrKb = chrKb;
        this.zipped = zipped;
        this.popularity = popularity;
    }

    /** The bundled From Below homebrew, pinned to the top of the library. */
    public static GameEntry builtinFromBelow() {
        return new GameEntry(
                "From Below",
                "file:///android_asset/roms/from_below.nes",
                "assets",
                0,
                -1, -1, -1,
                false,
                0);
    }
}
