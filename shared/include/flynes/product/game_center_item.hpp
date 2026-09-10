#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace flynes::product {

struct GameCenterItem final
{
    std::string canonical_id;
    std::string title_en;
    std::string title_zh_hans;
    bool builtin = false;
    bool favorite = false;
    std::int64_t last_played_sequence = 0;
    std::string original_filename;

    GameCenterItem(std::string canonical_id_value,
                   std::string title_en_value,
                   std::string title_zh_hans_value,
                   bool builtin_value,
                   bool favorite_value,
                   std::int64_t last_played_sequence_value,
                   std::string original_filename_value)
        : canonical_id(std::move(canonical_id_value))
        , title_en(std::move(title_en_value))
        , title_zh_hans(std::move(title_zh_hans_value))
        , builtin(builtin_value)
        , favorite(favorite_value)
        , last_played_sequence(last_played_sequence_value)
        , original_filename(std::move(original_filename_value))
    {
        if (is_blank(canonical_id))
        {
            throw std::invalid_argument("canonical id must not be blank");
        }
        if (last_played_sequence < 0)
        {
            throw std::invalid_argument("last played sequence must not be negative");
        }
    }

private:
    static bool is_frozen_blank_code_point(char32_t code_point)
    {
        return (code_point >= 0x0009 && code_point <= 0x000D)
            || (code_point >= 0x001C && code_point <= 0x0020)
            || code_point == 0x00A0
            || code_point == 0x1680
            || (code_point >= 0x2000 && code_point <= 0x200A)
            || code_point == 0x2028
            || code_point == 0x2029
            || code_point == 0x202F
            || code_point == 0x205F
            || code_point == 0x3000;
    }

    static bool decode_next_code_point(const std::string& value,
                                       std::size_t& offset,
                                       char32_t& code_point)
    {
        if (offset >= value.size())
        {
            return false;
        }

        const unsigned char lead = static_cast<unsigned char>(value[offset]);
        if (lead < 0x80)
        {
            code_point = lead;
            ++offset;
            return true;
        }
        if ((lead & 0xE0) == 0xC0)
        {
            if (offset + 1 >= value.size())
            {
                return false;
            }
            const unsigned char b1 = static_cast<unsigned char>(value[offset + 1]);
            if ((b1 & 0xC0) != 0x80)
            {
                return false;
            }
            code_point = ((lead & 0x1F) << 6) | (b1 & 0x3F);
            offset += 2;
            return true;
        }
        if ((lead & 0xF0) == 0xE0)
        {
            if (offset + 2 >= value.size())
            {
                return false;
            }
            const unsigned char b1 = static_cast<unsigned char>(value[offset + 1]);
            const unsigned char b2 = static_cast<unsigned char>(value[offset + 2]);
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80)
            {
                return false;
            }
            code_point = ((lead & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
            offset += 3;
            return true;
        }
        if ((lead & 0xF8) == 0xF0)
        {
            if (offset + 3 >= value.size())
            {
                return false;
            }
            const unsigned char b1 = static_cast<unsigned char>(value[offset + 1]);
            const unsigned char b2 = static_cast<unsigned char>(value[offset + 2]);
            const unsigned char b3 = static_cast<unsigned char>(value[offset + 3]);
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80)
            {
                return false;
            }
            code_point = ((lead & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6)
                | (b3 & 0x3F);
            offset += 4;
            return true;
        }
        return false;
    }

    static bool is_blank(const std::string& value)
    {
        if (value.empty())
        {
            return true;
        }

        std::size_t offset = 0;
        while (offset < value.size())
        {
            char32_t code_point = 0;
            if (!decode_next_code_point(value, offset, code_point))
            {
                return false;
            }
            if (!is_frozen_blank_code_point(code_point))
            {
                return false;
            }
        }
        return true;
    }
};

} // namespace flynes::product
