package com.flynes.emu.video.quality;

/** Preserves native presenter failure categories when constraining the resolver. */
public final class PresenterFailureMapper {
    private PresenterFailureMapper() { }

    public static RuntimeFailure fromNativeCode(int code) {
        switch (code) {
            case 0: return null;
            case 1: return RuntimeFailure.SHADER;
            case 2: return RuntimeFailure.FRAMEBUFFER;
            case 3: return RuntimeFailure.GL;
            default: return RuntimeFailure.CONTEXT;
        }
    }
}
