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
    static bool is_blank(const std::string& value)
    {
        for (const char ch : value)
        {
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f' && ch != '\v')
            {
                return false;
            }
        }
        return true;
    }
};

} // namespace flynes::product
