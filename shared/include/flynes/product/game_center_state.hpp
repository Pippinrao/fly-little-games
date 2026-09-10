#pragma once

#include "flynes/product/game_center_item.hpp"

#include <map>
#include <string>
#include <vector>

namespace flynes::product {

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
                                   const std::string& selected_canonical_id);

    Category category() const { return category_; }
    const std::string& query() const { return query_; }
    std::string selected_canonical_id() const;

    void set_category(Category value) { category_ = value; }
    void set_query(const std::string& value) { query_ = value; }
    void select(const std::string& canonical_id);

    std::vector<GameCenterItem> items_for(Category value,
                                          const std::vector<GameCenterItem>& all) const;
    std::vector<GameCenterItem> filtered(const std::vector<GameCenterItem>& all) const;
    void reconcile(const std::vector<GameCenterItem>& visible);

private:
    Category category_ = Category::All;
    std::string query_;
    std::map<Category, std::string> selections_;
};

} // namespace flynes::product
