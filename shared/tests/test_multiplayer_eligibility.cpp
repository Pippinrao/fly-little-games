// Multiplayer eligibility projection + Game Center two-player filter tests
// (plan P3; design 2026-09-13 §3.2, cases C02/C03).
// Matrix: 4 categories x 2 multiplayerOnly x 3 connection states, plus search,
// leading-zero canonical ids, UNKNOWN, empty categories, duplicate canonical
// ids and popularity ties. Connection state is NOT part of this projection:
// the same call sequence with any connection state must yield the same
// canonical ids in the same order (C02 asserts that platform-side, too).

#include "flynes/product/game_center_state.hpp"
#include "flynes/product/multiplayer_eligibility.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

using flynes::product::GameCenterItem;
using flynes::product::GameCenterState;
using flynes::product::MultiplayerCapabilityRegistry;
using flynes::product::MultiplayerEligibility;

GameCenterItem item(const char* id, bool builtin, bool favorite, long played, int popularity)
{
    return GameCenterItem{id, id, "", builtin, favorite, played, std::string(id) + ".nes",
                          popularity};
}

std::vector<std::string> ids(const std::vector<GameCenterItem>& items)
{
    std::vector<std::string> result;
    for (const GameCenterItem& game : items)
    {
        result.push_back(game.canonical_id);
    }
    return result;
}

bool same_relative_order(const std::vector<GameCenterItem>& before,
                         const std::vector<GameCenterItem>& after)
{
    std::vector<std::string> kept;
    for (const GameCenterItem& game : before)
    {
        if (std::find_if(after.begin(), after.end(), [&](const GameCenterItem& candidate) {
                return candidate.canonical_id == game.canonical_id;
            }) != after.end())
        {
            kept.push_back(game.canonical_id);
        }
    }
    return kept == ids(after);
}

MultiplayerCapabilityRegistry registry()
{
    MultiplayerCapabilityRegistry capabilities{7u};
    capabilities.put("dual-builtin", MultiplayerEligibility::Supported, 7u);
    capabilities.put("dual-cart", MultiplayerEligibility::Supported, 7u);
    capabilities.put("solo-builtin", MultiplayerEligibility::Unsupported, 7u);
    capabilities.put("solo-cart", MultiplayerEligibility::Unsupported, 7u);
    // "stale-profile" is deliberately registered under an old version and
    // "unread" is missing entirely: both must project UNKNOWN.
    capabilities.put("stale-profile", MultiplayerEligibility::Supported, 6u);
    return capabilities;
}

std::vector<GameCenterItem> catalog()
{
    return {
        item("solo-cart", false, false, 0, 10),
        item("dual-cart", false, true, 0, 90),
        item("solo-builtin", true, false, 0, 20),
        item("dual-builtin", false, true, 3, 20),
        item("stale-profile", false, true, 0, 5),
        item("unread", false, true, 1, 5),
        item("00puzzle", false, true, 2, 0),  // leading-zero canonical id
    };
}

void test_registry_tri_value()
{
    const MultiplayerCapabilityRegistry capabilities = registry();
    check(capabilities.eligibility_for("dual-builtin") == MultiplayerEligibility::Supported,
          "registered supported game reads Supported");
    check(capabilities.eligibility_for("solo-builtin") == MultiplayerEligibility::Unsupported,
          "registered unsupported game reads Unsupported");
    check(capabilities.eligibility_for("unread") == MultiplayerEligibility::Unknown,
          "unread id projects UNKNOWN, never UNSUPPORTED");
    check(capabilities.eligibility_for("stale-profile") == MultiplayerEligibility::Unknown,
          "version-mismatched entry projects UNKNOWN");
    check(capabilities.eligibility_for("") == MultiplayerEligibility::Unknown,
          "blank id projects UNKNOWN");
}

void test_matrix_filter_on_off_per_category()
{
    const MultiplayerCapabilityRegistry capabilities = registry();
    const std::vector<GameCenterItem> all = catalog();
    const GameCenterState::Category categories[] = {
        GameCenterState::Category::Recent, GameCenterState::Category::Favorites,
        GameCenterState::Category::All, GameCenterState::Category::Builtin};

    for (const GameCenterState::Category category : categories)
    {
        for (const bool filter_on : {false, true})
        {
            GameCenterState state;
            state.set_category(category);
            state.set_multiplayer_only(filter_on);

            const std::vector<GameCenterItem> base = state.filtered(all);
            const std::vector<GameCenterItem> filtered = state.filtered(all, capabilities);

            if (!filter_on)
            {
                check(ids(filtered) == ids(base),
                      "filter off: identical to the existing pipeline");
                continue;
            }
            // Filter on: every survivor is SUPPORTED, order is the base order.
            for (const GameCenterItem& game : filtered)
            {
                check(capabilities.eligibility_for(game.canonical_id)
                          == MultiplayerEligibility::Supported,
                      "filter on: survivors are exactly SUPPORTED games");
            }
            check(same_relative_order(base, filtered),
                  "filter on: relative order of survivors is unchanged (no re-sort)");
        }
    }

    // Concrete intersections.
    GameCenterState favorites;
    favorites.set_category(GameCenterState::Category::Favorites);
    favorites.set_multiplayer_only(true);
    const std::vector<std::string> favorite_duals =
        ids(favorites.filtered(catalog(), registry()));
    check(favorite_duals == std::vector<std::string>({"dual-cart", "dual-builtin"}),
          "C02: favorites x two-player keeps only SUPPORTED favorites in popularity order");

    GameCenterState builtin_solo_only;
    builtin_solo_only.set_category(GameCenterState::Category::Builtin);
    builtin_solo_only.set_multiplayer_only(true);
    check(ids(builtin_solo_only.filtered(catalog(), registry())).empty(),
          "C03: builtin x two-player with only solo builtins is empty");

    // Zero result stays in the category; clearing the switch restores it and
    // changes nothing else.
    check(builtin_solo_only.category() == GameCenterState::Category::Builtin,
          "C03: zero results stay in BUILTIN");
    builtin_solo_only.set_multiplayer_only(false);
    check(ids(builtin_solo_only.filtered(catalog(), registry())).size() == 1,
          "C03: closing the filter restores the builtin list");
    check(builtin_solo_only.multiplayer_only() == false
              && builtin_solo_only.category() == GameCenterState::Category::Builtin,
          "C03: closing the filter clears only the switch");
}

void test_connection_events_do_not_change_projection()
{
    // C02: "connection states" are not an input of this projection at all.
    // The identical call sequence must be idempotent and must not mutate the
    // category/query/filter state.
    const MultiplayerCapabilityRegistry capabilities = registry();
    const std::vector<GameCenterItem> all = catalog();

    GameCenterState state;
    state.set_category(GameCenterState::Category::Favorites);
    state.set_query("cart");
    state.set_multiplayer_only(true);
    const std::vector<std::string> before =
        ids(state.filtered(all, capabilities));

    // Simulated connection/disconnection events: no API to touch, so the only
    // honest assertion is that re-projecting is identical.
    const std::vector<std::string> after = ids(state.filtered(all, capabilities));
    check(before == after, "C02: connection events leave the projection unchanged");
    check(state.category() == GameCenterState::Category::Favorites
              && state.query() == "cart" && state.multiplayer_only(),
          "C02: connection events leave category, query and filter untouched");
}

void test_search_leading_zero_unknown_and_ties()
{
    const MultiplayerCapabilityRegistry capabilities = registry();

    GameCenterState search;
    search.set_category(GameCenterState::Category::Favorites);
    search.set_query("puzzle");
    search.set_multiplayer_only(true);
    // 00puzzle is SUPPORTED? It is not registered: UNKNOWN drops out under the
    // filter even when the search matches.
    check(ids(search.filtered(catalog(), capabilities)).empty(),
          "UNKNOWN games are excluded while the filter is on");

    GameCenterState search_off;
    search_off.set_category(GameCenterState::Category::Favorites);
    search_off.set_query("puzzle");
    check(ids(search_off.filtered(catalog(), capabilities)) ==
              std::vector<std::string>({"00puzzle"}),
          "UNKNOWN games remain visible while the filter is off");
    check(ids(search_off.filtered(catalog(), capabilities)).front() == "00puzzle",
          "C05/C17: leading-zero canonical ids survive search and filter");

    // Duplicate canonical ids collapse to one row regardless of the filter.
    std::vector<GameCenterItem> duplicated = catalog();
    duplicated.push_back(item("dual-cart", false, true, 0, 90));
    GameCenterState all;
    all.set_category(GameCenterState::Category::All);
    all.set_multiplayer_only(true);
    const std::vector<std::string> deduped = ids(all.filtered(duplicated, capabilities));
    check(std::count(deduped.begin(), deduped.end(), std::string("dual-cart")) == 1,
          "duplicate canonical ids stay deduplicated under the filter");

    // Popularity tie (solo-builtin vs dual-builtin, both 20) keeps the
    // canonical-id tiebreak; the filter only removes rows, never reorders.
    GameCenterState ties;
    ties.set_category(GameCenterState::Category::All);
    ties.set_multiplayer_only(false);
    const std::vector<std::string> tied = ids(ties.filtered(catalog()));
    const auto dual = std::find(tied.begin(), tied.end(), std::string("dual-builtin"));
    const auto solo = std::find(tied.begin(), tied.end(), std::string("solo-builtin"));
    check(dual != tied.end() && solo != tied.end() && dual < solo,
          "popularity ties keep the deterministic canonical-id order");
}

void test_selection_memory_and_restore()
{
    const MultiplayerCapabilityRegistry capabilities = registry();
    const std::vector<GameCenterItem> all = catalog();

    GameCenterState state = GameCenterState::restore("FAVORITES", "", "dual-cart", true);
    check(state.multiplayer_only(), "restore picks up the persisted filter");
    check(state.selected_canonical_id() == "dual-cart", "restore keeps the selection");

    // Selecting in other categories is remembered independently; toggling the
    // filter or reconciling an empty result must not clear other categories'
    // memory.
    state.select("00puzzle");
    state.set_multiplayer_only(true);
    state.filtered(all, capabilities);
    state.reconcile(state.filtered(all, capabilities));  // 00puzzle is UNKNOWN -> replaced
    check(state.selected_canonical_id() == "dual-cart",
          "C03: reconcile falls back to the first visible survivor");
    state.set_category(GameCenterState::Category::All);
    state.select("dual-cart");
    state.set_category(GameCenterState::Category::Favorites);
    check(state.selected_canonical_id() == "dual-cart",
          "per-category selection memory survives category switches");
}

} // namespace

int main()
{
    test_registry_tri_value();
    test_matrix_filter_on_off_per_category();
    test_connection_events_do_not_change_projection();
    test_search_leading_zero_unknown_and_ties();
    test_selection_memory_and_restore();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::fprintf(stdout, "test_multiplayer_eligibility: all checks passed\n");
    return 0;
}
