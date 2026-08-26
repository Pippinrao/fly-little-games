#include <jni.h>
#include <android/native_window_jni.h>
#include <android/asset_manager_jni.h>

#include "egl_presenter.h"

using flynes::video::EglPresenter;

namespace {
EglPresenter* presenter(jlong handle) {
    return reinterpret_cast<EglPresenter*>(handle);
}
}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeCreate(
        JNIEnv* env, jclass, jobject asset_manager) {
    try {
        return reinterpret_cast<jlong>(new EglPresenter(
                asset_manager ? AAssetManager_fromJava(env, asset_manager) : nullptr));
    }
    catch (...) { return 0; }
}

JNIEXPORT jboolean JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeDestroy(JNIEnv*, jclass, jlong handle) {
    EglPresenter* target = presenter(handle);
    if (!target) return JNI_TRUE;
    if (!target->shutdown()) return JNI_FALSE;
    delete target;
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeSurfaceCreated(
        JNIEnv* env, jclass, jlong handle, jobject surface, jlong epoch) {
    EglPresenter* target = presenter(handle);
    if (!target || !surface || epoch <= 0) return JNI_FALSE;
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    return window && target->surface_created(window, static_cast<std::uint64_t>(epoch))
            ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeSurfaceChanged(
        JNIEnv*, jclass, jlong handle, jint width, jint height, jlong epoch) {
    if (EglPresenter* target = presenter(handle)) {
        target->surface_changed(width, height, static_cast<std::uint64_t>(epoch));
    }
}

JNIEXPORT jboolean JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeSurfaceDestroyed(
        JNIEnv*, jclass, jlong handle, jlong epoch) {
    if (EglPresenter* target = presenter(handle)) {
        return target->surface_destroyed(static_cast<std::uint64_t>(epoch))
                ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeEnqueue(
        JNIEnv* env, jclass, jlong handle, jobject pixels, jlong sequence,
        jint width, jint height, jint pitch, jint format, jint bytes) {
    EglPresenter* target = presenter(handle);
    if (!target || !pixels || sequence < 0) return JNI_FALSE;
    void* address = env->GetDirectBufferAddress(pixels);
    jlong capacity = env->GetDirectBufferCapacity(pixels);
    return address && capacity > 0 && target->enqueue(address, static_cast<std::size_t>(capacity),
            static_cast<std::uint64_t>(sequence), width, height, pitch, format, bytes)
            ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeSetFilter(
        JNIEnv*, jclass, jlong handle, jint filter) {
    if (EglPresenter* target = presenter(handle)) target->set_filter(filter);
}

JNIEXPORT void JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeSetActive(
        JNIEnv*, jclass, jlong handle, jboolean active) {
    if (EglPresenter* target = presenter(handle)) target->set_active(active == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeResetSequence(
        JNIEnv*, jclass, jlong handle) {
    if (EglPresenter* target = presenter(handle)) target->reset_sequence();
}

JNIEXPORT jboolean JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeRequestFrameRate(
        JNIEnv*, jclass, jlong handle, jlong epoch, jfloat source_fps) {
    if (EglPresenter* target = presenter(handle)) {
        return target->request_frame_rate(static_cast<std::uint64_t>(epoch), source_fps)
                ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeClearFrameRate(
        JNIEnv*, jclass, jlong handle, jlong epoch) {
    if (EglPresenter* target = presenter(handle)) {
        return target->clear_frame_rate(static_cast<std::uint64_t>(epoch))
                ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

JNIEXPORT jlongArray JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeGetStats(
        JNIEnv* env, jclass, jlong handle) {
    EglPresenter* target = presenter(handle);
    if (!target) return nullptr;
    auto snapshot = target->stats();
    const jlong values[] = {static_cast<jlong>(snapshot.uploads),
                            static_cast<jlong>(snapshot.submissions),
                            static_cast<jlong>(snapshot.skipped_sequences),
                            static_cast<jlong>(snapshot.last_sequence),
                            static_cast<jlong>(snapshot.surface_epoch),
                            static_cast<jlong>(snapshot.runtime_failure_count),
                            static_cast<jlong>(snapshot.runtime_failure_code),
                            static_cast<jlong>(snapshot.gpu_timing_status),
                            static_cast<jlong>(snapshot.last_gpu_duration_ns),
                            static_cast<jlong>(snapshot.requested_frame_rate_millihz),
                            static_cast<jlong>(snapshot.frame_rate_vote_status)};
    jlongArray result = env->NewLongArray(11);
    if (result) env->SetLongArrayRegion(result, 0, 11, values);
    return result;
}

}  // extern "C"
