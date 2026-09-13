#include "flynes/product/game_center_state.hpp"

#include <cstdio>
#include <algorithm>
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

void check_items_equal(const std::vector<flynes::product::GameCenterItem>& left,
                       const std::vector<flynes::product::GameCenterItem>& right,
                       const char* message)
{
    check(left.size() == right.size(), message);
    if (left.size() != right.size())
    {
        return;
    }
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        check(left[index].canonical_id == right[index].canonical_id, message);
    }
}

flynes::product::GameCenterItem make_builtin()
{
    return flynes::product::GameCenterItem{
        "builtin", "From Below", "", true, false, 0, "from_below.nes"};
}

flynes::product::GameCenterItem make_favorite()
{
    return flynes::product::GameCenterItem{
        "mario", "Super Mario Bros.", "超级马里奥", false, true, 7, "mario.nes"};
}

flynes::product::GameCenterItem make_other()
{
    return flynes::product::GameCenterItem{
        "contra", "Contra", "魂斗罗", false, false, 0, "contra.zip"};
}

void test_filters_and_search()
{
    using flynes::product::GameCenterItem;
    using flynes::product::GameCenterState;

    const GameCenterItem builtin = make_builtin();
    const GameCenterItem favorite = make_favorite();
    const GameCenterItem other = make_other();
    const std::vector<GameCenterItem> all{builtin, favorite, other};

    GameCenterState state;
    check(state.items_for(GameCenterState::Category::Recent, all).size() == 1, "recent");
    check(state.items_for(GameCenterState::Category::Favorites, all).size() == 1, "fav");
    check(state.items_for(GameCenterState::Category::All, all).size() == 3, "all");
    check(state.items_for(GameCenterState::Category::Builtin, all).size() == 1, "builtin");

    state.set_query("魂斗");
    check(state.filtered(all)[0].canonical_id == "contra", "zh search");

    state.set_query("MARIO.NES");
    check(state.filtered(all)[0].canonical_id == "mario", "filename search");
}

void test_selection_per_category()
{
    using flynes::product::GameCenterState;

    GameCenterState state;
    state.select("mario");
    state.set_category(GameCenterState::Category::Builtin);
    state.select("builtin");

    state.set_category(GameCenterState::Category::All);
    check(state.selected_canonical_id() == "mario", "all keeps mario");

    state.set_category(GameCenterState::Category::Builtin);
    check(state.selected_canonical_id() == "builtin", "builtin keeps builtin");
}

void test_restore_reconcile()
{
    using flynes::product::GameCenterItem;
    using flynes::product::GameCenterState;

    GameCenterState state = GameCenterState::restore("ALL", "zelda", "missing");
    const std::vector<GameCenterItem> visible{make_builtin(), make_favorite(), make_other()};

    state.reconcile(visible);

    check(state.selected_canonical_id() == "builtin", "repair selection");
    check(state.query() == "zelda", "keep query");
}

void test_select_preserves_spaced_canonical_id()
{
    using flynes::product::GameCenterState;

    GameCenterState state;
    state.select(" game ");
    check(state.selected_canonical_id() == " game ", "select preserves spaces");
}

void test_restore_preserves_spaced_canonical_id()
{
    using flynes::product::GameCenterState;

    const GameCenterState state = GameCenterState::restore("ALL", "", " game ");
    check(state.selected_canonical_id() == " game ", "restore preserves spaces");
}

void test_rejects_nbsp_only_canonical_id()
{
    using flynes::product::GameCenterItem;

    bool threw = false;
    try
    {
        const std::string nbsp_only = "\xC2\xA0";
        const GameCenterItem item{nbsp_only, "Title", "", false, false, 0, "file.nes"};
        (void)item;
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "rejects nbsp-only canonical id");
}

void test_continuous_scrolling_returns_entire_filtered_library()
{
    using flynes::product::GameCenterItem;
    using flynes::product::GameCenterState;

    GameCenterState state;
    state.set_category(GameCenterState::Category::All);
    const std::vector<GameCenterItem> items{make_builtin(), make_favorite(), make_other()};

    check_items_equal({make_other(), make_favorite(), make_builtin()}, state.filtered(items),
                      "filtered all returns entire library in popularity order");

    state.reconcile(items);
    check(state.selected_canonical_id() == "builtin", "reconcile selects first visible");
}

void test_popularity_and_content_deduplication()
{
    using flynes::product::GameCenterItem;
    using flynes::product::GameCenterState;
    GameCenterState state;
    const GameCenterItem translated{"contra-zh", "", "魂斗罗", false, false, 0, "translated.zip"};
    const GameCenterItem sequel{"contra2", "Contra II", "", false, false, 0, "sequel.nes"};
    const GameCenterItem lower{"donkey", "", "", false, false, 0, "DONKEY KONG.nes"};
    std::vector<GameCenterItem> input{make_builtin(), lower, sequel, make_favorite(),
                                    translated, make_other(), make_other()};
    const std::vector<GameCenterItem> expected{make_other(), translated, sequel,
                                             make_favorite(), lower, make_builtin()};
    check_items_equal(expected, state.filtered(input), "popularity and exact content dedup");
    std::reverse(input.begin(), input.end());
    check_items_equal(expected, state.filtered(input), "independent of scan order");
    state.set_query("contra");
    check_items_equal({make_other(), sequel}, state.filtered(input), "search keeps order and dedup");
}

} // namespace

int main()
{
    check(flynes::product::popularity_for_package("collection.zip", "NES/Contra.nes") == 100,
          "rank nested ZIP entry name");
    check(flynes::product::popularity_for_package("魂斗罗.zip", "game.nes") == 100,
          "rank outer ZIP name");
    check(flynes::product::popularity_for_package("Mario collection/Metroid.nes", "") == 98,
          "exclude outer directory name");
    check(flynes::product::popularity_for_package("Mario collection/game.zip", "Contra folder/game.nes") == 0,
          "exclude inner directory name");
    test_popularity_and_content_deduplication();
    test_filters_and_search();
    test_selection_per_category();
    test_restore_reconcile();
    test_select_preserves_spaced_canonical_id();
    test_restore_preserves_spaced_canonical_id();
    test_rejects_nbsp_only_canonical_id();
    test_continuous_scrolling_returns_entire_filtered_library();

    if (failures == 0)
    {
        std::puts("flynes_product_game_center_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
