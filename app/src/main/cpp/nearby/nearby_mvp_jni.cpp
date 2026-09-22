#include <jni.h>
#include <android/log.h>

#include <flynes/flynes_nearby_mvp.h>

#include <cstdint>
#include <string>

namespace {
void diagnostic(void* context, const char* line) {
    __android_log_print(ANDROID_LOG_INFO, "FlyNesNearby", "owner=%p %s", context, line);
}
fly_lan_mvp_session* session_from(jlong handle) {
    return reinterpret_cast<fly_lan_mvp_session*>(static_cast<std::uintptr_t>(handle));
}
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_flynes_emu_NearbyMvpSession_nativeCreate(JNIEnv*, jclass) {
    auto* session = fly_lan_mvp_create();
    fly_lan_mvp_set_diagnostic_sink(session, diagnostic, session);
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(session));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_flynes_emu_NearbyMvpSession_nativeHost(JNIEnv* env, jclass, jlong handle,
                                                 jstring ipv4, jbyteArray token) {
    if (handle == 0 || ipv4 == nullptr || token == nullptr ||
        env->GetArrayLength(token) != 16) return JNI_FALSE;
    const char* address = env->GetStringUTFChars(ipv4, nullptr);
    if (address == nullptr) return JNI_FALSE;
    std::uint8_t bytes[16]{};
    env->GetByteArrayRegion(token, 0, 16, reinterpret_cast<jbyte*>(bytes));
    const int result = fly_lan_mvp_host(session_from(handle), address, bytes);
    env->ReleaseStringUTFChars(ipv4, address);
    for (auto& byte : bytes) byte = 0;
    return result == 1 ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_flynes_emu_NearbyMvpSession_nativeInvite(JNIEnv* env, jclass, jlong handle) {
    if (handle == 0) return nullptr;
    const auto size = fly_lan_mvp_copy_invite(session_from(handle), nullptr, 0);
    if (size == 0 || size > 256) return nullptr;
    std::string bytes(size, '\0');
    if (fly_lan_mvp_copy_invite(session_from(handle), bytes.data(), bytes.size()) != size)
        return nullptr;
    return env->NewStringUTF(bytes.c_str());
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_flynes_emu_NearbyMvpSession_nativeSnapshot(JNIEnv* env, jclass, jlong handle) {
    fly_lan_mvp_snapshot snapshot{};
    if (handle == 0 || !fly_lan_mvp_snapshot_read(session_from(handle), &snapshot))
        return nullptr;
    const jint values[11] = {static_cast<jint>(snapshot.state),
                            static_cast<jint>(snapshot.reason),
                            static_cast<jint>(snapshot.transport_result),
                            static_cast<jint>(snapshot.transport_operation),
                            static_cast<jint>(snapshot.role),
                            static_cast<jint>(snapshot.local_configured),
                            static_cast<jint>(snapshot.peer_configured),
                            static_cast<jint>(snapshot.local_ready),
                            static_cast<jint>(snapshot.peer_ready),
                            static_cast<jint>(snapshot.applied_buttons[0]),
                            static_cast<jint>(snapshot.applied_buttons[1])};
    jintArray result = env->NewIntArray(11);
    if (result != nullptr) env->SetIntArrayRegion(result, 0, 11, values);
    return result;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_flynes_emu_NearbyMvpSession_nativeSessionId(JNIEnv* env, jclass, jlong handle) {
    fly_lan_mvp_snapshot snapshot{};
    if (handle == 0 || !fly_lan_mvp_snapshot_read(session_from(handle), &snapshot) ||
        (snapshot.state != FLY_LAN_MVP_LOBBY &&
         snapshot.state != FLY_LAN_MVP_CONFIGURING &&
         snapshot.state != FLY_LAN_MVP_RUNNING)) return nullptr;
    jbyteArray result = env->NewByteArray(16);
    if (result != nullptr) {
        env->SetByteArrayRegion(result, 0, 16,
                reinterpret_cast<const jbyte*>(snapshot.session_id));
    }
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_flynes_emu_NearbyMvpSession_nativeSelectRom(
        JNIEnv* env, jclass, jlong handle, jbyteArray rom) {
    if (handle == 0 || rom == nullptr) return JNI_FALSE;
    const jsize size = env->GetArrayLength(rom);
    if (size <= 0) return JNI_FALSE;
    jbyte* bytes = env->GetByteArrayElements(rom, nullptr);
    if (bytes == nullptr) return JNI_FALSE;
    const int selected = fly_lan_mvp_select_rom(session_from(handle),
        reinterpret_cast<const std::uint8_t*>(bytes), static_cast<std::size_t>(size));
    env->ReleaseByteArrayElements(rom, bytes, JNI_ABORT);
    return selected == 1 ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_flynes_emu_NearbyMvpSession_nativeConfirm(JNIEnv*, jclass, jlong handle) {
    return handle != 0 && fly_lan_mvp_confirm(session_from(handle)) == 1 ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_flynes_emu_NearbyMvpSession_nativeSubmitInput(
        JNIEnv*, jclass, jlong handle, jint buttons) {
    return handle != 0 && fly_lan_mvp_submit_input(
        session_from(handle), static_cast<std::uint32_t>(buttons)) == 1 ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_flynes_emu_NearbyMvpSession_nativeSelectGame(JNIEnv* env, jclass, jlong handle, jbyteArray rom, jstring key) {
    if (!handle || !rom || !key) return JNI_FALSE;
    jbyte* bytes = env->GetByteArrayElements(rom, nullptr);
    const char* game_key = env->GetStringUTFChars(key, nullptr);
    const int selected = bytes && game_key ? fly_lan_mvp_select_game(session_from(handle),
        reinterpret_cast<const std::uint8_t*>(bytes), env->GetArrayLength(rom), game_key) : 0;
    if (bytes) env->ReleaseByteArrayElements(rom, bytes, JNI_ABORT);
    if (game_key) env->ReleaseStringUTFChars(key, game_key);
    return selected ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_flynes_emu_NearbyMvpSession_nativeSetPaused(JNIEnv*, jclass, jlong handle, jboolean paused) {
    return handle && fly_lan_mvp_set_paused(session_from(handle), paused) ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_flynes_emu_NearbyMvpSession_nativeReturnLobby(JNIEnv*, jclass, jlong handle) {
    return handle && fly_lan_mvp_return_lobby(session_from(handle)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_flynes_emu_NearbyMvpSession_nativeCompletedFrames(JNIEnv*, jclass, jlong handle) {
    fly_lan_mvp_snapshot snapshot{};
    return handle != 0 && fly_lan_mvp_snapshot_read(session_from(handle), &snapshot) == 1
        ? static_cast<jlong>(snapshot.completed_frames) : 0;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_flynes_emu_NearbyMvpSession_nativeCopyLatestFrame(
        JNIEnv* env, jclass, jlong handle, jbyteArray output) {
    if (handle == 0 || output == nullptr ||
        env->GetArrayLength(output) < static_cast<jsize>(FLY_RUNTIME_RGB565_BYTES)) return -1;
    jbyte* bytes = env->GetByteArrayElements(output, nullptr);
    if (bytes == nullptr) return -1;
    fly_latest_frame_v1 meta{};
    meta.struct_size = FLY_LATEST_FRAME_V1_SIZE;
    meta.version = FLY_LATEST_FRAME_VERSION_1;
    const int copied = fly_lan_mvp_copy_latest_frame(session_from(handle), bytes,
        FLY_RUNTIME_RGB565_BYTES, &meta);
    env->ReleaseByteArrayElements(output, bytes, copied == 1 ? 0 : JNI_ABORT);
    return copied == 1 ? static_cast<jlong>(meta.frame_index) : -1;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_flynes_emu_NearbyMvpSession_nativePullPcm(
        JNIEnv* env, jclass, jlong handle, jshortArray output) {
    if (handle == 0 || output == nullptr) return 0;
    const jsize capacity = env->GetArrayLength(output);
    if (capacity <= 0) return 0;
    jshort* samples = env->GetShortArrayElements(output, nullptr);
    if (samples == nullptr) return 0;
    fly_pcm_block_v1 block{};
    block.struct_size = FLY_PCM_BLOCK_V1_SIZE;
    block.version = FLY_PCM_BLOCK_VERSION_1;
    const int pulled = fly_lan_mvp_pull_pcm(session_from(handle),
        reinterpret_cast<std::int16_t*>(samples), static_cast<std::uint32_t>(capacity), &block);
    env->ReleaseShortArrayElements(output, samples, pulled == 1 ? 0 : JNI_ABORT);
    return pulled == 1 ? static_cast<jint>(block.sample_count) : 0;
}

extern "C" JNIEXPORT void JNICALL
Java_com_flynes_emu_NearbyMvpSession_nativeDestroy(JNIEnv*, jclass, jlong handle) {
    if (handle != 0) fly_lan_mvp_destroy(session_from(handle));
}
