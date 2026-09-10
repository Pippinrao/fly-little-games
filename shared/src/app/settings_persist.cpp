#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "app/settings_persist.hpp"

#include "catalog/content_identity.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace flynes::app {
namespace {

constexpr char kMagic[] = "FLYSET01";
constexpr std::uint32_t kVersion = 1u;
constexpr std::uint32_t kMaxStringBytes = 65536u;
constexpr std::uint64_t kMaxBytes = UINT64_C(16777216);

std::filesystem::path root_path(const std::string& data_root_utf8)
{
    return std::filesystem::u8path(data_root_utf8);
}

FILE* open_write(const std::filesystem::path& path)
{
#if defined(_WIN32)
    return _wfopen(path.wstring().c_str(), L"wb");
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

FILE* open_read(const std::filesystem::path& path)
{
#if defined(_WIN32)
    return _wfopen(path.wstring().c_str(), L"rb");
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

bool flush_sync_close(FILE* file)
{
    if (file == nullptr)
    {
        return false;
    }
    if (std::fflush(file) != 0)
    {
        std::fclose(file);
        return false;
    }
#if defined(_WIN32)
    if (_commit(_fileno(file)) != 0)
    {
        std::fclose(file);
        return false;
    }
#else
    if (fsync(fileno(file)) != 0)
    {
        std::fclose(file);
        return false;
    }
#endif
    return std::fclose(file) == 0;
}

bool read_file_bounded(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes)
{
    bytes.clear();
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
    {
        return false;
    }
    FILE* file = open_read(path);
    if (file == nullptr)
    {
        return false;
    }
    std::array<std::uint8_t, 8192> buffer{};
    std::uint64_t total = 0u;
    for (;;)
    {
        const std::size_t amount = std::fread(buffer.data(), 1u, buffer.size(), file);
        if (amount == 0u)
        {
            const bool ok = std::feof(file) != 0 && std::ferror(file) == 0;
            std::fclose(file);
            return ok;
        }
        if (total > kMaxBytes || amount > kMaxBytes - total)
        {
            std::fclose(file);
            bytes.clear();
            return false;
        }
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(amount));
        total += static_cast<std::uint64_t>(amount);
    }
}

bool write_file_sync(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    FILE* file = open_write(path);
    if (file == nullptr)
    {
        return false;
    }
    if (!bytes.empty() && std::fwrite(bytes.data(), 1u, bytes.size(), file) != bytes.size())
    {
        std::fclose(file);
        return false;
    }
    return flush_sync_close(file);
}

bool replace_live(const std::filesystem::path& live,
                  const std::filesystem::path& tmp,
                  const std::filesystem::path& bak)
{
    std::error_code error;
    if (std::filesystem::exists(live, error))
    {
        std::filesystem::remove(bak, error);
        std::filesystem::rename(live, bak, error);
        if (error)
        {
            std::filesystem::remove(tmp, error);
            return false;
        }
    }
    std::filesystem::rename(tmp, live, error);
    if (error)
    {
        std::error_code restore;
        if (std::filesystem::exists(bak, restore))
        {
            std::filesystem::rename(bak, live, restore);
        }
        std::filesystem::remove(tmp, error);
        return false;
    }
    return true;
}

bool sha256_bytes(const std::uint8_t* data, std::size_t size, std::array<std::uint8_t, 32>& out)
{
    const auto hex = flynes::catalog::sha256_hex(flynes::catalog::ContentBytes{data, size});
    if (!hex.ok() || hex.value.size() != 64u)
    {
        return false;
    }
    const auto nibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        return -1;
    };
    for (std::size_t index = 0; index < 32u; ++index)
    {
        const int high = nibble(hex.value[index * 2u]);
        const int low = nibble(hex.value[index * 2u + 1u]);
        if (high < 0 || low < 0)
        {
            return false;
        }
        out[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8u));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16u));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24u));
}

void append_f32(std::vector<std::uint8_t>& bytes, float value)
{
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(bytes, bits);
}

void append_string(std::vector<std::uint8_t>& bytes, const std::string& value)
{
    append_u32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

bool take_u32(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::uint32_t& value)
{
    if (bytes.size() - offset < 4u)
    {
        return false;
    }
    value = static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
            (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
            (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
    offset += 4u;
    return true;
}

bool take_f32(const std::vector<std::uint8_t>& bytes, std::size_t& offset, float& value)
{
    std::uint32_t bits = 0u;
    if (!take_u32(bytes, offset, bits))
    {
        return false;
    }
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}

bool take_string(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::string& value)
{
    std::uint32_t size = 0u;
    if (!take_u32(bytes, offset, size) || size > kMaxStringBytes || bytes.size() - offset < size)
    {
        return false;
    }
    value.assign(reinterpret_cast<const char*>(bytes.data() + offset), size);
    offset += size;
    return true;
}

std::vector<std::uint8_t> encode_payload(const SettingsData& settings)
{
    std::vector<std::uint8_t> bytes;
    append_u32(bytes, settings.aspect_mode);
    append_u32(bytes, settings.video_quality_preset);
    append_u32(bytes, settings.custom_refresh_policy);
    append_u32(bytes, settings.custom_temporal_mode);
    append_u32(bytes, settings.custom_spatial_mode);
    append_u32(bytes, settings.custom_post_effect);
    append_u32(bytes, settings.adaptive_protection);
    append_u32(bytes, settings.layout_preset);
    append_u32(bytes, settings.direction_mode);
    append_f32(bytes, settings.button_scale);
    append_f32(bytes, settings.vertical_offset);
    append_f32(bytes, settings.control_opacity);
    append_f32(bytes, settings.joystick_scale);
    append_f32(bytes, settings.dead_zone);
    append_u32(bytes, settings.haptic_level);
    append_u32(bytes, settings.distinct_ab_haptics);
    append_u32(bytes, settings.audio_enabled);
    append_u32(bytes, settings.audio_focus_policy);
    append_u32(bytes, settings.autosave_enabled);
    append_string(bytes, settings.locale_tag);
    append_string(bytes, settings.last_played_id);
    return bytes;
}

bool decode_payload(const std::vector<std::uint8_t>& bytes, SettingsData& settings)
{
    std::size_t offset = 0u;
    return take_u32(bytes, offset, settings.aspect_mode) &&
           take_u32(bytes, offset, settings.video_quality_preset) &&
           take_u32(bytes, offset, settings.custom_refresh_policy) &&
           take_u32(bytes, offset, settings.custom_temporal_mode) &&
           take_u32(bytes, offset, settings.custom_spatial_mode) &&
           take_u32(bytes, offset, settings.custom_post_effect) &&
           take_u32(bytes, offset, settings.adaptive_protection) &&
           take_u32(bytes, offset, settings.layout_preset) &&
           take_u32(bytes, offset, settings.direction_mode) &&
           take_f32(bytes, offset, settings.button_scale) &&
           take_f32(bytes, offset, settings.vertical_offset) &&
           take_f32(bytes, offset, settings.control_opacity) &&
           take_f32(bytes, offset, settings.joystick_scale) &&
           take_f32(bytes, offset, settings.dead_zone) &&
           take_u32(bytes, offset, settings.haptic_level) &&
           take_u32(bytes, offset, settings.distinct_ab_haptics) &&
           take_u32(bytes, offset, settings.audio_enabled) &&
           take_u32(bytes, offset, settings.audio_focus_policy) &&
           take_u32(bytes, offset, settings.autosave_enabled) &&
           take_string(bytes, offset, settings.locale_tag) &&
           take_string(bytes, offset, settings.last_played_id) &&
           offset == bytes.size();
}

bool decode_file(const std::vector<std::uint8_t>& encoded, SettingsData& settings)
{
    if (encoded.size() < 8u + 4u + 32u || encoded.size() > kMaxBytes ||
        std::memcmp(encoded.data(), kMagic, 8) != 0)
    {
        return false;
    }
    const std::uint32_t version = static_cast<std::uint32_t>(encoded[8]) |
                                  (static_cast<std::uint32_t>(encoded[9]) << 8u) |
                                  (static_cast<std::uint32_t>(encoded[10]) << 16u) |
                                  (static_cast<std::uint32_t>(encoded[11]) << 24u);
    if (version != kVersion)
    {
        return false;
    }
    std::array<std::uint8_t, 32> digest{};
    if (!sha256_bytes(encoded.data(), encoded.size() - 32u, digest) ||
        std::memcmp(digest.data(), encoded.data() + encoded.size() - 32u, 32) != 0)
    {
        return false;
    }
    std::vector<std::uint8_t> payload(encoded.begin() + 12, encoded.end() - 32);
    return decode_payload(payload, settings);
}

} // namespace

SettingsData load_settings(const std::string& data_root_utf8)
{
    SettingsData defaults;
    const std::filesystem::path root = root_path(data_root_utf8);
    const std::filesystem::path live = root / "settings.flyset01";
    const std::filesystem::path bak = root / "settings.flyset01.bak";
    std::error_code error;
    std::filesystem::path chosen;
    if (std::filesystem::is_regular_file(live, error))
    {
        chosen = live;
    }
    else if (std::filesystem::is_regular_file(bak, error))
    {
        chosen = bak;
    }
    else
    {
        return defaults;
    }
    std::vector<std::uint8_t> encoded;
    SettingsData loaded;
    if (!read_file_bounded(chosen, encoded) || !decode_file(encoded, loaded))
    {
        return defaults;
    }
    return loaded;
}

bool save_settings(const std::string& data_root_utf8, const SettingsData& settings)
{
    const std::vector<std::uint8_t> payload = encode_payload(settings);
    std::vector<std::uint8_t> file(reinterpret_cast<const std::uint8_t*>(kMagic),
                                   reinterpret_cast<const std::uint8_t*>(kMagic) + 8);
    append_u32(file, kVersion);
    file.insert(file.end(), payload.begin(), payload.end());
    std::array<std::uint8_t, 32> digest{};
    if (!sha256_bytes(file.data(), file.size(), digest) || file.size() + 32u > kMaxBytes)
    {
        return false;
    }
    file.insert(file.end(), digest.begin(), digest.end());

    const std::filesystem::path root = root_path(data_root_utf8);
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
    {
        return false;
    }
    const std::filesystem::path live = root / "settings.flyset01";
    const std::filesystem::path tmp = root / "settings.flyset01.tmp";
    const std::filesystem::path bak = root / "settings.flyset01.bak";
    if (!write_file_sync(tmp, file))
    {
        std::filesystem::remove(tmp, error);
        return false;
    }
    return replace_live(live, tmp, bak);
}

} // namespace flynes::app
