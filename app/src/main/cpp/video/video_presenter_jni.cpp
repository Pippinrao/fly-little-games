#include <jni.h>
#include <android/native_window_jni.h>

#include "egl_presenter.h"

using flynes::video::EglPresenter;

namespace {
EglPresenter* presenter(jlong handle) {
    return reinterpret_cast<EglPresenter*>(handle);
}
}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeCreate(JNIEnv*, jclass) {
    try { return reinterpret_cast<jlong>(new EglPresenter()); }
    catch (...) { return 0; }
}

JNIEXPORT void JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeDestroy(JNIEnv*, jclass, jlong handle) {
    delete presenter(handle);
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

JNIEXPORT void JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeSurfaceDestroyed(
        JNIEnv*, jclass, jlong handle, jlong epoch) {
    if (EglPresenter* target = presenter(handle)) {
        target->surface_destroyed(static_cast<std::uint64_t>(epoch));
    }
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
                            static_cast<jlong>(snapshot.surface_epoch)};
    jlongArray result = env->NewLongArray(5);
    if (result) env->SetLongArrayRegion(result, 0, 5, values);
    return result;
}

}  // extern "C"
