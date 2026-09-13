#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace flynes::catalog {

struct GameTitleRecord final {
    const char* payload_sha256_hex;
    const char* index_id;
    const char* title_en;
    const char* title_zh_hans;
    const char* aliases;
};

struct GameTitleMatch final {
    const GameTitleRecord* record = nullptr;
    std::uint32_t match_kind = 0; // 0 unknown, 1 exact content hash, 2 explicit alias
};

// ASCII case/spacing/separators and known region suffixes only. Variant markers
// (including translations, hacks and sequel numbers) remain part of the key.
std::string normalize_game_title_alias(std::string_view name);

class GameTitleIndex final {
public:
    // Records and their strings must outlive this index and all lookup results.
    GameTitleIndex(const GameTitleRecord* records, std::size_t count);
    GameTitleMatch lookup(const std::uint8_t* payload_sha256,
                          std::string_view fallback_name) const;
private:
    std::unordered_map<std::string, const GameTitleRecord*> hashes_;
    std::unordered_map<std::string, const GameTitleRecord*> aliases_;
};

// Thread-safe, initialized once; all result strings have process lifetime.
const GameTitleIndex& game_title_index();

} // namespace flynes::catalog
