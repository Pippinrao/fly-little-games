package com.flynes.emu.video.quality;

import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Set;

public final class GlCapabilities {
    private final int activeContextMajorVersion;
    private final int activeContextMinorVersion;
    private final String vendor;
    private final String renderer;
    private final String version;
    private final Set<String> extensions;
    private final int maxTextureSize;
    private final boolean fragmentHighp;
    private final boolean halfFloatColorFramebufferRenderable;
    private final boolean halfFloatColorFramebufferFilterable;
    private final boolean floatColorFramebufferRenderable;
    private final boolean disjointTimerQuery;
    private final boolean supportsEs31Compute;
    private final boolean canCreateOwnedEs31Presenter;

    public GlCapabilities(int activeContextMajorVersion, int activeContextMinorVersion,
                          String vendor, String renderer, String version, Set<String> extensions,
                          int maxTextureSize, boolean fragmentHighp,
                          boolean halfFloatColorFramebufferRenderable,
                          boolean halfFloatColorFramebufferFilterable,
                          boolean floatColorFramebufferRenderable, boolean disjointTimerQuery,
                          boolean supportsEs31Compute, boolean canCreateOwnedEs31Presenter) {
        this.activeContextMajorVersion = activeContextMajorVersion;
        this.activeContextMinorVersion = activeContextMinorVersion;
        this.vendor = vendor == null ? "" : vendor;
        this.renderer = renderer == null ? "" : renderer;
        this.version = version == null ? "" : version;
        this.extensions = Collections.unmodifiableSet(new LinkedHashSet<>(
                extensions == null ? Collections.emptySet() : extensions));
        this.maxTextureSize = maxTextureSize;
        this.fragmentHighp = fragmentHighp;
        this.halfFloatColorFramebufferRenderable = halfFloatColorFramebufferRenderable;
        this.halfFloatColorFramebufferFilterable = halfFloatColorFramebufferFilterable;
        this.floatColorFramebufferRenderable = floatColorFramebufferRenderable;
        this.disjointTimerQuery = disjointTimerQuery;
        this.supportsEs31Compute = supportsEs31Compute;
        this.canCreateOwnedEs31Presenter = canCreateOwnedEs31Presenter;
    }
    public static GlCapabilities unknown() {
        return new GlCapabilities(0, 0, "", "", "", Collections.emptySet(), 0,
                false, false, false, false, false, false, false);
    }
    public boolean knownForAdvancedRendering() {
        return activeContextMajorVersion >= 3 && maxTextureSize > 0 && fragmentHighp;
    }
    public boolean supportsMmpx2x() {
        return activeContextMajorVersion >= 2 && maxTextureSize >= 512 && fragmentHighp;
    }
    public boolean supportsScaleFx3x() {
        return activeContextMajorVersion >= 2 && maxTextureSize >= 768 && fragmentHighp
                && ((halfFloatColorFramebufferRenderable
                && halfFloatColorFramebufferFilterable)
                || floatColorFramebufferRenderable);
    }
    public int activeContextMajorVersion() { return activeContextMajorVersion; }
    public int activeContextMinorVersion() { return activeContextMinorVersion; }
    public String vendor() { return vendor; }
    public String renderer() { return renderer; }
    public String version() { return version; }
    public Set<String> extensions() { return extensions; }
    public int maxTextureSize() { return maxTextureSize; }
    public boolean fragmentHighp() { return fragmentHighp; }
    public boolean halfFloatColorFramebufferRenderable() {
        return halfFloatColorFramebufferRenderable;
    }
    public boolean halfFloatColorFramebufferFilterable() {
        return halfFloatColorFramebufferFilterable;
    }
    public boolean floatColorFramebufferRenderable() { return floatColorFramebufferRenderable; }
    public boolean disjointTimerQuery() { return disjointTimerQuery; }
    public boolean supportsEs31Compute() { return supportsEs31Compute; }
    public boolean canCreateOwnedEs31Presenter() { return canCreateOwnedEs31Presenter; }
}
