#include <jni.h>

#include <EGL/egl.h>
#include <android/asset_manager_jni.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "spatial_pipeline.h"

namespace {
using flynes::video::SpatialPipeline;

class OwnedPbufferContext {
public:
    bool create() {
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display_ == EGL_NO_DISPLAY || eglInitialize(display_, nullptr, nullptr) != EGL_TRUE) {
            destroy();
            return false;
        }
        constexpr EGLint kEs3Bit = 0x40;
        const EGLint config_attributes[] = {
                EGL_RENDERABLE_TYPE, kEs3Bit,
                EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                EGL_NONE};
        EGLConfig config = nullptr;
        EGLint count = 0;
        if (eglChooseConfig(display_, config_attributes, &config, 1, &count) != EGL_TRUE
                || count != 1) {
            destroy();
            return false;
        }
        const EGLint pbuffer_attributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        surface_ = eglCreatePbufferSurface(display_, config, pbuffer_attributes);
        const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, context_attributes);
        if (surface_ == EGL_NO_SURFACE || context_ == EGL_NO_CONTEXT
                || eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
            destroy();
            return false;
        }
        return true;
    }

    ~OwnedPbufferContext() { destroy(); }

private:
    void destroy() {
        if (display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
            if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
            eglTerminate(display_);
        }
        display_ = EGL_NO_DISPLAY;
        context_ = EGL_NO_CONTEXT;
        surface_ = EGL_NO_SURFACE;
    }

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
};

jintArray failure(JNIEnv* env, SpatialPipeline::Failure code) {
    const jint values[] = {static_cast<jint>(code), 0, 0};
    jintArray result = env->NewIntArray(3);
    if (result) env->SetIntArrayRegion(result, 0, 3, values);
    return result;
}
}  // namespace

extern "C" JNIEXPORT jintArray JNICALL
Java_com_flynes_emu_video_SpatialOffscreenRenderer_nativeRender(
        JNIEnv* env, jclass, jobject asset_manager, jint algorithm, jintArray pixels,
        jint width, jint height) {
    if (!asset_manager || !pixels || width <= 0 || height <= 0
            || algorithm < static_cast<jint>(SpatialPipeline::Algorithm::NEAREST_2X)
            || algorithm > static_cast<jint>(SpatialPipeline::Algorithm::SCALEFX_3X)
            || static_cast<jlong>(width) * height != env->GetArrayLength(pixels)) {
        return failure(env, SpatialPipeline::Failure::INVALID_INPUT);
    }
    std::vector<std::uint32_t> source(static_cast<std::size_t>(width) * height);
    std::vector<jint> java_pixels(source.size());
    env->GetIntArrayRegion(pixels, 0, static_cast<jsize>(java_pixels.size()),
                           java_pixels.data());
    if (env->ExceptionCheck()) return nullptr;
    for (std::size_t index = 0; index < source.size(); index++) {
        source[index] = static_cast<std::uint32_t>(java_pixels[index]);
    }

    OwnedPbufferContext context;
    if (!context.create()) {
        return failure(env, SpatialPipeline::Failure::EGL_CONTEXT_UNAVAILABLE);
    }
    SpatialPipeline pipeline;
    SpatialPipeline::Output output = pipeline.render(
            AAssetManager_fromJava(env, asset_manager),
            static_cast<SpatialPipeline::Algorithm>(algorithm),
            source.data(), width, height);
    if (output.failure != SpatialPipeline::Failure::NONE) {
        return failure(env, output.failure);
    }
    if (output.pixels.size() > static_cast<std::size_t>(std::numeric_limits<jsize>::max() - 3)) {
        return failure(env, SpatialPipeline::Failure::INVALID_INPUT);
    }
    std::vector<jint> encoded(output.pixels.size() + 3);
    encoded[0] = 0;
    encoded[1] = output.width;
    encoded[2] = output.height;
    for (std::size_t index = 0; index < output.pixels.size(); index++) {
        encoded[index + 3] = static_cast<jint>(output.pixels[index]);
    }
    jintArray result = env->NewIntArray(static_cast<jsize>(encoded.size()));
    if (result) env->SetIntArrayRegion(result, 0, static_cast<jsize>(encoded.size()),
                                       encoded.data());
    return result;
}
