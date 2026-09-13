#pragma once
#include <string_view>

#include "flynes/product/game_center_item.hpp"
#include "flynes/product/multiplayer_eligibility.hpp"

#include <map>
#include <string>
#include <vector>

namespace flynes::product {

// Use leaf filenames from every content-equivalent variant; never search aliases/directories.
[[nodiscard]] int popularity_for_package(std::string_view outer_filename, std::string_view entry_path);

class GameCenterState final
{
public:
    enum class Category
    {
        Recent,
        Favorites,
        All,
        Builtin,
    };

    GameCenterState() = default;

    static GameCenterState restore(const std::string& category_name,
                                   const std::string& query,
                                   const std::string& selected_canonical_id,
                                   bool multiplayer_only = false);

    Category category() const { return category_; }
    const std::string& query() const { return query_; }
    std::string selected_canonical_id() const;

    // Independent two-player filter (design U04): persisted per device, never
    // changed by category, query, or connection events.
    void set_multiplayer_only(bool value) noexcept { multiplayer_only_ = value; }
    bool multiplayer_only() const noexcept { return multiplayer_only_; }

    void set_category(Category value) { category_ = value; }
    void set_query(const std::string& value) { query_ = value; }
    void select(const std::string& canonical_id);

    std::vector<GameCenterItem> items_for(Category value,
                                          const std::vector<GameCenterItem>& all) const;
    std::vector<GameCenterItem> filtered(const std::vector<GameCenterItem>& all) const;
    // Applies the two-player filter AFTER the existing category/search filter
    // and sort, preserving the relative order of the surviving items. UNKNOWN
    // and UNSUPPORTED games drop out only while the filter is on.
    std::vector<GameCenterItem> filtered(const std::vector<GameCenterItem>& all,
                                         const MultiplayerCapabilityRegistry& capabilities) const;
    void reconcile(const std::vector<GameCenterItem>& visible);

private:
    Category category_ = Category::All;
    std::string query_;
    bool multiplayer_only_ = false;
    std::map<Category, std::string> selections_;
};

} // namespace flynes::product
