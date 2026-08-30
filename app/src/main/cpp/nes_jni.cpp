// JNI bridge: com.flynes.emu.NesCore <-> core C ABI (core/include/nes/nes.h).
//
// All native methods are static; the emulator instance is carried as a jlong
// handle. Emulation is driven from Java (AudioThread = audio-master clock),
// rendering through sequenced snapshots consumed by the OpenGL presenter.
//
// Threading model:
//   - nes_run_frames / nes_save_state / nes_load_state run on the audio thread
//     (save/load only happen while the audio loop is stopped).
//   - nes_set_input is lock-free (atomic store in the core), safe from the UI
//     thread while the audio thread runs frames.
//   - nes_copy_video_frame locks the published buffer while copying, so the GL
//     thread never observes a partially written frame.
#include <jni.h>
#include <cstdint>
#include <cstring>
#include <limits>
#include <chrono>

#include "nes/nes.h"

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

JNIEXPORT jobjectArray JNICALL
Java_com_flynes_emu_NesCore_nativeRomInfoStrings(JNIEnv* env, jclass, jlong handle)
{
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    if (!ctx) return nullptr;
    nes_rom_info info{};
    info.struct_size = sizeof(info);
    info.version = NES_STRUCT_VERSION;
    if (nes_get_rom_info(ctx, &info) < 0) return nullptr;

    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray result = env->NewObjectArray(6, stringClass, nullptr);
    const char* values[] = {info.title, info.publisher, info.developer,
                            info.region, info.sha1, info.crc32};
    for (jsize i = 0; i < 6; ++i) {
        jstring value = env->NewStringUTF(values[i]);
        env->SetObjectArrayElement(result, i, value);
        env->DeleteLocalRef(value);
    }
    env->DeleteLocalRef(stringClass);
    return result;
}

JNIEXPORT jintArray JNICALL
Java_com_flynes_emu_NesCore_nativeRomInfoNumbers(JNIEnv* env, jclass, jlong handle)
{
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    if (!ctx) return nullptr;
    nes_rom_info info{};
    info.struct_size = sizeof(info);
    info.version = NES_STRUCT_VERSION;
    if (nes_get_rom_info(ctx, &info) < 0) return nullptr;
    const jint values[] = {
        static_cast<jint>(info.mapper), static_cast<jint>(info.submapper),
        static_cast<jint>(info.prg_size), static_cast<jint>(info.chr_size),
        static_cast<jint>(info.wram_size), static_cast<jint>(info.vram_size),
        static_cast<jint>(info.has_battery), static_cast<jint>(info.system),
        static_cast<jint>(info.cpu), static_cast<jint>(info.ppu),
        static_cast<jint>(info.region_ntsc), static_cast<jint>(info.patched),
        static_cast<jint>(info.players)
    };
    jintArray result = env->NewIntArray(13);
    if (result) env->SetIntArrayRegion(result, 0, 13, values);
    return result;
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

JNIEXPORT jint JNICALL
Java_com_flynes_emu_NesCore_nativeCopyVideoFrameIfNew(JNIEnv* env, jclass, jlong handle,
                                                       jlong lastSequence,
                                                       jobject destination,
                                                       jintArray metadata,
                                                       jlongArray timing)
{
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    if (!ctx || !destination || !metadata || !timing
            || env->GetArrayLength(metadata) < 5 || env->GetArrayLength(timing) < 3)
        return static_cast<jint>(NES_ERR_INVALID_PARAM);

    void* pixels = env->GetDirectBufferAddress(destination);
    const jlong capacity = env->GetDirectBufferCapacity(destination);
    if (!pixels || capacity <= 0)
        return static_cast<jint>(NES_ERR_INVALID_PARAM);

    nes_video_snapshot snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    snapshot.version = NES_STRUCT_VERSION;
    const int rc = nes_copy_video_frame_if_new(ctx, static_cast<uint64_t>(lastSequence),
                                               pixels, static_cast<size_t>(capacity), &snapshot);
    if (rc != NES_OK)
        return static_cast<jint>(rc);
    if (snapshot.bytes_written > static_cast<size_t>(std::numeric_limits<jint>::max()))
        return static_cast<jint>(NES_ERR_BUFFER_TOO_SMALL);

    const jint values[] = {
        static_cast<jint>(snapshot.width),
        static_cast<jint>(snapshot.height),
        static_cast<jint>(snapshot.pitch),
        static_cast<jint>(snapshot.format),
        static_cast<jint>(snapshot.bytes_written)
    };
    env->SetIntArrayRegion(metadata, 0, 5, values);
    const jlong timing_values[] = {
        static_cast<jlong>(snapshot.sequence),
        static_cast<jlong>(snapshot.native_monotonic_ns),
        static_cast<jlong>(snapshot.source_region)
    };
    env->SetLongArrayRegion(timing, 0, 3, timing_values);
    return static_cast<jint>(NES_OK);
}

JNIEXPORT jlong JNICALL
Java_com_flynes_emu_NesCore_nativeMonotonicNow(JNIEnv*, jclass)
{
    return static_cast<jlong>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_NesCore_nativeRunFrameStep(JNIEnv* env, jclass, jlong handle,
                                                jobject audioBuf, jint capSamples,
                                                jlongArray step)
{
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    if (!ctx || !audioBuf || !step || env->GetArrayLength(step) < 4)
        return static_cast<jint>(NES_ERR_INVALID_PARAM);
    int16_t* dst = reinterpret_cast<int16_t*>(env->GetDirectBufferAddress(audioBuf));
    const jlong capacityBytes = env->GetDirectBufferCapacity(audioBuf);
    if (!dst || capacityBytes <= 0 || capSamples <= 0)
        return static_cast<jint>(NES_ERR_INVALID_PARAM);
    const jlong capacitySamples = capacityBytes / 2;
    if (static_cast<jlong>(capSamples) > capacitySamples)
        capSamples = static_cast<jint>(capacitySamples);

    nes_frame_step_result result{};
    result.struct_size = sizeof(result);
    result.version = NES_STRUCT_VERSION;
    const int rc = nes_run_frame_step(ctx, dst, static_cast<uint32_t>(capSamples), &result);
    if (rc < 0) return static_cast<jint>(rc);
    const jlong values[] = {
        static_cast<jlong>(result.frames_run),
        static_cast<jlong>(result.audio_samples),
        static_cast<jlong>(result.video_sequence),
        static_cast<jlong>(result.source_region)
    };
    env->SetLongArrayRegion(step, 0, 4, values);
    return static_cast<jint>(NES_OK);
}

JNIEXPORT jlong JNICALL
Java_com_flynes_emu_NesCore_nativeSetInput(JNIEnv*, jclass, jlong handle, jint buttons)
{
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    return ctx ? static_cast<jlong>(nes_set_input_versioned(
            ctx, 0, static_cast<uint32_t>(buttons))) : 0;
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_NesCore_nativeGetLastInputSample(JNIEnv* env, jclass, jlong handle,
                                                      jlongArray values)
{
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    if (!ctx || !values || env->GetArrayLength(values) < 6)
        return static_cast<jint>(NES_ERR_INVALID_PARAM);
    nes_input_sample sample{};
    sample.struct_size = sizeof(sample);
    sample.version = NES_STRUCT_VERSION;
    const int rc = nes_get_last_input_sample(ctx, &sample);
    if (rc < 0) return static_cast<jint>(rc);
    const jlong result[] = {
        static_cast<jlong>(sample.generation),
        static_cast<jlong>(sample.pad_bits[0]),
        static_cast<jlong>(sample.pad_bits[1]),
        static_cast<jlong>(sample.pad_bits[2]),
        static_cast<jlong>(sample.pad_bits[3]),
        static_cast<jlong>(sample.native_monotonic_ns)
    };
    env->SetLongArrayRegion(values, 0, 6, result);
    return static_cast<jint>(NES_OK);
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

} // extern "C"
