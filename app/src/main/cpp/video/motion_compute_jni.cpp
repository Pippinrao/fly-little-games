#include <jni.h>

#include <EGL/egl.h>
#include <GLES3/gl31.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "motion_compute_pipeline.h"
#include "video/temporal_interpolator.h"

namespace {
using flynes::video::MotionComputePipeline;

class OwnedEs31Pbuffer {
public:
    bool create() {
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display_ == EGL_NO_DISPLAY || eglInitialize(display_, nullptr, nullptr) != EGL_TRUE)
            return false;
        constexpr EGLint kEs3Bit = 0x40;
        const EGLint attributes[] = {EGL_RENDERABLE_TYPE, kEs3Bit,
                EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8, EGL_NONE};
        EGLConfig config = nullptr;
        EGLint count = 0;
        if (eglChooseConfig(display_, attributes, &config, 1, &count) != EGL_TRUE || count != 1)
            return false;
        const EGLint pbuffer[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        surface_ = eglCreatePbufferSurface(display_, config, pbuffer);
        const EGLint context[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, context);
        if (surface_ == EGL_NO_SURFACE || context_ == EGL_NO_CONTEXT
                || eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) return false;
        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        return major > 3 || (major == 3 && minor >= 1);
    }
    ~OwnedEs31Pbuffer() {
        if (display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
            if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
            eglTerminate(display_);
        }
    }
private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
};

jintArray encode_failure(JNIEnv* env, MotionComputePipeline::Failure failure) {
    const jint values[] = {static_cast<jint>(failure), 0, 0};
    jintArray result = env->NewIntArray(3);
    if (result) env->SetIntArrayRegion(result, 0, 3, values);
    return result;
}

jintArray encode_output(JNIEnv* env, const std::vector<std::uint16_t>& pixels,
                        int width, int height) {
    if (pixels.size() > static_cast<std::size_t>(std::numeric_limits<jsize>::max() - 3))
        return encode_failure(env, MotionComputePipeline::Failure::INVALID_INPUT);
    std::vector<jint> encoded(pixels.size() + 3);
    encoded[0] = 0;
    encoded[1] = width;
    encoded[2] = height;
    for (std::size_t i = 0; i < pixels.size(); ++i) encoded[i + 3] = pixels[i];
    jintArray result = env->NewIntArray(static_cast<jsize>(encoded.size()));
    if (result) env->SetIntArrayRegion(result, 0, static_cast<jsize>(encoded.size()),
                                       encoded.data());
    return result;
}
}  // namespace

extern "C" JNIEXPORT jintArray JNICALL
Java_com_flynes_emu_video_MotionOffscreenRenderer_nativeRender(
        JNIEnv* env, jclass, jshortArray frame_a, jshortArray frame_b,
        jint width, jint height, jboolean gpu, jboolean force_context_failure) {
    if (!frame_a || !frame_b || width <= 0 || height <= 0
            || static_cast<jlong>(width) * height != env->GetArrayLength(frame_a)
            || env->GetArrayLength(frame_a) != env->GetArrayLength(frame_b)) {
        return encode_failure(env, MotionComputePipeline::Failure::INVALID_INPUT);
    }
    const std::size_t count = static_cast<std::size_t>(width) * height;
    std::vector<jshort> java_a(count), java_b(count);
    env->GetShortArrayRegion(frame_a, 0, static_cast<jsize>(count), java_a.data());
    env->GetShortArrayRegion(frame_b, 0, static_cast<jsize>(count), java_b.data());
    if (env->ExceptionCheck()) return nullptr;
    std::vector<std::uint16_t> a(count), b(count), output(count);
    for (std::size_t i = 0; i < count; ++i) {
        a[i] = static_cast<std::uint16_t>(java_a[i]);
        b[i] = static_cast<std::uint16_t>(java_b[i]);
    }
    if (gpu != JNI_TRUE) {
        flynes::video::TemporalInterpolator oracle;
        auto result = oracle.interpolate_midpoint(
                {a.data(), width, height, width}, {b.data(), width, height, width},
                {output.data(), width, height, width});
        if (result.reason == flynes::video::TemporalHoldReason::INVALID_INPUT)
            return encode_failure(env, MotionComputePipeline::Failure::INVALID_INPUT);
        return encode_output(env, output, width, height);
    }
    if (force_context_failure == JNI_TRUE)
        return encode_failure(env, MotionComputePipeline::Failure::EGL_CONTEXT_UNAVAILABLE);
    OwnedEs31Pbuffer context;
    if (!context.create())
        return encode_failure(env, MotionComputePipeline::Failure::EGL_CONTEXT_UNAVAILABLE);
    MotionComputePipeline pipeline;
    if (!pipeline.initialize())
        return encode_failure(env, MotionComputePipeline::Failure::COMPUTE_SHADER_FAILURE);
    auto result = pipeline.interpolate(a.data(), b.data(), width, height);
    if (result.failure != MotionComputePipeline::Failure::NONE)
        return encode_failure(env, result.failure);
    return encode_output(env, result.pixels, width, height);
}
