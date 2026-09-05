#ifndef FLYNES_APP_CATALOG_PERSIST_HPP
#define FLYNES_APP_CATALOG_PERSIST_HPP

#include "app/catalog_state.hpp"

#include <memory>
#include <string>

namespace flynes::app {

inline constexpr std::uint64_t FLYCAT01_MAX_BYTES = UINT64_C(16777216);

std::shared_ptr<CatalogData> load_catalog(const std::string& data_root_utf8);
bool save_catalog(const std::string& data_root_utf8, const CatalogData& catalog);

} // namespace flynes::app

#endif
