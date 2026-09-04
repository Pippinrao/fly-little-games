#ifndef FLYNES_TESTS_ROM_FIXTURE_HPP
#define FLYNES_TESTS_ROM_FIXTURE_HPP

#include "catalog/rom_payload_parser.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace flynes::test {

struct RomFixture final
{
    std::string case_id;
    std::vector<std::uint8_t> payload;
    catalog::RomParseResult expected;
};

std::vector<RomFixture> load_rom_fixtures(const std::string& fixture_root);

} // namespace flynes::test

#endif
