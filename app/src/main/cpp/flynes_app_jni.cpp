// JNI bridge: com.flynes.emu.app.FlyNesApp <-> shared flynes_app C ABI.
// Scan FDs are borrowed: this file never closes or stores the caller's descriptor.
#include <jni.h>

#include <flynes/flynes_app.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string copy_bytes(JNIEnv* env, jbyteArray array)
{
    if (array == nullptr)
    {
        return {};
    }
    const jsize length = env->GetArrayLength(array);
    if (length < 0)
    {
        return {};
    }
    std::string bytes(static_cast<size_t>(length), '\0');
    if (length > 0)
    {
        env->GetByteArrayRegion(array, 0, length, reinterpret_cast<jbyte*>(bytes.data()));
    }
    return bytes;
}

void copy_uuid(JNIEnv* env, jbyteArray array, uint8_t out[16])
{
    std::memset(out, 0, 16);
    if (array == nullptr || env->GetArrayLength(array) != 16)
    {
        return;
    }
    env->GetByteArrayRegion(array, 0, 16, reinterpret_cast<jbyte*>(out));
}

jstring utf8_string(JNIEnv* env, const char* bytes)
{
    if (bytes == nullptr)
    {
        bytes = "";
    }
    const jsize length = static_cast<jsize>(std::strlen(bytes));
    jbyteArray array = env->NewByteArray(length);
    if (array == nullptr)
    {
        return nullptr;
    }
    if (length > 0)
    {
        env->SetByteArrayRegion(array, 0, length, reinterpret_cast<const jbyte*>(bytes));
    }
    jclass stringClass = env->FindClass("java/lang/String");
    jmethodID ctor = env->GetMethodID(stringClass, "<init>", "([BLjava/lang/String;)V");
    jstring charset = env->NewStringUTF("UTF-8");
    jstring result = static_cast<jstring>(env->NewObject(stringClass, ctor, array, charset));
    env->DeleteLocalRef(array);
    env->DeleteLocalRef(charset);
    env->DeleteLocalRef(stringClass);
    return result;
}

fly_app_t* app_from(jlong handle)
{
    return reinterpret_cast<fly_app_t*>(handle);
}

fly_scan_t* scan_from(jlong handle)
{
    return reinterpret_cast<fly_scan_t*>(handle);
}

} // namespace

extern "C" {

JNIEXPORT jint JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeCreate(
    JNIEnv* env, jclass, jbyteArray dataRoot, jbyteArray cacheRoot, jlongArray outHandle)
{
    if (outHandle == nullptr || env->GetArrayLength(outHandle) < 1)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    const jlong zero = 0;
    env->SetLongArrayRegion(outHandle, 0, 1, &zero);
    const std::string data = copy_bytes(env, dataRoot);
    const std::string cache = copy_bytes(env, cacheRoot);
    fly_platform_capabilities capabilities{};
    capabilities.struct_size = FLY_PLATFORM_CAPABILITIES_V1_SIZE;
    capabilities.version = FLY_PLATFORM_CAPABILITIES_VERSION_1;
    fly_app_config config{};
    config.struct_size = FLY_APP_CONFIG_V1_SIZE;
    config.version = FLY_APP_CONFIG_VERSION_1;
    config.data_root_utf8 = data.data();
    config.cache_root_utf8 = cache.data();
    config.platform_capabilities = &capabilities;
    config.data_root_utf8_length = static_cast<uint32_t>(data.size());
    config.cache_root_utf8_length = static_cast<uint32_t>(cache.size());
    fly_app_t* app = nullptr;
    const fly_result result = fly_app_create(&config, &app);
    if (result == FLY_RESULT_OK && app != nullptr)
    {
        const jlong handle = reinterpret_cast<jlong>(app);
        env->SetLongArrayRegion(outHandle, 0, 1, &handle);
    }
    return result;
}

JNIEXPORT void JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeDestroy(JNIEnv*, jclass, jlong handle)
{
    fly_app_destroy(app_from(handle));
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeScanBegin(
    JNIEnv* env, jclass, jlong app, jbyteArray uuid, jint scope, jlongArray outScan)
{
    if (outScan == nullptr || env->GetArrayLength(outScan) < 1)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    const jlong zero = 0;
    env->SetLongArrayRegion(outScan, 0, 1, &zero);
    fly_scan_config config{};
    config.struct_size = FLY_SCAN_CONFIG_V1_SIZE;
    config.version = FLY_SCAN_CONFIG_VERSION_1;
    copy_uuid(env, uuid, config.source_uuid);
    config.source_scope = static_cast<uint32_t>(scope);
    fly_scan_t* scan = nullptr;
    const fly_result result = fly_scan_begin(app_from(app), &config, &scan);
    if (result == FLY_RESULT_OK && scan != nullptr)
    {
        const jlong handle = reinterpret_cast<jlong>(scan);
        env->SetLongArrayRegion(outScan, 0, 1, &handle);
    }
    return result;
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeScanAddFile(
    JNIEnv* env, jclass, jlong scan, jbyteArray relativePath, jbyteArray displayName,
    jint borrowedFd, jint flags, jbyteArray expectedSha, jintArray outcomeOut)
{
    const std::string path = copy_bytes(env, relativePath);
    const std::string name = copy_bytes(env, displayName);
    fly_scan_file file{};
    file.struct_size = FLY_SCAN_FILE_V1_SIZE;
    file.version = FLY_SCAN_FILE_VERSION_1;
    file.source_relative_path_utf8 = path.data();
    file.display_name_utf8 = name.data();
    file.source_relative_path_utf8_length = static_cast<uint32_t>(path.size());
    file.display_name_utf8_length = static_cast<uint32_t>(name.size());
    file.borrowed_fd = borrowedFd;
    file.flags = static_cast<uint32_t>(flags);
    if (expectedSha != nullptr && env->GetArrayLength(expectedSha) == 32)
    {
        env->GetByteArrayRegion(
            expectedSha, 0, 32, reinterpret_cast<jbyte*>(file.expected_physical_sha256));
    }
    fly_scan_file_result result{};
    result.struct_size = FLY_SCAN_FILE_RESULT_V1_SIZE;
    result.version = FLY_SCAN_FILE_RESULT_VERSION_1;
    const fly_result code = fly_scan_add_file(scan_from(scan), &file, &result);
    if (outcomeOut != nullptr && env->GetArrayLength(outcomeOut) >= 3)
    {
        const jint values[] = {
            static_cast<jint>(result.outcome),
            static_cast<jint>(result.reason),
            static_cast<jint>(result.variant_count),
        };
        env->SetIntArrayRegion(outcomeOut, 0, 3, values);
    }
    return code;
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeScanCommit(JNIEnv*, jclass, jlong scan, jint completeness)
{
    return fly_scan_commit(scan_from(scan), static_cast<uint32_t>(completeness));
}

JNIEXPORT void JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeScanAbort(JNIEnv*, jclass, jlong scan)
{
    fly_scan_abort(scan_from(scan));
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeFavoriteSet(
    JNIEnv* env, jclass, jlong app, jbyteArray canonicalId, jint favorite)
{
    const std::string id = copy_bytes(env, canonicalId);
    return fly_catalog_favorite_set(
        app_from(app), id.data(), static_cast<uint32_t>(id.size()),
        static_cast<uint32_t>(favorite));
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeMarkPlayed(
    JNIEnv* env, jclass, jlong app, jbyteArray canonicalId)
{
    const std::string id = copy_bytes(env, canonicalId);
    return fly_catalog_mark_played(
        app_from(app), id.data(), static_cast<uint32_t>(id.size()));
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeUserStateGet(
    JNIEnv* env, jclass, jlong app, jbyteArray canonicalId, jlongArray out)
{
    if (out == nullptr || env->GetArrayLength(out) < 4)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    const std::string id = copy_bytes(env, canonicalId);
    fly_catalog_user_state state{};
    state.struct_size = FLY_CATALOG_USER_STATE_V1_SIZE;
    state.version = FLY_CATALOG_USER_STATE_VERSION_1;
    const fly_result result = fly_catalog_user_state_get(
        app_from(app), id.data(), static_cast<uint32_t>(id.size()), &state);
    if (result == FLY_RESULT_OK)
    {
        const jlong values[] = {
            static_cast<jlong>(state.favorite),
            static_cast<jlong>(state.play_count),
            static_cast<jlong>(state.favorite_revision),
            static_cast<jlong>(state.last_played_sequence),
        };
        env->SetLongArrayRegion(out, 0, 4, values);
    }
    return result;
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeSettingsGet(
    JNIEnv* env, jclass, jlong app, jintArray ints, jfloatArray floats,
    jbyteArray locale, jbyteArray lastPlayed, jintArray lengths)
{
    if (ints == nullptr || floats == nullptr || locale == nullptr || lastPlayed == nullptr
        || lengths == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    std::vector<char> localeBuf(static_cast<size_t>(env->GetArrayLength(locale)) + 1u, '\0');
    std::vector<char> lastBuf(static_cast<size_t>(env->GetArrayLength(lastPlayed)) + 1u, '\0');
    fly_settings_snapshot snapshot{};
    snapshot.struct_size = FLY_SETTINGS_SNAPSHOT_V1_SIZE;
    snapshot.version = FLY_SETTINGS_SNAPSHOT_VERSION_1;
    snapshot.locale_tag_utf8 = localeBuf.data();
    snapshot.locale_tag_capacity = static_cast<uint32_t>(localeBuf.size());
    snapshot.last_played_id_utf8 = lastBuf.data();
    snapshot.last_played_id_capacity = static_cast<uint32_t>(lastBuf.size());
    const fly_result result = fly_settings_get(app_from(app), &snapshot);
    if (result != FLY_RESULT_OK)
    {
        return result;
    }
    const jint intValues[] = {
        static_cast<jint>(snapshot.aspect_mode),
        static_cast<jint>(snapshot.video_quality_preset),
        static_cast<jint>(snapshot.custom_refresh_policy),
        static_cast<jint>(snapshot.custom_temporal_mode),
        static_cast<jint>(snapshot.custom_spatial_mode),
        static_cast<jint>(snapshot.custom_post_effect),
        static_cast<jint>(snapshot.adaptive_protection),
        static_cast<jint>(snapshot.layout_preset),
        static_cast<jint>(snapshot.direction_mode),
        static_cast<jint>(snapshot.haptic_level),
        static_cast<jint>(snapshot.distinct_ab_haptics),
        static_cast<jint>(snapshot.audio_enabled),
        static_cast<jint>(snapshot.audio_focus_policy),
        static_cast<jint>(snapshot.autosave_enabled),
    };
    const jfloat floatValues[] = {
        snapshot.button_scale,
        snapshot.vertical_offset,
        snapshot.control_opacity,
        snapshot.joystick_scale,
        snapshot.dead_zone,
    };
    env->SetIntArrayRegion(ints, 0, 14, intValues);
    env->SetFloatArrayRegion(floats, 0, 5, floatValues);
    const jint required[] = {
        static_cast<jint>(snapshot.locale_tag_required),
        static_cast<jint>(snapshot.last_played_id_required),
    };
    env->SetIntArrayRegion(lengths, 0, 2, required);
    const jsize localeCopy = static_cast<jsize>(
        std::min(localeBuf.size(), static_cast<size_t>(env->GetArrayLength(locale))));
    env->SetByteArrayRegion(locale, 0, localeCopy, reinterpret_cast<const jbyte*>(localeBuf.data()));
    const jsize lastCopy = static_cast<jsize>(
        std::min(lastBuf.size(), static_cast<size_t>(env->GetArrayLength(lastPlayed))));
    env->SetByteArrayRegion(
        lastPlayed, 0, lastCopy, reinterpret_cast<const jbyte*>(lastBuf.data()));
    return FLY_RESULT_OK;
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeSettingsApply(
    JNIEnv* env, jclass, jlong app, jintArray ints, jfloatArray floats,
    jbyteArray locale, jbyteArray lastPlayed)
{
    if (ints == nullptr || floats == nullptr || locale == nullptr || lastPlayed == nullptr
        || env->GetArrayLength(ints) < 14 || env->GetArrayLength(floats) < 5)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    jint intValues[14];
    jfloat floatValues[5];
    env->GetIntArrayRegion(ints, 0, 14, intValues);
    env->GetFloatArrayRegion(floats, 0, 5, floatValues);
    std::string localeText = copy_bytes(env, locale);
    std::string lastText = copy_bytes(env, lastPlayed);
    fly_settings_snapshot snapshot{};
    snapshot.struct_size = FLY_SETTINGS_SNAPSHOT_V1_SIZE;
    snapshot.version = FLY_SETTINGS_SNAPSHOT_VERSION_1;
    snapshot.aspect_mode = static_cast<uint32_t>(intValues[0]);
    snapshot.video_quality_preset = static_cast<uint32_t>(intValues[1]);
    snapshot.custom_refresh_policy = static_cast<uint32_t>(intValues[2]);
    snapshot.custom_temporal_mode = static_cast<uint32_t>(intValues[3]);
    snapshot.custom_spatial_mode = static_cast<uint32_t>(intValues[4]);
    snapshot.custom_post_effect = static_cast<uint32_t>(intValues[5]);
    snapshot.adaptive_protection = static_cast<uint32_t>(intValues[6]);
    snapshot.layout_preset = static_cast<uint32_t>(intValues[7]);
    snapshot.direction_mode = static_cast<uint32_t>(intValues[8]);
    snapshot.haptic_level = static_cast<uint32_t>(intValues[9]);
    snapshot.distinct_ab_haptics = static_cast<uint32_t>(intValues[10]);
    snapshot.audio_enabled = static_cast<uint32_t>(intValues[11]);
    snapshot.audio_focus_policy = static_cast<uint32_t>(intValues[12]);
    snapshot.autosave_enabled = static_cast<uint32_t>(intValues[13]);
    snapshot.button_scale = floatValues[0];
    snapshot.vertical_offset = floatValues[1];
    snapshot.control_opacity = floatValues[2];
    snapshot.joystick_scale = floatValues[3];
    snapshot.dead_zone = floatValues[4];
    snapshot.locale_tag_utf8 = localeText.data();
    snapshot.locale_tag_utf8_length = static_cast<uint32_t>(localeText.size());
    snapshot.last_played_id_utf8 = lastText.data();
    snapshot.last_played_id_utf8_length = static_cast<uint32_t>(lastText.size());
    return fly_settings_apply(app_from(app), &snapshot);
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeCatalogCapture(
    JNIEnv* env, jclass, jlong app, jlongArray outSnapshot)
{
    if (outSnapshot == nullptr || env->GetArrayLength(outSnapshot) < 1)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    const jlong zero = 0;
    env->SetLongArrayRegion(outSnapshot, 0, 1, &zero);
    fly_catalog_snapshot_t* snapshot = nullptr;
    const fly_result result = fly_catalog_snapshot(app_from(app), &snapshot);
    if (result == FLY_RESULT_OK && snapshot != nullptr)
    {
        const jlong handle = reinterpret_cast<jlong>(snapshot);
        env->SetLongArrayRegion(outSnapshot, 0, 1, &handle);
    }
    return result;
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeCatalogCount(
    JNIEnv* env, jclass, jlong snapshot, jlongArray out)
{
    if (out == nullptr || env->GetArrayLength(out) < 1)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    uint64_t count = 0;
    const fly_result result = fly_catalog_snapshot_count(
        reinterpret_cast<fly_catalog_snapshot_t*>(snapshot), &count);
    if (result == FLY_RESULT_OK)
    {
        const jlong value = static_cast<jlong>(count);
        env->SetLongArrayRegion(out, 0, 1, &value);
    }
    return result;
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeCatalogGeneration(
    JNIEnv* env, jclass, jlong snapshot, jlongArray out)
{
    if (out == nullptr || env->GetArrayLength(out) < 1)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    uint64_t generation = 0;
    const fly_result result = fly_catalog_snapshot_generation(
        reinterpret_cast<fly_catalog_snapshot_t*>(snapshot), &generation);
    if (result == FLY_RESULT_OK)
    {
        const jlong value = static_cast<jlong>(generation);
        env->SetLongArrayRegion(out, 0, 1, &value);
    }
    return result;
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeCatalogGet(
    JNIEnv* env, jclass, jlong snapshot, jlong index, jobjectArray uuidAndHashes,
    jlongArray sizes, jintArray enums, jobjectArray texts)
{
    if (uuidAndHashes == nullptr || sizes == nullptr || enums == nullptr || texts == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    std::array<char, FLY_CANONICAL_ID_MAX_UTF8_BYTES + 1u> canonical{};
    std::array<char, FLY_CANONICAL_ID_MAX_UTF8_BYTES + 1u> variant{};
    std::array<char, FLY_SCAN_DISPLAY_NAME_MAX_UTF8_BYTES + 1u> display{};
    std::array<char, FLY_SCAN_RELATIVE_PATH_MAX_UTF8_BYTES + 1u> path{};
    fly_catalog_entry entry{};
    entry.struct_size = FLY_CATALOG_ENTRY_V1_SIZE;
    entry.version = FLY_CATALOG_ENTRY_VERSION_1;
    entry.canonical_id_utf8 = canonical.data();
    entry.canonical_id_capacity = static_cast<uint32_t>(canonical.size());
    entry.variant_id_utf8 = variant.data();
    entry.variant_id_capacity = static_cast<uint32_t>(variant.size());
    entry.display_name_utf8 = display.data();
    entry.display_name_capacity = static_cast<uint32_t>(display.size());
    entry.source_relative_path_utf8 = path.data();
    entry.source_relative_path_capacity = static_cast<uint32_t>(path.size());
    const fly_result result = fly_catalog_snapshot_get(
        reinterpret_cast<fly_catalog_snapshot_t*>(snapshot),
        static_cast<uint64_t>(index), &entry);
    if (result != FLY_RESULT_OK)
    {
        return result;
    }
    auto put_bytes = [&](jsize slot, const uint8_t* data, jsize length) {
        jbyteArray array = env->NewByteArray(length);
        if (array != nullptr)
        {
            env->SetByteArrayRegion(array, 0, length, reinterpret_cast<const jbyte*>(data));
            env->SetObjectArrayElement(uuidAndHashes, slot, array);
            env->DeleteLocalRef(array);
        }
    };
    put_bytes(0, entry.source_uuid, 16);
    put_bytes(1, entry.payload_sha1, 20);
    put_bytes(2, entry.payload_sha256, 32);
    put_bytes(3, entry.physical_sha256, 32);
    put_bytes(4, entry.payload_crc32, 4);
    const jlong sizeValues[] = {
        static_cast<jlong>(entry.payload_size),
        static_cast<jlong>(entry.physical_size),
        static_cast<jlong>(entry.expected_bytes),
        static_cast<jlong>(entry.prg_bytes),
        static_cast<jlong>(entry.chr_bytes),
    };
    env->SetLongArrayRegion(sizes, 0, 5, sizeValues);
    const jint enumValues[] = {
        entry.mapper,
        entry.submapper,
        static_cast<jint>(entry.disk_sides),
        static_cast<jint>(entry.source_scope),
        static_cast<jint>(entry.package_format),
        static_cast<jint>(entry.rom_format),
        static_cast<jint>(entry.compatibility_state),
        static_cast<jint>(entry.compatibility_reason),
        static_cast<jint>(entry.freshness),
        static_cast<jint>(entry.flags),
    };
    env->SetIntArrayRegion(enums, 0, 10, enumValues);
    env->SetObjectArrayElement(texts, 0, utf8_string(env, canonical.data()));
    env->SetObjectArrayElement(texts, 1, utf8_string(env, variant.data()));
    env->SetObjectArrayElement(texts, 2, utf8_string(env, display.data()));
    env->SetObjectArrayElement(texts, 3, utf8_string(env, path.data()));
    return FLY_RESULT_OK;
}

JNIEXPORT void JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeCatalogRelease(JNIEnv*, jclass, jlong snapshot)
{
    fly_catalog_snapshot_release(reinterpret_cast<fly_catalog_snapshot_t*>(snapshot));
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeSourceCount(
    JNIEnv* env, jclass, jlong app, jlongArray out)
{
    if (out == nullptr || env->GetArrayLength(out) < 1)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    uint64_t count = 0;
    const fly_result result = fly_source_status_count(app_from(app), &count);
    if (result == FLY_RESULT_OK)
    {
        const jlong value = static_cast<jlong>(count);
        env->SetLongArrayRegion(out, 0, 1, &value);
    }
    return result;
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_app_FlyNesApp_nativeSourceGet(
    JNIEnv* env, jclass, jlong app, jlong index, jbyteArray uuid, jintArray fields)
{
    fly_source_status status{};
    status.struct_size = FLY_SOURCE_STATUS_V1_SIZE;
    status.version = FLY_SOURCE_STATUS_VERSION_1;
    const fly_result result = fly_source_status_get(
        app_from(app), static_cast<uint64_t>(index), &status);
    if (result != FLY_RESULT_OK)
    {
        return result;
    }
    if (uuid != nullptr && env->GetArrayLength(uuid) >= 16)
    {
        env->SetByteArrayRegion(uuid, 0, 16, reinterpret_cast<const jbyte*>(status.source_uuid));
    }
    if (fields != nullptr && env->GetArrayLength(fields) >= 3)
    {
        const jint values[] = {
            static_cast<jint>(status.source_scope),
            static_cast<jint>(status.last_completeness),
            static_cast<jint>(status.freshness),
        };
        env->SetIntArrayRegion(fields, 0, 3, values);
    }
    return FLY_RESULT_OK;
}

} // extern "C"
