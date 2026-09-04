#include "content_identity.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace flynes::catalog {
namespace {

constexpr std::uint64_t max_hash_bytes =
    std::numeric_limits<std::uint64_t>::max() / 8u;
constexpr char uppercase_hex[] = "0123456789ABCDEF";

constexpr std::array<std::uint32_t, 64> sha256_round_constants = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u,
};

std::uint32_t rotate_left(std::uint32_t value, unsigned int bits) noexcept
{
    return (value << bits) | (value >> (32u - bits));
}

std::uint32_t rotate_right(std::uint32_t value, unsigned int bits) noexcept
{
    return (value >> bits) | (value << (32u - bits));
}

std::uint32_t big_endian_u32(const std::uint8_t* bytes) noexcept
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24u) |
           (static_cast<std::uint32_t>(bytes[1]) << 16u) |
           (static_cast<std::uint32_t>(bytes[2]) << 8u) |
           static_cast<std::uint32_t>(bytes[3]);
}

void store_big_endian_u64(std::uint8_t* output, std::uint64_t value) noexcept
{
    for (std::size_t index = 0u; index < 8u; ++index)
    {
        output[7u - index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
}

std::string words_to_hex(const std::uint32_t* words, std::size_t count)
{
    std::string result(count * 8u, '0');
    std::size_t output = 0u;
    for (std::size_t word_index = 0u; word_index < count; ++word_index)
    {
        const std::uint32_t word = words[word_index];
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            result[output++] = uppercase_hex[(word >> shift) & 0xFu];
        }
    }
    return result;
}

std::string u32_to_hex(std::uint32_t value)
{
    return words_to_hex(&value, 1u);
}

const char* error_message(ContentIdentityError error) noexcept
{
    switch (error)
    {
    case ContentIdentityError::NONE:
        return "";
    case ContentIdentityError::INVALID_BYTE_VIEW:
        return "Hash input pointer must not be null when length is nonzero";
    case ContentIdentityError::LENGTH_OVERFLOW:
        return "Hash input length exceeds the supported length limit";
    case ContentIdentityError::ALREADY_FINALIZED:
        return "Hash calculation has already been finalized";
    case ContentIdentityError::INVALID_UTF8:
        return "ID input must be valid Unicode";
    case ContentIdentityError::BLANK:
        return "ID input must not be blank";
    case ContentIdentityError::INVALID_HEX:
        return "Hash input must be hexadecimal";
    }
    return "Content identity calculation failed";
}

TextResult text_error(ContentIdentityError error, std::string message = {})
{
    TextResult result;
    result.error = error;
    result.message = message.empty() ? error_message(error) : std::move(message);
    return result;
}

ContentIdentityError validate_view(ContentBytes bytes) noexcept
{
    return bytes.data == nullptr && bytes.size != 0u
               ? ContentIdentityError::INVALID_BYTE_VIEW
               : ContentIdentityError::NONE;
}

bool size_to_u64(std::size_t size, std::uint64_t& result) noexcept
{
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t))
    {
        if (size > static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max()))
        {
            return false;
        }
    }
    result = static_cast<std::uint64_t>(size);
    return true;
}

template <typename ProcessBlock>
ContentIdentityError update_buffered_hash(ProcessBlock process_block,
                                          std::array<std::uint8_t, 64>& buffer,
                                          std::size_t& buffered,
                                          std::uint64_t& total_bytes,
                                          ContentIdentityError& stored_error,
                                          bool finalized,
                                          ContentBytes bytes) noexcept
{
    if (stored_error != ContentIdentityError::NONE)
    {
        return stored_error;
    }
    if (finalized)
    {
        return ContentIdentityError::ALREADY_FINALIZED;
    }
    stored_error = validate_view(bytes);
    if (stored_error != ContentIdentityError::NONE)
    {
        return stored_error;
    }
    std::uint64_t additional = 0u;
    if (!size_to_u64(bytes.size, additional))
    {
        stored_error = ContentIdentityError::LENGTH_OVERFLOW;
        return stored_error;
    }
    stored_error = validate_hash_byte_count(total_bytes, additional);
    if (stored_error != ContentIdentityError::NONE)
    {
        return stored_error;
    }
    total_bytes += additional;

    const std::uint8_t* input = bytes.data;
    std::size_t remaining = bytes.size;
    if (buffered != 0u)
    {
        const std::size_t copied = std::min(64u - buffered, remaining);
        if (copied != 0u)
        {
            std::memcpy(buffer.data() + buffered, input, copied);
            input += copied;
            remaining -= copied;
            buffered += copied;
        }
        if (buffered == 64u)
        {
            process_block(buffer.data());
            buffered = 0u;
        }
    }
    while (remaining >= 64u)
    {
        process_block(input);
        input += 64u;
        remaining -= 64u;
    }
    if (remaining != 0u)
    {
        std::memcpy(buffer.data(), input, remaining);
        buffered = remaining;
    }
    return ContentIdentityError::NONE;
}

template <typename ProcessBlock, std::size_t WordCount>
TextResult finish_buffered_hash(ProcessBlock process_block,
                                std::array<std::uint32_t, WordCount>& state,
                                std::array<std::uint8_t, 64>& buffer,
                                std::size_t& buffered,
                                std::uint64_t total_bytes,
                                ContentIdentityError error,
                                bool& finalized,
                                std::string& final_hex)
{
    if (error != ContentIdentityError::NONE)
    {
        return text_error(error);
    }
    if (!finalized)
    {
        const std::uint64_t bit_length = total_bytes * 8u;
        buffer[buffered++] = 0x80u;
        if (buffered > 56u)
        {
            std::fill(buffer.begin() + static_cast<std::ptrdiff_t>(buffered),
                      buffer.end(),
                      std::uint8_t{0});
            process_block(buffer.data());
            buffered = 0u;
        }
        std::fill(buffer.begin() + static_cast<std::ptrdiff_t>(buffered),
                  buffer.begin() + 56,
                  std::uint8_t{0});
        store_big_endian_u64(buffer.data() + 56u, bit_length);
        process_block(buffer.data());
        buffered = 0u;
        final_hex = words_to_hex(state.data(), state.size());
        finalized = true;
    }
    TextResult result;
    result.value = final_hex;
    return result;
}

struct Utf8Scan final
{
    bool valid = false;
    bool blank = false;
    std::uint64_t utf16_units = 0u;
};

bool continuation(unsigned char value) noexcept
{
    return value >= 0x80u && value <= 0xBFu;
}

bool blank_code_point(std::uint32_t code_point) noexcept
{
    return (code_point >= 0x0009u && code_point <= 0x000Du) ||
           (code_point >= 0x001Cu && code_point <= 0x0020u) ||
           code_point == 0x00A0u || code_point == 0x1680u ||
           (code_point >= 0x2000u && code_point <= 0x200Au) ||
           code_point == 0x2028u || code_point == 0x2029u ||
           code_point == 0x202Fu || code_point == 0x205Fu ||
           code_point == 0x3000u;
}

Utf8Scan scan_utf8(std::string_view value) noexcept
{
    Utf8Scan result;
    result.valid = true;
    result.blank = value.empty();
    bool all_blank = true;
    std::size_t offset = 0u;
    while (offset < value.size())
    {
        const auto first = static_cast<unsigned char>(value[offset]);
        std::uint32_t code_point = 0u;
        std::size_t width = 0u;
        if (first <= 0x7Fu)
        {
            code_point = first;
            width = 1u;
        }
        else if (first >= 0xC2u && first <= 0xDFu)
        {
            if (offset + 1u >= value.size() ||
                !continuation(static_cast<unsigned char>(value[offset + 1u])))
            {
                result.valid = false;
                return result;
            }
            code_point = (static_cast<std::uint32_t>(first & 0x1Fu) << 6u) |
                         (static_cast<unsigned char>(value[offset + 1u]) & 0x3Fu);
            width = 2u;
        }
        else if (first >= 0xE0u && first <= 0xEFu)
        {
            if (offset + 2u >= value.size())
            {
                result.valid = false;
                return result;
            }
            const auto second = static_cast<unsigned char>(value[offset + 1u]);
            const auto third = static_cast<unsigned char>(value[offset + 2u]);
            const bool second_valid = continuation(second) &&
                                      !(first == 0xE0u && second < 0xA0u) &&
                                      !(first == 0xEDu && second > 0x9Fu);
            if (!second_valid || !continuation(third))
            {
                result.valid = false;
                return result;
            }
            code_point = (static_cast<std::uint32_t>(first & 0x0Fu) << 12u) |
                         (static_cast<std::uint32_t>(second & 0x3Fu) << 6u) |
                         static_cast<std::uint32_t>(third & 0x3Fu);
            width = 3u;
        }
        else if (first >= 0xF0u && first <= 0xF4u)
        {
            if (offset + 3u >= value.size())
            {
                result.valid = false;
                return result;
            }
            const auto second = static_cast<unsigned char>(value[offset + 1u]);
            const auto third = static_cast<unsigned char>(value[offset + 2u]);
            const auto fourth = static_cast<unsigned char>(value[offset + 3u]);
            const bool second_valid = continuation(second) &&
                                      !(first == 0xF0u && second < 0x90u) &&
                                      !(first == 0xF4u && second > 0x8Fu);
            if (!second_valid || !continuation(third) || !continuation(fourth))
            {
                result.valid = false;
                return result;
            }
            code_point = (static_cast<std::uint32_t>(first & 0x07u) << 18u) |
                         (static_cast<std::uint32_t>(second & 0x3Fu) << 12u) |
                         (static_cast<std::uint32_t>(third & 0x3Fu) << 6u) |
                         static_cast<std::uint32_t>(fourth & 0x3Fu);
            width = 4u;
        }
        else
        {
            result.valid = false;
            return result;
        }
        result.utf16_units += code_point > 0xFFFFu ? 2u : 1u;
        all_blank = all_blank && blank_code_point(code_point);
        offset += width;
    }
    result.blank = value.empty() || all_blank;
    return result;
}

TextResult normalize_sha256(std::string_view value)
{
    // Exactly 64 UTF-16 code units occupy between 64 and 192 well-formed UTF-8 bytes.
    if (value.size() < 64u || value.size() > 192u)
    {
        return text_error(ContentIdentityError::INVALID_HEX,
                          "payload SHA-256 must be 64 hex characters");
    }
    const Utf8Scan scan = scan_utf8(value);
    if (scan.valid && scan.utf16_units != 64u)
    {
        return text_error(ContentIdentityError::INVALID_HEX,
                          "payload SHA-256 must be 64 hex characters");
    }
    if (!scan.valid)
    {
        return text_error(ContentIdentityError::INVALID_HEX,
                          "payload SHA-256 must be hexadecimal");
    }
    std::string normalized;
    normalized.reserve(value.size());
    for (const char character : value)
    {
        const bool decimal = character >= '0' && character <= '9';
        const bool lower = character >= 'a' && character <= 'f';
        const bool upper = character >= 'A' && character <= 'F';
        if (!decimal && !lower && !upper)
        {
            return text_error(ContentIdentityError::INVALID_HEX,
                              "payload SHA-256 must be hexadecimal");
        }
        normalized.push_back(lower
                                 ? static_cast<char>(character - ('a' - 'A'))
                                 : character);
    }
    TextResult result;
    result.value = std::move(normalized);
    return result;
}

TextResult validate_id_input(std::string_view value)
{
    std::uint64_t byte_length = 0u;
    if (!size_to_u64(value.size(), byte_length) ||
        validate_id_utf8_length(byte_length) != ContentIdentityError::NONE)
    {
        return text_error(ContentIdentityError::LENGTH_OVERFLOW, "ID input is too long");
    }
    const Utf8Scan scan = scan_utf8(value);
    if (!scan.valid)
    {
        return text_error(ContentIdentityError::INVALID_UTF8);
    }
    if (scan.blank)
    {
        return text_error(ContentIdentityError::BLANK);
    }
    TextResult result;
    result.value.assign(value.data(), value.size());
    return result;
}

StableIdResult stable_error(const TextResult& error)
{
    StableIdResult result;
    result.error = error.error;
    result.message = error.message;
    return result;
}

StableIdResult digest_id(std::string_view prefix,
                         std::initializer_list<std::string_view> parts)
{
    Sha256Hasher digest;
    for (const std::string_view part : parts)
    {
        const TextResult checked = validate_id_input(part);
        if (!checked.ok())
        {
            return stable_error(checked);
        }
        const std::uint32_t length = static_cast<std::uint32_t>(part.size());
        const std::array<std::uint8_t, 4> framed_length = {
            static_cast<std::uint8_t>(length >> 24u),
            static_cast<std::uint8_t>(length >> 16u),
            static_cast<std::uint8_t>(length >> 8u),
            static_cast<std::uint8_t>(length),
        };
        ContentIdentityError error = digest.update({framed_length.data(), framed_length.size()});
        if (error == ContentIdentityError::NONE)
        {
            error = digest.update({reinterpret_cast<const std::uint8_t*>(part.data()), part.size()});
        }
        if (error != ContentIdentityError::NONE)
        {
            StableIdResult result;
            result.error = error;
            result.message = error_message(error);
            return result;
        }
    }
    const TextResult hashed = digest.finish_hex();
    if (!hashed.ok())
    {
        return stable_error(hashed);
    }
    StableIdResult result;
    result.value.reserve(prefix.size() + hashed.value.size());
    result.value.append(prefix.data(), prefix.size());
    result.value += hashed.value;
    return result;
}

template <typename Hasher>
TextResult one_shot(ContentBytes bytes)
{
    Hasher hasher;
    const ContentIdentityError error = hasher.update(bytes);
    if (error != ContentIdentityError::NONE)
    {
        return text_error(error);
    }
    return hasher.finish_hex();
}

} // namespace

ContentIdentityError validate_hash_byte_count(std::uint64_t current_bytes,
                                               std::uint64_t additional_bytes) noexcept
{
    if (current_bytes > max_hash_bytes || additional_bytes > max_hash_bytes - current_bytes)
    {
        return ContentIdentityError::LENGTH_OVERFLOW;
    }
    return ContentIdentityError::NONE;
}

ContentIdentityError validate_id_utf8_length(std::uint64_t byte_length) noexcept
{
    return byte_length > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())
               ? ContentIdentityError::LENGTH_OVERFLOW
               : ContentIdentityError::NONE;
}

Sha1Hasher::Sha1Hasher() noexcept
    : state_({0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u})
{
}

void Sha1Hasher::process_block(const std::uint8_t* block) noexcept
{
    std::array<std::uint32_t, 80> words{};
    for (std::size_t index = 0u; index < 16u; ++index)
    {
        words[index] = big_endian_u32(block + index * 4u);
    }
    for (std::size_t index = 16u; index < words.size(); ++index)
    {
        words[index] = rotate_left(
            words[index - 3u] ^ words[index - 8u] ^ words[index - 14u] ^ words[index - 16u],
            1u);
    }
    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    for (std::size_t index = 0u; index < words.size(); ++index)
    {
        std::uint32_t function = 0u;
        std::uint32_t constant = 0u;
        if (index < 20u)
        {
            function = (b & c) | (~b & d);
            constant = 0x5A827999u;
        }
        else if (index < 40u)
        {
            function = b ^ c ^ d;
            constant = 0x6ED9EBA1u;
        }
        else if (index < 60u)
        {
            function = (b & c) | (b & d) | (c & d);
            constant = 0x8F1BBCDCu;
        }
        else
        {
            function = b ^ c ^ d;
            constant = 0xCA62C1D6u;
        }
        const std::uint32_t temporary =
            rotate_left(a, 5u) + function + e + constant + words[index];
        e = d;
        d = c;
        c = rotate_left(b, 30u);
        b = a;
        a = temporary;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
}

ContentIdentityError Sha1Hasher::update(ContentBytes bytes) noexcept
{
    return update_buffered_hash(
        [this](const std::uint8_t* block) noexcept { process_block(block); },
        buffer_,
        buffered_,
        total_bytes_,
        error_,
        finalized_,
        bytes);
}

TextResult Sha1Hasher::finish_hex()
{
    return finish_buffered_hash(
        [this](const std::uint8_t* block) noexcept { process_block(block); },
        state_,
        buffer_,
        buffered_,
        total_bytes_,
        error_,
        finalized_,
        final_hex_);
}

Sha256Hasher::Sha256Hasher() noexcept
    : state_({0x6A09E667u,
              0xBB67AE85u,
              0x3C6EF372u,
              0xA54FF53Au,
              0x510E527Fu,
              0x9B05688Cu,
              0x1F83D9ABu,
              0x5BE0CD19u})
{
}

void Sha256Hasher::process_block(const std::uint8_t* block) noexcept
{
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0u; index < 16u; ++index)
    {
        words[index] = big_endian_u32(block + index * 4u);
    }
    for (std::size_t index = 16u; index < words.size(); ++index)
    {
        const std::uint32_t first = rotate_right(words[index - 15u], 7u) ^
                                    rotate_right(words[index - 15u], 18u) ^
                                    (words[index - 15u] >> 3u);
        const std::uint32_t second = rotate_right(words[index - 2u], 17u) ^
                                     rotate_right(words[index - 2u], 19u) ^
                                     (words[index - 2u] >> 10u);
        words[index] = words[index - 16u] + first + words[index - 7u] + second;
    }
    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];
    for (std::size_t index = 0u; index < words.size(); ++index)
    {
        const std::uint32_t sum_one =
            rotate_right(e, 6u) ^ rotate_right(e, 11u) ^ rotate_right(e, 25u);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t temporary_one =
            h + sum_one + choice + sha256_round_constants[index] + words[index];
        const std::uint32_t sum_zero =
            rotate_right(a, 2u) ^ rotate_right(a, 13u) ^ rotate_right(a, 22u);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temporary_two = sum_zero + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary_one;
        d = c;
        c = b;
        b = a;
        a = temporary_one + temporary_two;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

ContentIdentityError Sha256Hasher::update(ContentBytes bytes) noexcept
{
    return update_buffered_hash(
        [this](const std::uint8_t* block) noexcept { process_block(block); },
        buffer_,
        buffered_,
        total_bytes_,
        error_,
        finalized_,
        bytes);
}

TextResult Sha256Hasher::finish_hex()
{
    return finish_buffered_hash(
        [this](const std::uint8_t* block) noexcept { process_block(block); },
        state_,
        buffer_,
        buffered_,
        total_bytes_,
        error_,
        finalized_,
        final_hex_);
}

ContentIdentityError Crc32Hasher::update(ContentBytes bytes) noexcept
{
    if (error_ != ContentIdentityError::NONE)
    {
        return error_;
    }
    if (finalized_)
    {
        return ContentIdentityError::ALREADY_FINALIZED;
    }
    error_ = validate_view(bytes);
    if (error_ != ContentIdentityError::NONE)
    {
        return error_;
    }
    std::uint64_t additional = 0u;
    if (!size_to_u64(bytes.size, additional) ||
        additional > std::numeric_limits<std::uint64_t>::max() - total_bytes_)
    {
        error_ = ContentIdentityError::LENGTH_OVERFLOW;
        return error_;
    }
    total_bytes_ += additional;
    for (std::size_t index = 0u; index < bytes.size; ++index)
    {
        state_ ^= bytes.data[index];
        for (unsigned int bit = 0u; bit < 8u; ++bit)
        {
            const std::uint32_t mask = 0u - (state_ & 1u);
            state_ = (state_ >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ContentIdentityError::NONE;
}

TextResult Crc32Hasher::finish_hex()
{
    if (error_ != ContentIdentityError::NONE)
    {
        return text_error(error_);
    }
    if (!finalized_)
    {
        final_hex_ = u32_to_hex(state_ ^ 0xFFFFFFFFu);
        finalized_ = true;
    }
    TextResult result;
    result.value = final_hex_;
    return result;
}

TextResult sha1_hex(ContentBytes bytes)
{
    return one_shot<Sha1Hasher>(bytes);
}

TextResult sha256_hex(ContentBytes bytes)
{
    return one_shot<Sha256Hasher>(bytes);
}

TextResult crc32_hex(ContentBytes bytes)
{
    return one_shot<Crc32Hasher>(bytes);
}

RomContentHashResult hash_rom_content(ContentBytes payload, ContentBytes physical_package)
{
    const TextResult payload_sha1 = sha1_hex(payload);
    const TextResult payload_sha256 = sha256_hex(payload);
    const TextResult payload_crc32 = crc32_hex(payload);
    const TextResult physical_sha256 = sha256_hex(physical_package);
    for (const TextResult* result :
         {&payload_sha1, &payload_sha256, &payload_crc32, &physical_sha256})
    {
        if (!result->ok())
        {
            RomContentHashResult failure;
            failure.error = result->error;
            failure.message = result->message;
            return failure;
        }
    }
    RomContentHashResult result;
    result.value.payload_sha1 = payload_sha1.value;
    result.value.payload_sha256 = payload_sha256.value;
    result.value.physical_package_sha256 = physical_sha256.value;
    result.value.crc32 = payload_crc32.value;
    return result;
}

StableIdResult package_id(std::string_view source_id, std::string_view stable_document_key)
{
    return digest_id("pkg:", {source_id, stable_document_key});
}

StableIdResult saf_source_id(std::string_view tree_locator)
{
    return digest_id("source:", {"SAF_TREE", tree_locator});
}

StableIdResult variant_id(std::string_view package_id_value,
                          std::string_view exact_raw_locator,
                          std::string_view payload_sha256)
{
    const TextResult normalized = normalize_sha256(payload_sha256);
    if (!normalized.ok())
    {
        return stable_error(normalized);
    }
    return digest_id("variant:", {package_id_value, exact_raw_locator, normalized.value});
}

StableIdResult provisional_game_id(std::string_view payload_sha256)
{
    const TextResult normalized = normalize_sha256(payload_sha256);
    if (!normalized.ok())
    {
        return stable_error(normalized);
    }
    StableIdResult result;
    result.value = "game:" + normalized.value;
    return result;
}

StableIdResult entry_outcome_id(std::string_view package_id_value,
                                std::string_view exact_raw_locator)
{
    return digest_id("entry:", {package_id_value, exact_raw_locator});
}

} // namespace flynes::catalog
