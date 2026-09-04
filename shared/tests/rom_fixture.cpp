#include "rom_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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

namespace fs = std::filesystem;

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

[[noreturn]] void fail_manifest(std::size_t line_number,
                                const std::string& case_id,
                                const char* field,
                                const std::string& detail)
{
    std::ostringstream message;
    message << "manifest line " << line_number;
    if (!case_id.empty())
    {
        message << ", case '" << case_id << "'";
    }
    message << ", field '" << field << "': " << detail;
    throw std::runtime_error(message.str());
}

bool is_lower_ascii(char value)
{
    return value >= 'a' && value <= 'z';
}

bool is_decimal_digit(char value)
{
    return value >= '0' && value <= '9';
}

bool is_fixture_identifier(const std::string& value)
{
    if (value.empty() || !is_lower_ascii(value.front()))
    {
        return false;
    }
    for (const char character : value)
    {
        if (!is_lower_ascii(character) && !is_decimal_digit(character) && character != '_')
        {
            return false;
        }
    }
    return true;
}

bool is_safe_blob_basename(const std::string& value)
{
    constexpr const char* extension = ".bin";
    constexpr std::size_t extension_size = 4u;
    return value.size() > extension_size &&
           value.compare(value.size() - extension_size, extension_size, extension) == 0 &&
           is_fixture_identifier(value.substr(0u, value.size() - extension_size));
}

bool is_lower_hex_sha256(const std::string& value)
{
    if (value.size() != 64u)
    {
        return false;
    }
    for (const char character : value)
    {
        const bool valid = is_decimal_digit(character) ||
                           (character >= 'a' && character <= 'f');
        if (!valid)
        {
            return false;
        }
    }
    return true;
}

bool parse_bool(const std::string& value,
                std::size_t line_number,
                const std::string& case_id,
                const char* field)
{
    if (value == "true")
    {
        return true;
    }
    if (value == "false")
    {
        return false;
    }
    fail_manifest(line_number, case_id, field, "expected 'true' or 'false', got '" + value + "'");
}

std::uint64_t parse_u64(const std::string& value,
                        std::size_t line_number,
                        const std::string& case_id,
                        const char* field)
{
    if (value.empty() || (value.size() > 1u && value.front() == '0'))
    {
        fail_manifest(line_number,
                      case_id,
                      field,
                      "expected canonical unsigned decimal, got '" + value + "'");
    }
    std::uint64_t result = 0;
    for (const char character : value)
    {
        if (!is_decimal_digit(character))
        {
            fail_manifest(line_number,
                          case_id,
                          field,
                          "expected canonical unsigned decimal, got '" + value + "'");
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10u)
        {
            fail_manifest(line_number,
                          case_id,
                          field,
                          "unsigned decimal is out of uint64 range: '" + value + "'");
        }
        result = result * 10u + digit;
    }
    return result;
}

std::int32_t parse_mapper(const std::string& value,
                          std::size_t line_number,
                          const std::string& case_id,
                          const char* field)
{
    if (value == "-1")
    {
        return -1;
    }
    const std::uint64_t parsed = parse_u64(value, line_number, case_id, field);
    if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
    {
        fail_manifest(line_number,
                      case_id,
                      field,
                      "mapper value is out of int32 range: '" + value + "'");
    }
    return static_cast<std::int32_t>(parsed);
}

catalog::RomFormat parse_format(const std::string& value,
                                std::size_t line_number,
                                const std::string& case_id)
{
    if (value == "INES") return catalog::RomFormat::INES;
    if (value == "NES2") return catalog::RomFormat::NES2;
    if (value == "FDS") return catalog::RomFormat::FDS;
    if (value == "UNIF") return catalog::RomFormat::UNIF;
    if (value == "UNKNOWN") return catalog::RomFormat::UNKNOWN;
    fail_manifest(line_number, case_id, "format", "unknown value '" + value + "'");
}

catalog::CompatibilityState parse_state(const std::string& value,
                                        std::size_t line_number,
                                        const std::string& case_id)
{
    if (value == "PLAYABLE") return catalog::CompatibilityState::PLAYABLE;
    if (value == "UNSUPPORTED") return catalog::CompatibilityState::UNSUPPORTED;
    if (value == "INVALID") return catalog::CompatibilityState::INVALID;
    if (value == "UNKNOWN") return catalog::CompatibilityState::UNKNOWN;
    fail_manifest(line_number, case_id, "state", "unknown value '" + value + "'");
}

catalog::CompatibilityReason parse_reason(const std::string& value,
                                          std::size_t line_number,
                                          const std::string& case_id)
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
    fail_manifest(line_number, case_id, "reason", "unknown value '" + value + "'");
}

catalog::RomWarning parse_warning(const std::string& value,
                                  std::size_t line_number,
                                  const std::string& case_id)
{
    using Warning = catalog::RomWarning;
    if (value == "TRAILING_DATA") return Warning::TRAILING_DATA;
    if (value == "DIRTY_HEADER") return Warning::DIRTY_HEADER;
    if (value == "UNICODE_PATH_REJECTED") return Warning::UNICODE_PATH_REJECTED;
    fail_manifest(line_number, case_id, "warnings", "unknown value '" + value + "'");
}

std::vector<std::uint8_t> read_blob(const fs::path& path,
                                    std::size_t line_number,
                                    const std::string& case_id)
{
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error || !fs::exists(status))
    {
        fail_manifest(line_number,
                      case_id,
                      "blob",
                      "missing referenced blob '" + path.filename().string() + "'");
    }
    if (fs::is_symlink(status) || !fs::is_regular_file(status))
    {
        fail_manifest(line_number,
                      case_id,
                      "blob",
                      "referenced blob is not a regular non-symlink file: '" +
                          path.filename().string() + "'");
    }

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        fail_manifest(line_number, case_id, "blob", "could not open referenced blob");
    }
    const std::streamoff length = input.tellg();
    if (length < 0 || static_cast<std::uintmax_t>(length) >
                          static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
    {
        fail_manifest(line_number, case_id, "blob", "blob has an unsupported length");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty())
    {
        if (bytes.size() > static_cast<std::size_t>(
                               std::numeric_limits<std::streamsize>::max()))
        {
            fail_manifest(line_number, case_id, "blob", "blob exceeds stream read limits");
        }
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input)
        {
            fail_manifest(line_number, case_id, "blob", "could not read complete blob");
        }
    }
    return bytes;
}

void parse_warnings(const std::string& value,
                    catalog::RomAnalysis& analysis,
                    std::size_t line_number,
                    const std::string& case_id)
{
    if (value == "NONE")
    {
        return;
    }
    const std::vector<std::string> warning_names = split(value, ',');
    if (warning_names.size() > analysis.warnings.size())
    {
        fail_manifest(line_number, case_id, "warnings", "too many warnings");
    }
    for (const std::string& name : warning_names)
    {
        analysis.warnings[analysis.warning_count] =
            parse_warning(name, line_number, case_id);
        ++analysis.warning_count;
    }
}

struct ParsedRow final
{
    RomFixture fixture;
    std::string blob_name;
    std::string expected_sha256;
    std::size_t line_number = 0;
};

ParsedRow parse_fixture_fields(const std::vector<std::string>& fields,
                               std::size_t line_number)
{
    const std::string case_id = fields.size() > 1u ? fields[1] : std::string{};
    if (fields.size() != 18u)
    {
        fail_manifest(line_number,
                      case_id,
                      "row",
                      "expected 18 tab-separated columns, got " +
                          std::to_string(fields.size()));
    }
    if (fields[0] != "1")
    {
        fail_manifest(line_number,
                      case_id,
                      "schema_version",
                      "expected '1', got '" + fields[0] + "'");
    }
    if (!is_fixture_identifier(case_id))
    {
        fail_manifest(line_number,
                      case_id,
                      "case_id",
                      "must match [a-z][a-z0-9_]*, got '" + case_id + "'");
    }
    if (!is_safe_blob_basename(fields[2]))
    {
        fail_manifest(line_number,
                      case_id,
                      "blob",
                      "must be a safe basename matching [a-z][a-z0-9_]*.bin, got '" +
                          fields[2] + "'");
    }
    if (!is_lower_hex_sha256(fields[3]))
    {
        fail_manifest(line_number,
                      case_id,
                      "sha256",
                      "must be exactly 64 lowercase hexadecimal characters");
    }

    ParsedRow row;
    row.fixture.case_id = case_id;
    row.blob_name = fields[2];
    row.expected_sha256 = fields[3];
    row.line_number = line_number;
    row.fixture.expected.recognized = parse_bool(fields[4], line_number, case_id, "recognized");
    row.fixture.expected.format = parse_format(fields[5], line_number, case_id);
    row.fixture.expected.state = parse_state(fields[6], line_number, case_id);
    row.fixture.expected.reason = parse_reason(fields[7], line_number, case_id);
    row.fixture.expected.analysis.expected_bytes =
        parse_u64(fields[8], line_number, case_id, "expected_bytes");
    row.fixture.expected.analysis.actual_bytes =
        parse_u64(fields[9], line_number, case_id, "actual_bytes");
    row.fixture.expected.analysis.prg_bytes =
        parse_u64(fields[10], line_number, case_id, "prg_bytes");
    row.fixture.expected.analysis.chr_bytes =
        parse_u64(fields[11], line_number, case_id, "chr_bytes");
    row.fixture.expected.analysis.mapper =
        parse_mapper(fields[12], line_number, case_id, "mapper");
    row.fixture.expected.analysis.submapper =
        parse_mapper(fields[13], line_number, case_id, "submapper");
    row.fixture.expected.analysis.trainer = parse_bool(fields[14], line_number, case_id, "trainer");
    row.fixture.expected.analysis.battery = parse_bool(fields[15], line_number, case_id, "battery");
    row.fixture.expected.analysis.disk_sides =
        parse_u64(fields[16], line_number, case_id, "disk_sides");
    parse_warnings(fields[17], row.fixture.expected.analysis, line_number, case_id);
    return row;
}

const ParsedRow* find_row_for_blob(const std::vector<ParsedRow>& rows,
                                   const std::string& blob_name)
{
    for (const ParsedRow& row : rows)
    {
        if (row.blob_name == blob_name)
        {
            return &row;
        }
    }
    return nullptr;
}

void validate_corpus_entries(const fs::path& fixture_root,
                             const std::vector<ParsedRow>& rows)
{
    std::unordered_set<std::string> expected_entries;
    for (const ParsedRow& row : rows)
    {
        expected_entries.insert(row.blob_name);
    }
    expected_entries.insert("manifest.tsv");

    std::error_code error;
    fs::directory_iterator iterator(fixture_root, error);
    const fs::directory_iterator end;
    if (error)
    {
        throw std::runtime_error("fixture corpus root could not be enumerated: " +
                                 fixture_root.string());
    }
    while (iterator != end)
    {
        const fs::directory_entry entry = *iterator;
        const std::string name = entry.path().filename().string();
        const ParsedRow* const row = find_row_for_blob(rows, name);
        const fs::file_status status = entry.symlink_status(error);
        if (error)
        {
            if (row != nullptr)
            {
                fail_manifest(row->line_number,
                              row->fixture.case_id,
                              "blob",
                              "could not inspect referenced blob '" + name + "'");
            }
            throw std::runtime_error("fixture corpus entry '" + name +
                                     "': could not inspect entry");
        }
        if (fs::is_symlink(status) || !fs::is_regular_file(status))
        {
            if (row != nullptr)
            {
                fail_manifest(row->line_number,
                              row->fixture.case_id,
                              "blob",
                              "referenced blob is not a regular non-symlink file: '" +
                                  name + "'");
            }
            throw std::runtime_error("fixture corpus entry '" + name +
                                     "': expected a regular file, not a directory or symlink");
        }
        if (expected_entries.erase(name) == 0u)
        {
            throw std::runtime_error("fixture corpus entry '" + name +
                                     "': unexpected file not referenced by manifest");
        }
        iterator.increment(error);
        if (error)
        {
            throw std::runtime_error("fixture corpus root could not be fully enumerated: " +
                                     fixture_root.string());
        }
    }
}

} // namespace

std::vector<RomFixture> load_rom_fixtures(const std::string& fixture_root)
{
    const fs::path root_path(fixture_root);
    std::error_code error;
    const fs::file_status root_status = fs::symlink_status(root_path, error);
    if (error || !fs::is_directory(root_status))
    {
        throw std::runtime_error("fixture corpus root is not a directory: " + fixture_root);
    }
    const fs::path manifest_path = root_path / "manifest.tsv";
    const fs::file_status manifest_status = fs::symlink_status(manifest_path, error);
    if (error || !fs::exists(manifest_status))
    {
        throw std::runtime_error("fixture corpus field 'manifest.tsv': missing manifest file");
    }
    if (fs::is_symlink(manifest_status) || !fs::is_regular_file(manifest_status))
    {
        throw std::runtime_error(
            "fixture corpus field 'manifest.tsv': expected a regular non-symlink file");
    }

    std::ifstream manifest(manifest_path, std::ios::binary);
    if (!manifest)
    {
        throw std::runtime_error("fixture corpus field 'manifest.tsv': could not open manifest");
    }

    std::string line;
    if (!std::getline(manifest, line))
    {
        fail_manifest(1u, {}, "header", "manifest is empty");
    }
    if (!line.empty() && line.back() == '\r')
    {
        line.pop_back();
    }
    if (line != manifest_header)
    {
        fail_manifest(1u, {}, "header", "unexpected manifest header");
    }

    std::vector<ParsedRow> rows;
    std::unordered_set<std::string> case_ids;
    std::unordered_set<std::string> blob_names;
    std::size_t line_number = 1u;
    while (std::getline(manifest, line))
    {
        ++line_number;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty())
        {
            fail_manifest(line_number, {}, "row", "empty manifest row");
        }
        const std::vector<std::string> fields = split(line, '\t');
        ParsedRow row = parse_fixture_fields(fields, line_number);
        if (!case_ids.insert(row.fixture.case_id).second)
        {
            fail_manifest(line_number,
                          row.fixture.case_id,
                          "case_id",
                          "duplicate case ID '" + row.fixture.case_id + "'");
        }
        if (!blob_names.insert(row.blob_name).second)
        {
            fail_manifest(line_number,
                          row.fixture.case_id,
                          "blob",
                          "duplicate blob name '" + row.blob_name + "'");
        }
        rows.push_back(std::move(row));
    }
    if (!manifest.eof())
    {
        throw std::runtime_error("fixture corpus field 'manifest.tsv': read error");
    }
    if (rows.size() != 25u)
    {
        throw std::runtime_error(
            "fixture corpus field 'case count': version one must contain exactly 25 cases");
    }

    validate_corpus_entries(root_path, rows);

    std::vector<RomFixture> fixtures;
    fixtures.reserve(rows.size());
    for (ParsedRow& row : rows)
    {
        row.fixture.payload =
            read_blob(root_path / row.blob_name, row.line_number, row.fixture.case_id);
        if (sha256(row.fixture.payload) != row.expected_sha256)
        {
            fail_manifest(row.line_number,
                          row.fixture.case_id,
                          "sha256",
                          "blob digest does not match manifest value");
        }
        if (row.fixture.payload.size() != row.fixture.expected.analysis.actual_bytes)
        {
            fail_manifest(row.line_number,
                          row.fixture.case_id,
                          "actual_bytes",
                          "blob length does not match manifest value");
        }
        fixtures.push_back(std::move(row.fixture));
    }
    return fixtures;
}

} // namespace flynes::test
