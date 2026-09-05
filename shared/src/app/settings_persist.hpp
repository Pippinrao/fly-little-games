#ifndef FLYNES_APP_SETTINGS_PERSIST_HPP
#define FLYNES_APP_SETTINGS_PERSIST_HPP

#include "app/catalog_state.hpp"

#include <string>

namespace flynes::app {

SettingsData load_settings(const std::string& data_root_utf8);
bool save_settings(const std::string& data_root_utf8, const SettingsData& settings);

} // namespace flynes::app

#endif
