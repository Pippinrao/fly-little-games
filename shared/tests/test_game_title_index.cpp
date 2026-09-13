#include <flynes/catalog/game_title_index.hpp>
#include <flynes/flynes_app.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
}

int main() {
    using namespace flynes::catalog;
    const GameTitleRecord rows[] = {
        {"0000000000000000000000000000000000000000000000000000000000000000", "alpha", "Alpha Game", "甲游戏", "Alpha Game\nAlpha\nShared"},
        {"1111111111111111111111111111111111111111111111111111111111111111", "beta", "Beta", "乙游戏", "Beta\nShared"},
        {"2222222222222222222222222222222222222222222222222222222222222222", "alpha", "Alpha Game", "甲游戏", "Alpha Game\nAlpha"},
    };
    GameTitleIndex index(rows, 3);
    check(normalize_game_title_alias("  ALPHA__Game - (USA).NES  ") == "alpha game", "normalize ASCII separators, extension and known region");
    check(normalize_game_title_alias("甲游戏.nes") == "甲游戏", "preserve UTF-8 Chinese aliases");
    check(normalize_game_title_alias("folder/Alpha (JUE).nes") == "alpha", "normalize explicit combined region code on nested member");
    check(normalize_game_title_alias("Alpha (Japan) [T+Eng].nes") == "alpha (japan) [t+eng]", "preserve translation suffix and adjacent metadata conservatively");
    std::array<std::uint8_t, 32> hash{};
    auto match = index.lookup(hash.data(), "Beta.nes");
    check(match.record && std::strcmp(match.record->index_id, "alpha") == 0 && match.match_kind == 1, "content hash wins over misleading alias");
    match = index.lookup(nullptr, "ALPHA_GAME (Europe).nes");
    check(match.record && match.match_kind == 2 && std::strcmp(match.record->title_zh_hans, "甲游戏") == 0, "regional equivalent hashes share bilingual alias");
    check(index.lookup(nullptr, "Shared.nes").record == nullptr, "conflicting explicit alias remains unknown");
    for (const char* name : {"Alpha 2.nes", "Alpha (Hack).nes", "Alpha [T+Eng].nes", "Alpha (Rev 1).nes", "Unknown.nes"})
        check(index.lookup(nullptr, name).record == nullptr, "unknown sequel and modified variants never collapse to base title");
    hash.fill(0xFF);
    check(index.lookup(hash.data(), "Alpha.nes").match_kind == 2, "unindexed hash can use explicit filename alias");
    check(index.lookup(nullptr, "").record == nullptr, "empty lookup remains unknown");
    fly_game_title output{"sentinel", "sentinel", "sentinel", "sentinel", 99};
    check(fly_game_title_resolve(nullptr, nullptr, 1, &output) == FLY_RESULT_INVALID_ARGUMENT && output.match_kind == 99,
          "invalid null name length preserves caller output");
    check(fly_game_title_resolve(nullptr, "x", 1, nullptr) == FLY_RESULT_INVALID_ARGUMENT,
          "null output rejected");
    const auto construction_start = std::chrono::steady_clock::now();
    check(fly_game_title_resolve(nullptr, nullptr, 0, &output) == FLY_RESULT_OK && output.match_kind == 0 &&
              std::strcmp(output.index_id_utf8, "") == 0 && std::strcmp(output.title_en_utf8, "") == 0 &&
              std::strcmp(output.title_zh_hans_utf8, "") == 0 && std::strcmp(output.aliases_utf8, "") == 0,
          "unknown result succeeds with four stable empty strings");
    const auto construction_end = std::chrono::steady_clock::now();
    const char bounded[] = "Balloon FightX";
    check(fly_game_title_resolve(nullptr, bounded, 13, &output) == FLY_RESULT_OK && output.match_kind == 2 &&
              std::strcmp(output.title_zh_hans_utf8, "气球战士(气球大战)") == 0,
          "public C API respects length without needing a NUL terminator");
    static const GameTitleRecord corpus[] = {
#define FLYNES_GAME_TITLE(hash_hex, id, en, zh, aliases) {hash_hex, id, en, zh, aliases},
#include "../data/game_titles.inc"
#undef FLYNES_GAME_TITLE
    };
    check(std::size(corpus) >= 2000, "production title index contains corpus rather than a tiny sample");
    std::vector<std::array<std::uint8_t, 32>> corpus_hashes;
    corpus_hashes.reserve(std::size(corpus));
    for (const auto& row : corpus) {
        for (std::size_t i = 0; i < hash.size(); ++i) {
            const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
            hash[i] = static_cast<std::uint8_t>(nibble(row.payload_sha256_hex[i * 2]) * 16 +
                                               nibble(row.payload_sha256_hex[i * 2 + 1]));
        }
        corpus_hashes.push_back(hash);
    }
    const auto traversal_start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < std::size(corpus); ++i) {
        const auto& row = corpus[i];
        check(fly_game_title_resolve(corpus_hashes[i].data(), "Wrong filename.nes", 18, &output) == FLY_RESULT_OK &&
                  output.match_kind == 1 && std::strcmp(output.index_id_utf8, row.index_id) == 0 &&
                  std::strcmp(output.title_en_utf8, row.title_en) == 0 &&
                  std::strcmp(output.title_zh_hans_utf8, row.title_zh_hans) == 0 &&
                  std::strcmp(output.aliases_utf8, row.aliases) == 0,
              "every corpus hash returns its bilingual metadata regardless of filename");
    }
    const auto traversal_end = std::chrono::steady_clock::now();
    // Repeated rows model list rebinding with either locale selected. Locale
    // switches require no new resolver state: both returned titles stay present.
    for (int pass = 0; pass < 20; ++pass) {
        const char* name = pass % 2 == 0 ? "Completely unrelated.nes" : "无关的文件名.nes";
        for (std::size_t i = 0; i < std::size(corpus); ++i) {
            const auto& row = corpus[i];
            check(fly_game_title_resolve(corpus_hashes[i].data(), name,
                      static_cast<std::uint32_t>(std::strlen(name)), &output) == FLY_RESULT_OK &&
                      output.match_kind == 1 && std::strcmp(output.title_en_utf8, row.title_en) == 0 &&
                      std::strcmp(output.title_zh_hans_utf8, row.title_zh_hans) == 0,
                  "repeated locale-independent hash lookups retain both indexed titles");
        }
    }
    const auto repeated_end = std::chrono::steady_clock::now();
    const auto milliseconds = [](auto start, auto end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    };
    std::printf("title index host timing: construct=%.3f ms; %zu hashes=%.3f ms; %zu repeated hashes=%.3f ms\n",
        milliseconds(construction_start, construction_end), std::size(corpus),
        milliseconds(traversal_start, traversal_end), std::size(corpus) * 20,
        milliseconds(traversal_end, repeated_end));
    return failures == 0 ? 0 : 1;
}
