#ifndef FLYNES_IOS_PORTABILITY_SMOKE_HPP
#define FLYNES_IOS_PORTABILITY_SMOKE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace flynes::ios {

struct PortabilitySmokeInput final
{
    std::string_view data_root;
    std::string_view cache_root;
    const std::uint8_t* rom_bytes = nullptr;
    std::size_t rom_size = 0u;
};

struct PortabilitySmokeResult final
{
    int exit_code = 1;
    std::uint32_t frames_run = 0u;
    std::uint32_t audio_samples = 0u;
    std::size_t state_bytes = 0u;
    std::uint64_t input_generation = 0u;
    std::uint64_t input_monotonic_ns = 0u;
    std::uint64_t video_sequence = 0u;
    std::uint64_t video_monotonic_ns = 0u;
    std::uint64_t non_black_pixels = 0u;
    std::uint64_t catalog_generation = 0u;
    std::uint64_t catalog_count = 0u;
    std::string full_file_sha256;
    std::string core_cartridge_sha1;
    std::string report;
};

/**
 * Test-private, synchronous portability probe. This is deliberately not part
 * of the public flynes_app or nes C ABIs and must not become a runtime API.
 */
PortabilitySmokeResult run_portability_smoke(const PortabilitySmokeInput& input);

} // namespace flynes::ios

#endif // FLYNES_IOS_PORTABILITY_SMOKE_HPP
