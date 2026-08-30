package com.flynes.emu.video.status;

import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES20;

import com.flynes.emu.video.quality.GlCapabilities;

import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.atomic.AtomicReference;

/** Owns short-lived probe contexts; it never borrows or mutates the game renderer context. */
public final class GlCapabilityProbe {
    public enum State { UNKNOWN, KNOWN }

    public interface Backend {
        Es2Session openBoundedEs2Pbuffer(int width, int height);
        Es31Attempt attemptSeparateEs31Context();
    }

    public interface Es2Session extends AutoCloseable {
        Es2Properties readProperties();
        @Override void close();
    }

    public static final class Es2Properties {
        final String vendor;
        final String renderer;
        final String version;
        final Set<String> extensions;
        final int maxTextureSize;
        final boolean fragmentHighp;
        final boolean halfFloatRenderable;
        final boolean halfFloatFilterable;
        final boolean floatRenderable;
        final boolean disjointTimerQuery;

        public Es2Properties(String vendor, String renderer, String version,
                             Set<String> extensions, int maxTextureSize,
                             boolean fragmentHighp, boolean halfFloatRenderable,
                             boolean halfFloatFilterable, boolean floatRenderable,
                             boolean disjointTimerQuery) {
            this.vendor = vendor;
            this.renderer = renderer;
            this.version = version;
            this.extensions = Collections.unmodifiableSet(new LinkedHashSet<>(extensions));
            this.maxTextureSize = maxTextureSize;
            this.fragmentHighp = fragmentHighp;
            this.halfFloatRenderable = halfFloatRenderable;
            this.halfFloatFilterable = halfFloatFilterable;
            this.floatRenderable = floatRenderable;
            this.disjointTimerQuery = disjointTimerQuery;
        }
    }

    public static final class Es31Attempt implements AutoCloseable {
        private final boolean supportsCompute;
        private final boolean presenterContextCreated;
        private final Runnable closeAction;
        private boolean closed;

        public Es31Attempt(boolean supportsCompute, boolean presenterContextCreated,
                           Runnable closeAction) {
            this.supportsCompute = supportsCompute;
            this.presenterContextCreated = presenterContextCreated;
            this.closeAction = Objects.requireNonNull(closeAction, "closeAction");
        }
        public boolean supportsCompute() { return supportsCompute; }
        public boolean presenterContextCreated() { return presenterContextCreated; }
        @Override public void close() {
            if (closed) return;
            closed = true;
            closeAction.run();
        }
    }

    public static final class Snapshot {
        private final State state;
        private final GlCapabilities capabilities;
        Snapshot(State state, GlCapabilities capabilities) {
            this.state = state;
            this.capabilities = capabilities;
        }
        public State state() { return state; }
        public GlCapabilities capabilities() { return capabilities; }
    }

    private final Backend backend;
    private final AtomicReference<Snapshot> snapshot = new AtomicReference<>(
            new Snapshot(State.UNKNOWN, GlCapabilities.unknown()));

    public GlCapabilityProbe(Backend backend) {
        this.backend = Objects.requireNonNull(backend, "backend");
    }

    public Snapshot snapshot() { return snapshot.get(); }

    public synchronized Snapshot probe() {
        snapshot.set(new Snapshot(State.UNKNOWN, GlCapabilities.unknown()));
        try (Es2Session es2 = backend.openBoundedEs2Pbuffer(16, 16)) {
            Es2Properties properties = es2.readProperties();
            boolean compute = false;
            boolean presenter = false;
            try (Es31Attempt attempt = backend.attemptSeparateEs31Context()) {
                if (attempt != null) {
                    compute = attempt.supportsCompute();
                    presenter = attempt.presenterContextCreated();
                }
            } catch (RuntimeException unsupported) {
                // ES2 facts remain known; an ES3.1 failure is a capability result, not a probe lie.
            }
            GlCapabilities capabilities = new GlCapabilities(2, 0, properties.vendor,
                    properties.renderer, properties.version, properties.extensions,
                    properties.maxTextureSize, properties.fragmentHighp,
                    properties.halfFloatRenderable, properties.halfFloatFilterable,
                    properties.floatRenderable, properties.disjointTimerQuery,
                    compute, presenter);
            Snapshot known = new Snapshot(State.KNOWN, capabilities);
            snapshot.set(known);
            return known;
        } catch (RuntimeException failure) {
            Snapshot unknown = new Snapshot(State.UNKNOWN, GlCapabilities.unknown());
            snapshot.set(unknown);
            return unknown;
        }
    }

    public void invalidateForContextLoss() {
        snapshot.set(new Snapshot(State.UNKNOWN, GlCapabilities.unknown()));
    }

    public static final class AndroidBackend implements Backend {
        @Override public Es2Session openBoundedEs2Pbuffer(int width, int height) {
            return OwnedEglSession.create(width, height, 2);
        }

        @Override public Es31Attempt attemptSeparateEs31Context() {
            OwnedEglSession session = OwnedEglSession.create(16, 16, 3);
            boolean es31 = isAtLeastEs31(safeGlString(GLES20.GL_VERSION));
            return new Es31Attempt(es31, es31, session::close);
        }

        private static boolean isAtLeastEs31(String version) {
            if (version == null) return false;
            int marker = version.indexOf("OpenGL ES ");
            if (marker < 0) return false;
            String suffix = version.substring(marker + "OpenGL ES ".length());
            return suffix.startsWith("3.1") || suffix.startsWith("3.2")
                    || suffix.startsWith("4.");
        }
    }

    private static final class OwnedEglSession implements Es2Session {
        private EGLDisplay display;
        private EGLSurface surface;
        private EGLContext context;
        private boolean closed;

        static OwnedEglSession create(int width, int height, int clientVersion) {
            OwnedEglSession owned = new OwnedEglSession();
            try {
                owned.display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
                if (owned.display == EGL14.EGL_NO_DISPLAY) throw eglFailure("eglGetDisplay");
                int[] versions = new int[2];
                if (!EGL14.eglInitialize(owned.display, versions, 0, versions, 1))
                    throw eglFailure("eglInitialize");
                int renderable = clientVersion >= 3 ? 0x40 : EGL14.EGL_OPENGL_ES2_BIT;
                int[] configAttributes = {EGL14.EGL_RENDERABLE_TYPE, renderable,
                        EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT,
                        EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8,
                        EGL14.EGL_BLUE_SIZE, 8, EGL14.EGL_NONE};
                EGLConfig[] configs = new EGLConfig[1];
                int[] count = new int[1];
                if (!EGL14.eglChooseConfig(owned.display, configAttributes, 0,
                        configs, 0, 1, count, 0) || count[0] == 0)
                    throw eglFailure("eglChooseConfig");
                int[] pbuffer = {EGL14.EGL_WIDTH, width, EGL14.EGL_HEIGHT, height,
                        EGL14.EGL_NONE};
                owned.surface = EGL14.eglCreatePbufferSurface(owned.display, configs[0],
                        pbuffer, 0);
                if (owned.surface == EGL14.EGL_NO_SURFACE)
                    throw eglFailure("eglCreatePbufferSurface");
                int[] contextAttributes = {EGL14.EGL_CONTEXT_CLIENT_VERSION, clientVersion,
                        EGL14.EGL_NONE};
                owned.context = EGL14.eglCreateContext(owned.display, configs[0],
                        EGL14.EGL_NO_CONTEXT, contextAttributes, 0);
                if (owned.context == EGL14.EGL_NO_CONTEXT)
                    throw eglFailure("eglCreateContext");
                if (!EGL14.eglMakeCurrent(owned.display, owned.surface, owned.surface,
                        owned.context)) throw eglFailure("eglMakeCurrent");
                return owned;
            } catch (RuntimeException failure) {
                owned.close();
                throw failure;
            }
        }

        @Override public Es2Properties readProperties() {
            ensureOpen();
            String vendor = safeGlString(GLES20.GL_VENDOR);
            String renderer = safeGlString(GLES20.GL_RENDERER);
            String version = safeGlString(GLES20.GL_VERSION);
            Set<String> extensions = splitExtensions(safeGlString(GLES20.GL_EXTENSIONS));
            int[] maxTexture = new int[1];
            GLES20.glGetIntegerv(GLES20.GL_MAX_TEXTURE_SIZE, maxTexture, 0);
            int[] range = new int[2];
            int[] precision = new int[1];
            GLES20.glGetShaderPrecisionFormat(GLES20.GL_FRAGMENT_SHADER,
                    GLES20.GL_HIGH_FLOAT, range, 0, precision, 0);
            GlColorTargetProbe.Result half = GlColorTargetProbe.probe(
                    GlColorTargetProbe.GL_HALF_FLOAT_OES);
            GlColorTargetProbe.Result full = GlColorTargetProbe.probe(GLES20.GL_FLOAT);
            boolean halfRenderable = half.renderableAndSampled();
            boolean halfFilterable = half.linearlyFilterable();
            boolean floatRenderable = full.renderableAndSampled();
            boolean disjoint = extensions.contains("GL_EXT_disjoint_timer_query");
            if (vendor.isEmpty() || renderer.isEmpty() || maxTexture[0] <= 0)
                throw new IllegalStateException("incomplete GL identity");
            return new Es2Properties(vendor, renderer, version, extensions, maxTexture[0],
                    precision[0] > 0, halfRenderable, halfFilterable, floatRenderable, disjoint);
        }

        private void ensureOpen() {
            if (closed) throw new IllegalStateException("EGL session is closed");
        }

        @Override public void close() {
            if (closed) return;
            closed = true;
            if (display != null && display != EGL14.EGL_NO_DISPLAY) {
                EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE,
                        EGL14.EGL_NO_CONTEXT);
                if (surface != null && surface != EGL14.EGL_NO_SURFACE)
                    EGL14.eglDestroySurface(display, surface);
                if (context != null && context != EGL14.EGL_NO_CONTEXT)
                    EGL14.eglDestroyContext(display, context);
                EGL14.eglTerminate(display);
            }
            display = null;
            surface = null;
            context = null;
        }
    }

    private static IllegalStateException eglFailure(String operation) {
        return new IllegalStateException(operation + " failed: 0x"
                + Integer.toHexString(EGL14.eglGetError()));
    }

    private static String safeGlString(int name) {
        String value = GLES20.glGetString(name);
        return value == null ? "" : value;
    }

    private static Set<String> splitExtensions(String value) {
        if (value == null || value.trim().isEmpty()) return Collections.emptySet();
        return new LinkedHashSet<>(Arrays.asList(value.trim().split("\\s+")));
    }
}
