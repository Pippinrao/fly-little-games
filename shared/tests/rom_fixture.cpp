#include "rom_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace flynes::test {
namespace {

constexpr const char* manifest_header =
    "schema_version\tcase_id\tblob\tsha256\trecognized\tformat\tstate\treason\t"
    "expected_bytes\tactual_bytes\tprg_bytes\tchr_bytes\tmapper\tsubmapper\ttrainer\t"
    "battery\tdisk_sides\twarnings";

constexpr std::array<std::uint32_t, 64> sha256_round_constants = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u,
};

std::uint32_t rotate_right(std::uint32_t value, unsigned int bits)
{
    return (value >> bits) | (value << (32u - bits));
}

std::string sha256(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() > std::numeric_limits<std::uint64_t>::max() / 8u)
    {
        throw std::runtime_error("fixture is too large for SHA-256 length encoding");
    }
    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8u;
    std::vector<std::uint8_t> padded = bytes;
    padded.push_back(0x80u);
    while (padded.size() % 64u != 56u)
    {
        padded.push_back(0u);
    }
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        padded.push_back(static_cast<std::uint8_t>(bit_length >> shift));
    }

    std::array<std::uint32_t, 8> hash = {
        0x6A09E667u,
        0xBB67AE85u,
        0x3C6EF372u,
        0xA54FF53Au,
        0x510E527Fu,
        0x9B05688Cu,
        0x1F83D9ABu,
        0x5BE0CD19u,
    };
    for (std::size_t chunk = 0; chunk < padded.size(); chunk += 64u)
    {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16u; ++index)
        {
            const std::size_t offset = chunk + index * 4u;
            words[index] =
                (static_cast<std::uint32_t>(padded[offset]) << 24u) |
                (static_cast<std::uint32_t>(padded[offset + 1u]) << 16u) |
                (static_cast<std::uint32_t>(padded[offset + 2u]) << 8u) |
                static_cast<std::uint32_t>(padded[offset + 3u]);
        }
        for (std::size_t index = 16u; index < words.size(); ++index)
        {
            const std::uint32_t first =
                rotate_right(words[index - 15u], 7u) ^
                rotate_right(words[index - 15u], 18u) ^
                (words[index - 15u] >> 3u);
            const std::uint32_t second =
                rotate_right(words[index - 2u], 17u) ^
                rotate_right(words[index - 2u], 19u) ^
                (words[index - 2u] >> 10u);
            words[index] = words[index - 16u] + first + words[index - 7u] + second;
        }

        std::uint32_t a = hash[0];
        std::uint32_t b = hash[1];
        std::uint32_t c = hash[2];
        std::uint32_t d = hash[3];
        std::uint32_t e = hash[4];
        std::uint32_t f = hash[5];
        std::uint32_t g = hash[6];
        std::uint32_t h = hash[7];
        for (std::size_t index = 0; index < words.size(); ++index)
        {
            const std::uint32_t sum_one =
                rotate_right(e, 6u) ^ rotate_right(e, 11u) ^ rotate_right(e, 25u);
            const std::uint32_t choice = (e & f) ^ (~e & g);
            const std::uint32_t temporary_one =
                h + sum_one + choice + sha256_round_constants[index] + words[index];
            const std::uint32_t sum_zero =
                rotate_right(a, 2u) ^ rotate_right(a, 13u) ^ rotate_right(a, 22u);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temporary_two = sum_zero + majority;

            h = g;
            g = f;
            f = e;
            e = d + temporary_one;
            d = c;
            c = b;
            b = a;
            a = temporary_one + temporary_two;
        }
        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
        hash[5] += f;
        hash[6] += g;
        hash[7] += h;
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const std::uint32_t value : hash)
    {
        output << std::setw(8) << value;
    }
    return output.str();
}

std::vector<std::string> split(const std::string& value, char delimiter)
{
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true)
    {
        const std::size_t end = value.find(delimiter, start);
        fields.push_back(value.substr(start, end - start));
        if (end == std::string::npos)
        {
            return fields;
        }
        start = end + 1u;
    }
}

bool parse_bool(const std::string& value)
{
    if (value == "true")
    {
        return true;
    }
    if (value == "false")
    {
        return false;
    }
    throw std::runtime_error("invalid fixture boolean: " + value);
}

std::uint64_t parse_u64(const std::string& value)
{
    std::size_t consumed = 0;
    const unsigned long long parsed = std::stoull(value, &consumed);
    if (consumed != value.size())
    {
        throw std::runtime_error("invalid unsigned fixture integer: " + value);
    }
    return static_cast<std::uint64_t>(parsed);
}

std::int32_t parse_i32(const std::string& value)
{
    std::size_t consumed = 0;
    const long parsed = std::stol(value, &consumed);
    if (consumed != value.size() || parsed < std::numeric_limits<std::int32_t>::min() ||
        parsed > std::numeric_limits<std::int32_t>::max())
    {
        throw std::runtime_error("invalid signed fixture integer: " + value);
    }
    return static_cast<std::int32_t>(parsed);
}

catalog::RomFormat parse_format(const std::string& value)
{
    if (value == "INES") return catalog::RomFormat::INES;
    if (value == "NES2") return catalog::RomFormat::NES2;
    if (value == "FDS") return catalog::RomFormat::FDS;
    if (value == "UNIF") return catalog::RomFormat::UNIF;
    if (value == "UNKNOWN") return catalog::RomFormat::UNKNOWN;
    throw std::runtime_error("invalid ROM format: " + value);
}

catalog::CompatibilityState parse_state(const std::string& value)
{
    if (value == "PLAYABLE") return catalog::CompatibilityState::PLAYABLE;
    if (value == "UNSUPPORTED") return catalog::CompatibilityState::UNSUPPORTED;
    if (value == "INVALID") return catalog::CompatibilityState::INVALID;
    if (value == "UNKNOWN") return catalog::CompatibilityState::UNKNOWN;
    throw std::runtime_error("invalid compatibility state: " + value);
}

catalog::CompatibilityReason parse_reason(const std::string& value)
{
    using Reason = catalog::CompatibilityReason;
    if (value == "PLAYABLE_NES") return Reason::PLAYABLE_NES;
    if (value == "FDS_BIOS_API_NOT_IMPLEMENTED") return Reason::FDS_BIOS_API_NOT_IMPLEMENTED;
    if (value == "UNIF_PRODUCT_DISABLED") return Reason::UNIF_PRODUCT_DISABLED;
    if (value == "NES_HEADER_INVALID") return Reason::NES_HEADER_INVALID;
    if (value == "NES_ZERO_PRG") return Reason::NES_ZERO_PRG;
    if (value == "NES_TRUNCATED") return Reason::NES_TRUNCATED;
    if (value == "NES_SIZE_OVERFLOW") return Reason::NES_SIZE_OVERFLOW;
    if (value == "FDS_INVALID_HEADER") return Reason::FDS_INVALID_HEADER;
    if (value == "FDS_INVALID_SIDE_COUNT") return Reason::FDS_INVALID_SIDE_COUNT;
    if (value == "FDS_TRUNCATED") return Reason::FDS_TRUNCATED;
    if (value == "UNIF_INVALID_CHUNK") return Reason::UNIF_INVALID_CHUNK;
    if (value == "UNIF_MISSING_PRG") return Reason::UNIF_MISSING_PRG;
    if (value == "UNKNOWN_FORMAT") return Reason::UNKNOWN_FORMAT;
    throw std::runtime_error("invalid compatibility reason: " + value);
}

catalog::RomWarning parse_warning(const std::string& value)
{
    using Warning = catalog::RomWarning;
    if (value == "TRAILING_DATA") return Warning::TRAILING_DATA;
    if (value == "DIRTY_HEADER") return Warning::DIRTY_HEADER;
    if (value == "UNICODE_PATH_REJECTED") return Warning::UNICODE_PATH_REJECTED;
    throw std::runtime_error("invalid ROM warning: " + value);
}

std::vector<std::uint8_t> read_blob(const std::string& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        throw std::runtime_error("could not open fixture blob: " + path);
    }
    const std::streamoff length = input.tellg();
    if (length < 0 || static_cast<std::uintmax_t>(length) >
                          static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
    {
        throw std::runtime_error("fixture blob has an unsupported length: " + path);
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty())
    {
        if (bytes.size() > static_cast<std::size_t>(
                               std::numeric_limits<std::streamsize>::max()))
        {
            throw std::runtime_error("fixture blob exceeds stream read limits: " + path);
        }
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input)
        {
            throw std::runtime_error("could not read complete fixture blob: " + path);
        }
    }
    return bytes;
}

void parse_warnings(const std::string& value, catalog::RomAnalysis& analysis)
{
    if (value == "NONE")
    {
        return;
    }
    const std::vector<std::string> warning_names = split(value, ',');
    if (warning_names.size() > analysis.warnings.size())
    {
        throw std::runtime_error("fixture has too many ROM warnings");
    }
    for (const std::string& name : warning_names)
    {
        analysis.warnings[analysis.warning_count] = parse_warning(name);
        ++analysis.warning_count;
    }
}

RomFixture parse_fixture(const std::vector<std::string>& fields,
                         const std::string& fixture_root)
{
    if (fields.size() != 18u)
    {
        throw std::runtime_error("fixture manifest row does not have 18 columns");
    }
    if (fields[0] != "1")
    {
        throw std::runtime_error("unsupported ROM fixture schema: " + fields[0]);
    }

    RomFixture fixture;
    fixture.case_id = fields[1];
    fixture.payload = read_blob(fixture_root + "/" + fields[2]);
    if (sha256(fixture.payload) != fields[3])
    {
        throw std::runtime_error("SHA-256 mismatch for fixture " + fixture.case_id);
    }

    fixture.expected.recognized = parse_bool(fields[4]);
    fixture.expected.format = parse_format(fields[5]);
    fixture.expected.state = parse_state(fields[6]);
    fixture.expected.reason = parse_reason(fields[7]);
    fixture.expected.analysis.expected_bytes = parse_u64(fields[8]);
    fixture.expected.analysis.actual_bytes = parse_u64(fields[9]);
    fixture.expected.analysis.prg_bytes = parse_u64(fields[10]);
    fixture.expected.analysis.chr_bytes = parse_u64(fields[11]);
    fixture.expected.analysis.mapper = parse_i32(fields[12]);
    fixture.expected.analysis.submapper = parse_i32(fields[13]);
    fixture.expected.analysis.trainer = parse_bool(fields[14]);
    fixture.expected.analysis.battery = parse_bool(fields[15]);
    fixture.expected.analysis.disk_sides = parse_i32(fields[16]);
    parse_warnings(fields[17], fixture.expected.analysis);
    if (fixture.payload.size() != fixture.expected.analysis.actual_bytes)
    {
        throw std::runtime_error("actual byte count mismatch for fixture " + fixture.case_id);
    }
    return fixture;
}

} // namespace

std::vector<RomFixture> load_rom_fixtures(const std::string& fixture_root)
{
    std::ifstream manifest(fixture_root + "/manifest.tsv", std::ios::binary);
    if (!manifest)
    {
        throw std::runtime_error("could not open ROM fixture manifest");
    }

    std::string line;
    if (!std::getline(manifest, line))
    {
        throw std::runtime_error("ROM fixture manifest is empty");
    }
    if (!line.empty() && line.back() == '\r')
    {
        line.pop_back();
    }
    if (line != manifest_header)
    {
        throw std::runtime_error("unexpected ROM fixture manifest header");
    }

    std::vector<RomFixture> fixtures;
    std::unordered_set<std::string> case_ids;
    std::unordered_set<std::string> blob_names;
    while (std::getline(manifest, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty())
        {
            throw std::runtime_error("empty row in ROM fixture manifest");
        }
        const std::vector<std::string> fields = split(line, '\t');
        if (fields.size() < 3u || !case_ids.insert(fields[1]).second ||
            !blob_names.insert(fields[2]).second)
        {
            throw std::runtime_error("duplicate or malformed ROM fixture row");
        }
        fixtures.push_back(parse_fixture(fields, fixture_root));
    }
    if (!manifest.eof())
    {
        throw std::runtime_error("error while reading ROM fixture manifest");
    }
    if (fixtures.size() != 25u)
    {
        throw std::runtime_error("version-one ROM fixture manifest must have 25 cases");
    }
    return fixtures;
}

} // namespace flynes::test
