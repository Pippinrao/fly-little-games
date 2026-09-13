#include <flynes/catalog/game_title_index.hpp>
#include <algorithm>
#include <cstring>

namespace flynes::catalog {
namespace {
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
void trim(std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n\f\v");
    if (first == std::string::npos) { value.clear(); return; }
    value.erase(value.find_last_not_of(" \t\r\n\f\v") + 1);
    value.erase(0, first);
}
bool region(std::string_view value) {
    static constexpr std::string_view regions[] = {
        "u", "e", "j", "ju", "je", "ue", "jue", "usa", "europe", "japan", "world", "asia", "china", "hong kong",
        "korea", "australia", "canada", "france", "germany", "italy", "spain", "sweden",
        "usa, europe", "japan, usa", "japan, europe", "japan, usa, europe", "u,e", "j,u", "j,e", "j,u,e"
    };
    return std::find(std::begin(regions), std::end(regions), value) != std::end(regions);
}
bool same_title(const GameTitleRecord& a, const GameTitleRecord& b) {
    return std::strcmp(a.title_en, b.title_en) == 0 && std::strcmp(a.title_zh_hans, b.title_zh_hans) == 0;
}
void insert(std::unordered_map<std::string, const GameTitleRecord*>& map,
            std::string key, const GameTitleRecord& row) {
    if (key.empty()) return;
    auto added = map.emplace(std::move(key), &row);
    if (!added.second && added.first->second != nullptr) {
        const auto* old = added.first->second;
        if (!same_title(*old, row)) added.first->second = nullptr;
        else if (std::strcmp(row.index_id, old->index_id) < 0) added.first->second = &row;
    }
}
}

std::string normalize_game_title_alias(std::string_view name) {
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos) name.remove_prefix(slash + 1);
    std::string result(name);
    for (char& c : result) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    trim(result);
    const auto dot = result.find_last_of('.');
    if (dot != std::string::npos) {
        const std::string_view suffix(result.data() + dot, result.size() - dot);
        if (suffix == ".nes" || suffix == ".fds" || suffix == ".unf" || suffix == ".unif") result.erase(dot);
    }
    trim(result);
    while (!result.empty() && (result.back() == ')' || result.back() == ']')) {
        const char opener = result.back() == ')' ? '(' : '[';
        const auto start = result.find_last_of(opener);
        if (start == std::string::npos) break;
        std::string tag = result.substr(start + 1, result.size() - start - 2);
        trim(tag);
        if (!region(tag)) break;
        result.erase(start);
        trim(result);
    }
    std::string key;
    bool separator = false;
    for (char c : result) {
        if (space(c) || c == '_' || c == '-') { separator = !key.empty(); continue; }
        if (separator) key.push_back(' ');
        key.push_back(c);
        separator = false;
    }
    return key;
}

GameTitleIndex::GameTitleIndex(const GameTitleRecord* records, std::size_t count) {
    hashes_.reserve(count);
    aliases_.reserve(count * 3);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& row = records[i];
        insert(hashes_, row.payload_sha256_hex, row);
        insert(aliases_, normalize_game_title_alias(row.title_en), row);
        insert(aliases_, normalize_game_title_alias(row.title_zh_hans), row);
        std::string_view aliases(row.aliases);
        while (!aliases.empty()) {
            const auto end = aliases.find('\n');
            insert(aliases_, normalize_game_title_alias(aliases.substr(0, end)), row);
            if (end == std::string_view::npos) break;
            aliases.remove_prefix(end + 1);
        }
    }
}

GameTitleMatch GameTitleIndex::lookup(const std::uint8_t* payload_sha256, std::string_view fallback_name) const {
    if (payload_sha256 != nullptr) {
        static constexpr char hex[] = "0123456789abcdef";
        std::string key(64, '0');
        for (std::size_t i = 0; i < 32; ++i) {
            key[i * 2] = hex[payload_sha256[i] >> 4];
            key[i * 2 + 1] = hex[payload_sha256[i] & 15];
        }
        const auto found = hashes_.find(key);
        if (found != hashes_.end()) {
            if (found->second != nullptr) return {found->second, 1};
            return {}; // A conflicting exact hash cannot be repaired by a weaker name.
        }
    }
    const auto found = aliases_.find(normalize_game_title_alias(fallback_name));
    return found != aliases_.end() && found->second != nullptr ? GameTitleMatch{found->second, 2} : GameTitleMatch{};
}
const GameTitleIndex& game_title_index() {
    static const GameTitleRecord records[] = {
#define FLYNES_GAME_TITLE(hash, id, en, zh, aliases) {hash, id, en, zh, aliases},
#include "../../data/game_titles.inc"
#undef FLYNES_GAME_TITLE
    };
    static const GameTitleIndex index(records, std::size(records));
    return index;
}
}
