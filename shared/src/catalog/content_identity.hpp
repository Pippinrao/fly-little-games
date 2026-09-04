#ifndef FLYNES_CATALOG_CONTENT_IDENTITY_HPP
#define FLYNES_CATALOG_CONTENT_IDENTITY_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace flynes::catalog {

/** Borrowed bytes consumed synchronously. A null pointer is valid only for an empty view. */
struct ContentBytes final
{
    const std::uint8_t* data = nullptr;
    std::size_t size = 0u;
};

enum class ContentIdentityError : std::uint8_t
{
    NONE = 0,
    INVALID_BYTE_VIEW = 1,
    LENGTH_OVERFLOW = 2,
    ALREADY_FINALIZED = 3,
    INVALID_UTF8 = 4,
    BLANK = 5,
    INVALID_HEX = 6,
};

struct TextResult final
{
    ContentIdentityError error = ContentIdentityError::NONE;
    std::string value;
    std::string message;

    bool ok() const noexcept
    {
        return error == ContentIdentityError::NONE;
    }
};

ContentIdentityError validate_hash_byte_count(std::uint64_t current_bytes,
                                               std::uint64_t additional_bytes) noexcept;
ContentIdentityError validate_id_utf8_length(std::uint64_t byte_length) noexcept;

class Sha1Hasher final
{
public:
    Sha1Hasher() noexcept;

    ContentIdentityError update(ContentBytes bytes) noexcept;
    TextResult finish_hex();

private:
    void process_block(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 5> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_ = 0u;
    std::uint64_t total_bytes_ = 0u;
    ContentIdentityError error_ = ContentIdentityError::NONE;
    bool finalized_ = false;
    std::string final_hex_;
};

class Sha256Hasher final
{
public:
    Sha256Hasher() noexcept;

    ContentIdentityError update(ContentBytes bytes) noexcept;
    TextResult finish_hex();

private:
    void process_block(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_ = 0u;
    std::uint64_t total_bytes_ = 0u;
    ContentIdentityError error_ = ContentIdentityError::NONE;
    bool finalized_ = false;
    std::string final_hex_;
};

class Crc32Hasher final
{
public:
    Crc32Hasher() noexcept = default;

    ContentIdentityError update(ContentBytes bytes) noexcept;
    TextResult finish_hex();

private:
    std::uint32_t state_ = 0xFFFFFFFFu;
    std::uint64_t total_bytes_ = 0u;
    ContentIdentityError error_ = ContentIdentityError::NONE;
    bool finalized_ = false;
    std::string final_hex_;
};

TextResult sha1_hex(ContentBytes bytes);
TextResult sha256_hex(ContentBytes bytes);
TextResult crc32_hex(ContentBytes bytes);

struct RomContentHashes final
{
    std::string payload_sha1;
    std::string payload_sha256;
    std::string physical_package_sha256;
    std::string crc32;
};

struct RomContentHashResult final
{
    ContentIdentityError error = ContentIdentityError::NONE;
    RomContentHashes value;
    std::string message;

    bool ok() const noexcept
    {
        return error == ContentIdentityError::NONE;
    }
};

RomContentHashResult hash_rom_content(ContentBytes payload, ContentBytes physical_package);

struct StableIdResult final
{
    ContentIdentityError error = ContentIdentityError::NONE;
    std::string value;
    std::string message;

    bool ok() const noexcept
    {
        return error == ContentIdentityError::NONE;
    }
};

inline constexpr std::string_view RAW_LOCATOR = "RAW";

StableIdResult package_id(std::string_view source_id, std::string_view stable_document_key);
StableIdResult saf_source_id(std::string_view tree_locator);
StableIdResult variant_id(std::string_view package_id_value,
                          std::string_view exact_raw_locator,
                          std::string_view payload_sha256);
StableIdResult provisional_game_id(std::string_view payload_sha256);
StableIdResult entry_outcome_id(std::string_view package_id_value,
                                std::string_view exact_raw_locator);

} // namespace flynes::catalog

#endif
