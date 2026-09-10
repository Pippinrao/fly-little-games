#pragma once

#include <string>
#include <string_view>

namespace flynes::harmony {

struct CatalogSmokeResult final
{
    std::string generation;
    std::string count;
    std::string source_count;
    std::string locale_tag;
};

[[nodiscard]] CatalogSmokeResult run_catalog_smoke(std::string_view data_root,
                                                   std::string_view cache_root);

} // namespace flynes::harmony
