// JNI bridge: com.flynes.emu.NesCore <-> core C ABI (core/include/nes/nes.h).
//
// All native methods are static; the emulator instance is carried as a jlong
// handle. Emulation is driven from Java (AudioThread = audio-master clock),
// rendering from the UI thread (Choreographer -> nativeBlit).
//
// Threading model:
//   - nes_run_frames / nes_save_state / nes_load_state run on the audio thread
//     (save/load only happen while the audio loop is stopped).
//   - nes_set_input is lock-free (atomic store in the core), safe from the UI
//     thread while the audio thread runs frames.
//   - nativeBlit reads the core framebuffer concurrently with nes_run_frames;
//     the pointer is stable for the lifetime of the core (only contents change),
//     so worst case is visual tearing, never a crash.
#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <cstdint>
#include <cstring>

#include "nes/nes.h"

namespace {

// Nearest-neighbor RGB565 scale: src (pitch bytes/row) -> dst (stride pixels/row).
void blit_scale_rgb565(const uint8_t* src, int32_t src_pitch,
                       int32_t src_w, int32_t src_h,
                       uint8_t* dst, int32_t dst_stride, int scale)
{
    for (int32_t y = 0; y < src_h; ++y) {
        const uint16_t* src_row = reinterpret_cast<const uint16_t*>(src + static_cast<int64_t>(y) * src_pitch);
        // dst_stride is in pixels; RGB565 = 2 bytes per pixel.
        uint16_t* dst_row = reinterpret_cast<uint16_t*>(dst + static_cast<int64_t>(y) * scale * dst_stride * 2);
        for (int32_t x = 0; x < src_w; ++x) {
            const uint16_t px = src_row[x];
            for (int dy = 0; dy < scale; ++dy) {
                uint16_t* out = dst_row + static_cast<int64_t>(dy) * dst_stride + x * scale;
                for (int dx = 0; dx < scale; ++dx)
                    out[dx] = px;
            }
        }
    }
}

} // namespace

extern "C" {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

JNIEXPORT jlong JNICALL
Java_com_flynes_emu_NesCore_nativeCreate(JNIEnv*, jclass)
{
    // NULL cfg -> defaults: NTSC / 48000 Hz / RGB565.
    return reinterpret_cast<jlong>(nes_create(nullptr));
}

JNIEXPORT void JNICALL
Java_com_flynes_emu_NesCore_nativeDestroy(JNIEnv*, jclass, jlong handle)
{
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    if (ctx)
        nes_destroy(ctx);
}

// ---------------------------------------------------------------------------
// ROM loading (patch is rejected: nes_load_rom_patched is NOT_IMPLEMENTED)
// ---------------------------------------------------------------------------

JNIEXPORT jint JNICALL
Java_com_flynes_emu_NesCore_nativeLoadRom(JNIEnv* env, jclass, jlong handle,
                                          jbyteArray rom, jbyteArray patch)
{
    if (patch != nullptr)
        return static_cast<jint>(NES_ERR_NOT_IMPLEMENTED);
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    if (!ctx)
        return static_cast<jint>(NES_ERR_INVALID_PARAM);
    if (rom == nullptr)
        return static_cast<jint>(NES_ERR_INVALID_PARAM);

    const jsize len = env->GetArrayLength(rom);
    jbyte* bytes = env->GetByteArrayElements(rom, nullptr);
    if (!bytes)
        return static_cast<jint>(NES_ERR_OUT_OF_MEMORY);

    nes_rom_info info;
    std::memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.version = NES_STRUCT_VERSION;

    // 0/positive = success (warnings are positive); negative = failure.
    const int rc = nes_load_rom(ctx, reinterpret_cast<const uint8_t*>(bytes),
                                static_cast<size_t>(len), &info);
    env->ReleaseByteArrayElements(rom, bytes, JNI_ABORT);
    return static_cast<jint>(rc);
}

// ---------------------------------------------------------------------------
// Database (NstDatabase.xml is bundled as an asset; load before any ROM)
// ---------------------------------------------------------------------------

JNIEXPORT jint JNICALL
Java_com_flynes_emu_NesCore_nativeLoadDatabase(JNIEnv* env, jclass, jlong handle,
                                               jbyteArray xml)
{
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    if (!ctx)
        return static_cast<jint>(NES_ERR_INVALID_PARAM);
    if (xml == nullptr)
        return static_cast<jint>(NES_ERR_INVALID_PARAM);

    const jsize len = env->GetArrayLength(xml);
    jbyte* bytes = env->GetByteArrayElements(xml, nullptr);
    if (!bytes)
        return static_cast<jint>(NES_ERR_OUT_OF_MEMORY);

    const int rc = nes_load_database(ctx, reinterpret_cast<const uint8_t*>(bytes),
                                     static_cast<size_t>(len));
    env->ReleaseByteArrayElements(xml, bytes, JNI_ABORT);
    return static_cast<jint>(rc);
}

// ---------------------------------------------------------------------------
// Frame loop (audio-master clock: the Java side paces via AudioTrack.write)
// ---------------------------------------------------------------------------

JNIEXPORT jint JNICALL
Java_com_flynes_emu_NesCore_nativeRunFrames(JNIEnv* env, jclass, jlong handle,
                                            jint maxFrames, jobject audioBuf,
                                            jint capSamples)
{
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    if (!ctx)
        return static_cast<jint>(NES_ERR_INVALID_PARAM);
    // Null/address-less buffer: nothing can be written — return 0 (idle) rather
    // than an error so the audio loop treats it as "no output this round".
    if (audioBuf == nullptr)
        return 0;

    int16_t* dst = reinterpret_cast<int16_t*>(env->GetDirectBufferAddress(audioBuf));
    if (!dst)
        return 0;

    // GetDirectBufferCapacity returns BYTES; capSamples is in int16 samples, so
    // clamp against capacity/2 (clamping against raw bytes would allow 2x the
    // real buffer and overflow it).
    const jlong capacityBytes = env->GetDirectBufferCapacity(audioBuf);
    if (capacityBytes <= 0 || capSamples <= 0)
        return 0; // zero/unknown capacity or non-positive cap: write nothing
    const jlong capacitySamples = capacityBytes / 2;
    if (static_cast<jlong>(capSamples) > capacitySamples)
        capSamples = static_cast<jint>(capacitySamples);

    uint32_t frames_run = 0;
    uint32_t samples_written = 0;
    const int rc = nes_run_frames(ctx, static_cast<uint32_t>(maxFrames > 0 ? maxFrames : 0),
                                  dst, static_cast<uint32_t>(capSamples),
                                  &frames_run, &samples_written);
    if (rc < 0)
        return static_cast<jint>(rc);
    // Success: return samples written so the Java audio loop knows how much to play.
    return static_cast<jint>(samples_written);
}

// ---------------------------------------------------------------------------
// Input / audio format
// ---------------------------------------------------------------------------

JNIEXPORT void JNICALL
Java_com_flynes_emu_NesCore_nativeSetInput(JNIEnv*, jclass, jlong handle, jint buttons)
{
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    if (ctx)
        nes_set_input(ctx, 0, static_cast<uint32_t>(buttons));
}

JNIEXPORT void JNICALL
Java_com_flynes_emu_NesCore_nativeSetAudioFormat(JNIEnv*, jclass, jlong handle,
                                                 jint rate, jint stereo)
{
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    if (ctx)
        nes_set_audio_format(ctx, static_cast<uint32_t>(rate), stereo);
}

JNIEXPORT void JNICALL
Java_com_flynes_emu_NesCore_nativeSetVideoFilter(JNIEnv*, jclass, jlong handle, jint filter)
{
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    if (ctx)
        nes_set_video_format(ctx, NES_PIXFMT_RGB565, static_cast<nes_video_filter>(filter));
}

// ---------------------------------------------------------------------------
// Save / load state
// ---------------------------------------------------------------------------

JNIEXPORT jint JNICALL
Java_com_flynes_emu_NesCore_nativeSaveState(JNIEnv* env, jclass, jlong handle, jbyteArray out)
{
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    if (!ctx)
        return static_cast<jint>(NES_ERR_INVALID_PARAM);
    if (out == nullptr)
        return static_cast<jint>(NES_ERR_INVALID_PARAM);

    const jsize cap = env->GetArrayLength(out);
    jbyte* bytes = env->GetByteArrayElements(out, nullptr);
    if (!bytes)
        return static_cast<jint>(NES_ERR_OUT_OF_MEMORY);

    size_t written = 0;
    size_t needed = 0;
    const int rc = nes_save_state(ctx, reinterpret_cast<uint8_t*>(bytes),
                                  static_cast<size_t>(cap), &written, &needed);
    // Copy the native-written prefix back into the Java array.
    env->ReleaseByteArrayElements(out, bytes, 0);
    if (rc < 0)
        return static_cast<jint>(rc);
    return static_cast<jint>(written);
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_NesCore_nativeLoadState(JNIEnv* env, jclass, jlong handle, jbyteArray in)
{
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    if (!ctx)
        return static_cast<jint>(NES_ERR_INVALID_PARAM);
    if (in == nullptr)
        return static_cast<jint>(NES_ERR_INVALID_PARAM);

    const jsize len = env->GetArrayLength(in);
    jbyte* bytes = env->GetByteArrayElements(in, nullptr);
    if (!bytes)
        return static_cast<jint>(NES_ERR_OUT_OF_MEMORY);

    const int rc = nes_load_state(ctx, reinterpret_cast<const uint8_t*>(bytes),
                                  static_cast<size_t>(len));
    env->ReleaseByteArrayElements(in, bytes, JNI_ABORT);
    return static_cast<jint>(rc);
}

// ---------------------------------------------------------------------------
// Rendering: blit the RGB565 framebuffer into an ANativeWindow, scaled.
// ---------------------------------------------------------------------------

JNIEXPORT void JNICALL
Java_com_flynes_emu_NesCore_nativeBlit(JNIEnv* env, jclass, jlong handle,
                                       jobject surface, jint scale)
{
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    if (!ctx || surface == nullptr || scale < 1)
        return;

    const nes_video_frame* frame = nes_get_video_frame(ctx);
    if (!frame || !frame->pixels || frame->format != NES_PIXFMT_RGB565)
        return;

    ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
    if (!win)
        return;

    const int32_t src_w = static_cast<int32_t>(frame->width);
    const int32_t src_h = static_cast<int32_t>(frame->height);
    const int32_t dst_w = src_w * scale;
    const int32_t dst_h = src_h * scale;

    ANativeWindow_setBuffersGeometry(win, dst_w, dst_h, WINDOW_FORMAT_RGB_565);

    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(win, &buf, nullptr) == 0) {
        // buf.stride is in pixels; negative stride (bottom-up) is not handled
        // in phase 0 (not produced by Android RGB565 surfaces in practice).
        if (buf.bits != nullptr && buf.stride >= dst_w) {
            blit_scale_rgb565(static_cast<const uint8_t*>(frame->pixels), frame->pitch,
                              src_w, src_h,
                              static_cast<uint8_t*>(buf.bits), buf.stride, scale);
        }
        ANativeWindow_unlockAndPost(win);
    }
    ANativeWindow_release(win);
}

} // extern "C"
