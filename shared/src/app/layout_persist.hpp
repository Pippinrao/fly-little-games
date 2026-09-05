#ifndef FLYNES_APP_LAYOUT_PERSIST_HPP
#define FLYNES_APP_LAYOUT_PERSIST_HPP

#include <string>

namespace flynes::app {

std::string load_control_layout(const std::string& data_root_utf8);
bool save_control_layout(const std::string& data_root_utf8, const std::string& encoded_utf8);

} // namespace flynes::app

#endif
