#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "app/layout_persist.hpp"

#include "flynes/product/control_layout.hpp"

#include <array>
#include <cstdio>
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

constexpr std::uint64_t kMaxBytes = UINT64_C(65536);

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

bool is_utf8_continuation(std::uint8_t byte) noexcept
{
    return byte >= 0x80u && byte <= 0xBFu;
}

bool is_valid_utf8(const char* bytes, std::uint32_t length) noexcept
{
    if (bytes == nullptr)
    {
        return false;
    }
    std::uint32_t index = 0u;
    while (index < length)
    {
        const auto first = static_cast<std::uint8_t>(bytes[index]);
        if (first <= 0x7Fu)
        {
            if (first == 0u)
            {
                return false;
            }
            ++index;
            continue;
        }
        if (first >= 0xC2u && first <= 0xDFu)
        {
            if (length - index < 2u ||
                !is_utf8_continuation(static_cast<std::uint8_t>(bytes[index + 1u])))
            {
                return false;
            }
            index += 2u;
            continue;
        }
        if (first >= 0xE0u && first <= 0xEFu)
        {
            if (length - index < 3u)
            {
                return false;
            }
            const auto second = static_cast<std::uint8_t>(bytes[index + 1u]);
            const auto third = static_cast<std::uint8_t>(bytes[index + 2u]);
            if (!is_utf8_continuation(second) || !is_utf8_continuation(third))
            {
                return false;
            }
            if (first == 0xE0u && second < 0xA0u)
            {
                return false;
            }
            if (first == 0xEDu && second > 0x9Fu)
            {
                return false;
            }
            index += 3u;
            continue;
        }
        if (first >= 0xF0u && first <= 0xF4u)
        {
            if (length - index < 4u)
            {
                return false;
            }
            const auto second = static_cast<std::uint8_t>(bytes[index + 1u]);
            const auto third = static_cast<std::uint8_t>(bytes[index + 2u]);
            const auto fourth = static_cast<std::uint8_t>(bytes[index + 3u]);
            if (!is_utf8_continuation(second) || !is_utf8_continuation(third) ||
                !is_utf8_continuation(fourth))
            {
                return false;
            }
            if (first == 0xF0u && second < 0x90u)
            {
                return false;
            }
            if (first == 0xF4u && second > 0x8Fu)
            {
                return false;
            }
            index += 4u;
            continue;
        }
        return false;
    }
    return true;
}

std::string normalize_encoded(const std::string& raw_utf8)
{
    if (!is_valid_utf8(raw_utf8.data(), static_cast<std::uint32_t>(raw_utf8.size())))
    {
        return flynes::product::ControlLayoutV2::recommended().encode();
    }
    return flynes::product::ControlLayoutV2::decode_or_recommended(raw_utf8).encode();
}

std::string recommended_encoded()
{
    return flynes::product::ControlLayoutV2::recommended().encode();
}

} // namespace

std::string load_control_layout(const std::string& data_root_utf8)
{
    const std::filesystem::path root = root_path(data_root_utf8);
    const std::filesystem::path live = root / "control_layout.v2";
    const std::filesystem::path bak = root / "control_layout.v2.bak";
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
        return recommended_encoded();
    }
    std::vector<std::uint8_t> bytes;
    if (!read_file_bounded(chosen, bytes))
    {
        return recommended_encoded();
    }
    return normalize_encoded(
        std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

bool save_control_layout(const std::string& data_root_utf8, const std::string& encoded_utf8)
{
    const std::string normalized = normalize_encoded(encoded_utf8);
    std::vector<std::uint8_t> bytes(normalized.begin(), normalized.end());

    const std::filesystem::path root = root_path(data_root_utf8);
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
    {
        return false;
    }
    const std::filesystem::path live = root / "control_layout.v2";
    const std::filesystem::path tmp = root / "control_layout.v2.tmp";
    const std::filesystem::path bak = root / "control_layout.v2.bak";
    if (!write_file_sync(tmp, bytes))
    {
        std::filesystem::remove(tmp, error);
        return false;
    }
    return replace_live(live, tmp, bak);
}

} // namespace flynes::app
