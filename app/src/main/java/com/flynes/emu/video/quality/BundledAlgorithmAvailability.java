package com.flynes.emu.video.quality;

/** Immutable provenance for spatial algorithms actually packaged in this build. */
public final class BundledAlgorithmAvailability {
    private static final String MMPX_IMPLEMENTATION_SHA256 =
            "92256e17caa772247282d3bd185f236e35be964e65675d7f19af686ea1987a0d";
    private static final String SCALEFX_UPSTREAM_COMMIT =
            "4f4eb801b2dbcaed0a9669a9deec1a098f3623d8";
    private static final String SCALEFX_IMPLEMENTATION_SHA256 =
            "9689c6c12ec3caefdc2494181dd24c3c521f9ecebbd5d5c4f944265127b44140";

    private BundledAlgorithmAvailability() { }

    public static BuildAlgorithmAvailability current() {
        return new BuildAlgorithmAvailability(true, true, false, 1,
                MMPX_IMPLEMENTATION_SHA256, SCALEFX_UPSTREAM_COMMIT,
                SCALEFX_IMPLEMENTATION_SHA256, 0, "");
    }
}
