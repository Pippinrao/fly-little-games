#include <jni.h>
#include <android/native_window_jni.h>
#include <android/asset_manager_jni.h>
#include <swappy/swappyGL.h>

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
        JNIEnv* env, jclass, jobject asset_manager, jobject activity_context) {
    try {
        jclass activity_class = env->FindClass("android/app/Activity");
        const bool is_activity = activity_context && activity_class
                && env->IsInstanceOf(activity_context, activity_class);
        if (activity_class) env->DeleteLocalRef(activity_class);
        const bool swappy_initialized = is_activity && SwappyGL_init(env, activity_context);
        return reinterpret_cast<jlong>(new EglPresenter(
                asset_manager ? AAssetManager_fromJava(env, asset_manager) : nullptr,
                swappy_initialized));
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

JNIEXPORT jboolean JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeConfigureMotion(
        JNIEnv*, jclass, jlong handle, jlong epoch, jlong display_generation,
        jfloat display_hz, jfloat source_fps, jlong lease_deadline_ns,
        jboolean force_pacer_disabled) {
    if (EglPresenter* target = presenter(handle)) {
        return target->configure_motion(static_cast<std::uint64_t>(epoch),
                static_cast<std::uint64_t>(display_generation), display_hz, source_fps,
                static_cast<std::int64_t>(lease_deadline_ns),
                force_pacer_disabled == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeUpdateMotionLease(
        JNIEnv*, jclass, jlong handle, jlong epoch, jlong display_generation,
        jlong lease_deadline_ns) {
    if (EglPresenter* target = presenter(handle)) {
        return target->update_motion_lease(static_cast<std::uint64_t>(epoch),
                static_cast<std::uint64_t>(display_generation),
                static_cast<std::int64_t>(lease_deadline_ns)) ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeBeginMotionShadow(
        JNIEnv*, jclass, jlong handle, jlong epoch, jfloat source_fps) {
    if (EglPresenter* target = presenter(handle)) {
        return static_cast<jlong>(target->begin_motion_shadow(
                static_cast<std::uint64_t>(epoch), source_fps));
    }
    return -1;
}

JNIEXPORT jlong JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeExitMotion(
        JNIEnv*, jclass, jlong handle, jlong epoch, jboolean drain_to_immediate) {
    if (EglPresenter* target = presenter(handle)) {
        return static_cast<jlong>(target->exit_motion(static_cast<std::uint64_t>(epoch),
                drain_to_immediate == JNI_TRUE));
    }
    return -1;
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
                            static_cast<jlong>(snapshot.frame_rate_vote_status),
                            static_cast<jlong>(snapshot.last_presentation_ns),
                            static_cast<jlong>(snapshot.real_slots),
                            static_cast<jlong>(snapshot.interpolated_slots),
                            static_cast<jlong>(snapshot.held_slots),
                            static_cast<jlong>(snapshot.warped_slots),
                            static_cast<jlong>(snapshot.cadence_adjustments),
                            static_cast<jlong>(snapshot.swappy_total_frames),
                            static_cast<jlong>(snapshot.swappy_last_progress_ns),
                            static_cast<jlong>(snapshot.motion_queue_depth),
                            static_cast<jlong>(snapshot.last_pair_a_sequence),
                            static_cast<jlong>(snapshot.last_pair_b_sequence),
                            static_cast<jlong>(snapshot.actual_presentation_count),
                            static_cast<jlong>(snapshot.last_actual_presentation_ns),
                            static_cast<jlong>(snapshot.last_actual_real_sequence),
                            static_cast<jlong>(snapshot.last_actual_real_presentation_ns),
                            static_cast<jlong>(snapshot.pacing_owner),
                            static_cast<jlong>(snapshot.last_transition_id),
                            static_cast<jlong>(snapshot.temporal_state),
                            static_cast<jlong>(snapshot.shadow_pair_count),
                            static_cast<jlong>(snapshot.shadow_unsafe_ppm_sum),
                            static_cast<jlong>(snapshot.shadow_peak_unsafe_ppm),
                            static_cast<jlong>(snapshot.shadow_gpu_timing_status),
                            static_cast<jlong>(snapshot.shadow_last_gpu_duration_ns),
                            static_cast<jlong>(snapshot.shadow_gpu_valid_sample_count),
                            static_cast<jlong>(snapshot.shadow_peak_gpu_duration_ns)};
    jlongArray result = env->NewLongArray(36);
    if (result) env->SetLongArrayRegion(result, 0, 36, values);
    return result;
}

JNIEXPORT jlong JNICALL
Java_com_flynes_emu_video_NativeVideoPresenter_nativeGetActualRealPresentationNs(
        JNIEnv*, jclass, jlong handle, jlong sequence) {
    EglPresenter* target = presenter(handle);
    if (!target || sequence < 0) return -1;
    return static_cast<jlong>(target->actual_real_presentation_ns(
            static_cast<std::uint64_t>(sequence)));
}

}  // extern "C"
