#include "flynes/product/game_center_state.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace flynes::product {
namespace {

std::string trim(std::string_view value)
{
    std::size_t start = 0;
    while (start < value.size()
           && std::isspace(static_cast<unsigned char>(value[start])) != 0)
    {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
    {
        --end;
    }

    return std::string(value.substr(start, end - start));
}

bool is_blank(std::string_view value)
{
    return trim(value).empty();
}

std::string normalize(std::string_view value)
{
    const std::string trimmed = trim(value);
    std::string result;
    result.reserve(trimmed.size());
    for (const unsigned char ch : trimmed)
    {
        if (ch >= 'A' && ch <= 'Z')
        {
            result.push_back(static_cast<char>(ch - 'A' + 'a'));
        }
        else
        {
            result.push_back(static_cast<char>(ch));
        }
    }
    return result;
}

bool contains(std::string_view value, std::string_view needle)
{
    return normalize(value).find(needle) != std::string::npos;
}

GameCenterState::Category category_from_name(std::string_view category_name)
{
    if (category_name == "RECENT")
    {
        return GameCenterState::Category::Recent;
    }
    if (category_name == "FAVORITES")
    {
        return GameCenterState::Category::Favorites;
    }
    if (category_name == "ALL")
    {
        return GameCenterState::Category::All;
    }
    if (category_name == "BUILTIN")
    {
        return GameCenterState::Category::Builtin;
    }
    return GameCenterState::Category::All;
}

} // namespace

GameCenterState GameCenterState::restore(const std::string& category_name,
                                         const std::string& query,
                                         const std::string& selected_canonical_id)
{
    GameCenterState state;
    state.category_ = category_from_name(category_name);
    state.query_ = query;
    if (!is_blank(selected_canonical_id))
    {
        state.selections_[state.category_] = selected_canonical_id;
    }
    return state;
}

std::string GameCenterState::selected_canonical_id() const
{
    const auto found = selections_.find(category_);
    if (found == selections_.end())
    {
        return {};
    }
    return found->second;
}

void GameCenterState::select(const std::string& canonical_id)
{
    if (is_blank(canonical_id))
    {
        selections_.erase(category_);
        return;
    }
    selections_[category_] = canonical_id;
}

std::vector<GameCenterItem> GameCenterState::items_for(
    Category value,
    const std::vector<GameCenterItem>& all) const
{
    std::vector<GameCenterItem> result;
    for (const GameCenterItem& item : all)
    {
        bool include = false;
        switch (value)
        {
        case Category::Recent:
            include = item.last_played_sequence > 0;
            break;
        case Category::Favorites:
            include = item.favorite;
            break;
        case Category::All:
            include = true;
            break;
        case Category::Builtin:
            include = item.builtin;
            break;
        }
        if (include)
        {
            result.push_back(item);
        }
    }

    if (value == Category::Recent)
    {
        std::sort(result.begin(),
                  result.end(),
                  [](const GameCenterItem& left, const GameCenterItem& right) {
                      return left.last_played_sequence > right.last_played_sequence;
                  });
    }

    return result;
}

std::vector<GameCenterItem> GameCenterState::filtered(
    const std::vector<GameCenterItem>& all) const
{
    const std::vector<GameCenterItem> category_items = items_for(category_, all);
    const std::string needle = normalize(query_);
    if (needle.empty())
    {
        return category_items;
    }

    std::vector<GameCenterItem> result;
    for (const GameCenterItem& item : category_items)
    {
        if (contains(item.title_en, needle) || contains(item.title_zh_hans, needle)
            || contains(item.original_filename, needle))
        {
            result.push_back(item);
        }
    }
    return result;
}

void GameCenterState::reconcile(const std::vector<GameCenterItem>& visible)
{
    const std::string selected = selected_canonical_id();
    bool exists = false;
    for (const GameCenterItem& item : visible)
    {
        exists = exists || item.canonical_id == selected;
    }
    if (!exists)
    {
        select(visible.empty() ? std::string{} : visible.front().canonical_id);
    }
}

} // namespace flynes::product
