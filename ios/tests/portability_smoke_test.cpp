#include "fixture_sha256.hpp"
#include "portability_smoke.hpp"

#include <nes/nes.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    const auto sha_matches = [](const char* input, std::size_t size, const char* expected) {
        return flynes::ios::fixture_sha256_hex(
                   reinterpret_cast<const std::uint8_t*>(input), size) == expected;
    };
    if (!sha_matches(nullptr,
                     0u,
                     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855") ||
        !sha_matches("abc",
                     3u,
                     "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD") ||
        !sha_matches("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                     56u,
                     "248D6A61D20638B8E5C026930C3E6039A33CE45964FF2167F6ECEDD419DB06C1") ||
        !flynes::ios::fixture_sha256_hex(nullptr, 1u).empty())
    {
        std::cerr << "test-private SHA-256 vectors failed\n";
        return 1;
    }

    if (argc != 2)
    {
        std::cerr << "usage: flynes_ios_portability_smoke_host_test ROM\n";
        return 2;
    }

    std::ifstream stream(argv[1], std::ios::binary);
    if (!stream)
    {
        std::cerr << "could not open the licensed ROM fixture\n";
        return 2;
    }
    const std::vector<std::uint8_t> rom{
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (stream.bad() || rom.empty())
    {
        std::cerr << "could not read the licensed ROM fixture\n";
        return 2;
    }

    const flynes::ios::PortabilitySmokeResult result =
        flynes::ios::run_portability_smoke({
            "ios-stage1-host-data",
            "ios-stage1-host-cache",
            rom.data(),
            rom.size(),
        });
    std::cout << result.report << '\n';

    if (result.exit_code != 0 ||
        result.frames_run != 60u ||
        result.audio_samples == 0u || result.audio_samples > 60u * 1024u ||
        result.audio_samples_changed == 0u ||
        result.audio_samples_changed > result.audio_samples ||
        result.state_bytes == 0u || result.state_bytes > 8u * 1024u * 1024u ||
        result.input_generation != 4u ||
        result.input_pad_bits[0] != NES_BTN_A ||
        result.input_pad_bits[1] != NES_BTN_B ||
        result.input_pad_bits[2] != NES_BTN_SELECT ||
        result.input_pad_bits[3] != NES_BTN_START ||
        result.video_sequence != result.frames_run ||
        result.non_black_pixels == 0u || result.non_black_pixels > 256u * 240u ||
        result.report.find("FLYNES_IOS_SMOKE_PASS") == std::string::npos ||
        result.report.find(
            "full_file_sha256=1A3AC4FAF4B35640505344059AE5D91DAE07CD47E1FB4D9D2A33C76391F1C555") ==
            std::string::npos ||
        result.report.find(
            "core_cartridge_sha1=77C42676DB38D384C1D6B00090ADBC820BF70AB0") ==
            std::string::npos)
    {
        std::cerr << "portability smoke did not return its verified PASS evidence\n";
        return 1;
    }
    return 0;
}
