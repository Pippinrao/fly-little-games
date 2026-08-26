package com.flynes.emu.video.quality;

public final class BuildAlgorithmAvailability {
    private final boolean mmpxIncluded;
    private final boolean scaleFxIncluded;
    private final boolean motionCompensationIncluded;
    private final int mmpxOracleVersion;
    private final String mmpxImplementationHash;
    private final String scaleFxUpstreamCommit;
    private final String scaleFxImplementationHash;
    private final int motionAlgorithmVersion;
    private final String motionImplementationHash;

    public BuildAlgorithmAvailability(boolean mmpxIncluded, boolean scaleFxIncluded,
                                      boolean motionCompensationIncluded, int mmpxOracleVersion,
                                      String mmpxImplementationHash, String scaleFxUpstreamCommit,
                                      String scaleFxImplementationHash, int motionAlgorithmVersion,
                                      String motionImplementationHash) {
        this.mmpxIncluded = mmpxIncluded;
        this.scaleFxIncluded = scaleFxIncluded;
        this.motionCompensationIncluded = motionCompensationIncluded;
        this.mmpxOracleVersion = mmpxOracleVersion;
        this.mmpxImplementationHash = value(mmpxImplementationHash);
        this.scaleFxUpstreamCommit = value(scaleFxUpstreamCommit);
        this.scaleFxImplementationHash = value(scaleFxImplementationHash);
        this.motionAlgorithmVersion = motionAlgorithmVersion;
        this.motionImplementationHash = value(motionImplementationHash);
    }
    public static BuildAlgorithmAvailability baseOnly() {
        return new BuildAlgorithmAvailability(false, false, false, 0, "", "", "", 0, "");
    }
    private static String value(String value) { return value == null ? "" : value; }
    public boolean mmpxIncluded() { return mmpxIncluded; }
    public boolean scaleFxIncluded() { return scaleFxIncluded; }
    public boolean motionCompensationIncluded() { return motionCompensationIncluded; }
    public int mmpxOracleVersion() { return mmpxOracleVersion; }
    public String mmpxImplementationHash() { return mmpxImplementationHash; }
    public String scaleFxUpstreamCommit() { return scaleFxUpstreamCommit; }
    public String scaleFxImplementationHash() { return scaleFxImplementationHash; }
    public int motionAlgorithmVersion() { return motionAlgorithmVersion; }
    public String motionImplementationHash() { return motionImplementationHash; }
}
