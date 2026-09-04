#include "zip_open_fixture.hpp"

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
    "schema_version\tcase_id\tblob\tsha256\tmax_package_bytes\tmax_payload_bytes\t"
    "max_zip_entries\tmax_cumulative_inflated_bytes\tmax_name_bytes\t"
    "max_compression_ratio\tratio_guard_threshold_bytes\toutcome\terror_code\t"
    "error_message\tentries";

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
        0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
        0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u,
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
    std::size_t start = 0u;
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

bool is_lower(char value)
{
    return value >= 'a' && value <= 'z';
}

bool is_digit(char value)
{
    return value >= '0' && value <= '9';
}

bool is_blank(const std::string& value)
{
    for (const unsigned char character : value)
    {
        const bool whitespace =
            (character >= 0x09u && character <= 0x0Du) ||
            (character >= 0x1Cu && character <= 0x20u);
        if (!whitespace)
        {
            return false;
        }
    }
    return true;
}

bool is_identifier(const std::string& value)
{
    if (value.empty() || !is_lower(value.front()))
    {
        return false;
    }
    for (const char character : value)
    {
        if (!is_lower(character) && !is_digit(character) && character != '_')
        {
            return false;
        }
    }
    return true;
}

bool is_safe_blob(const std::string& case_id, const std::string& value)
{
    return value == case_id + ".zip" && value.find('/') == std::string::npos &&
           value.find('\\') == std::string::npos;
}

bool is_lower_hex(const std::string& value)
{
    if (value.empty())
    {
        return false;
    }
    for (const char character : value)
    {
        if (!is_digit(character) && (character < 'a' || character > 'f'))
        {
            return false;
        }
    }
    return true;
}

std::uint64_t parse_u64(const std::string& value,
                        std::size_t line_number,
                        const std::string& case_id,
                        const char* field)
{
    if (value.empty() || (value.size() > 1u && value.front() == '0'))
    {
        fail_manifest(line_number, case_id, field,
                      "expected canonical unsigned decimal, got '" + value + "'");
    }
    std::uint64_t result = 0u;
    for (const char character : value)
    {
        if (!is_digit(character))
        {
            fail_manifest(line_number, case_id, field,
                          "expected canonical unsigned decimal, got '" + value + "'");
        }
        const auto digit = static_cast<std::uint64_t>(character - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10u)
        {
            fail_manifest(line_number, case_id, field,
                          "unsigned decimal is out of uint64 range: '" + value + "'");
        }
        result = result * 10u + digit;
    }
    return result;
}

template <typename Value>
Value parse_bounded(const std::string& value,
                    std::size_t line_number,
                    const std::string& case_id,
                    const char* field,
                    std::uint64_t minimum,
                    std::uint64_t maximum)
{
    const std::uint64_t parsed = parse_u64(value, line_number, case_id, field);
    if (parsed < minimum || parsed > maximum)
    {
        fail_manifest(line_number, case_id, field, "value is outside the allowed range");
    }
    return static_cast<Value>(parsed);
}

bool parse_bool(const std::string& value,
                std::size_t line_number,
                const std::string& case_id,
                const char* field)
{
    if (value == "true") return true;
    if (value == "false") return false;
    fail_manifest(line_number, case_id, field,
                  "expected 'true' or 'false', got '" + value + "'");
}

std::vector<std::uint8_t> parse_hex(const std::string& value,
                                    std::size_t line_number,
                                    const std::string& case_id,
                                    const char* field)
{
    if (value == "-")
    {
        return {};
    }
    if (!is_lower_hex(value) || value.size() % 2u != 0u)
    {
        fail_manifest(line_number, case_id, field,
                      "expected lowercase whole-byte hexadecimal, got '" + value + "'");
    }
    std::vector<std::uint8_t> result(value.size() / 2u);
    for (std::size_t index = 0u; index < result.size(); ++index)
    {
        const auto nibble = [](char character) -> std::uint8_t {
            return static_cast<std::uint8_t>(
                is_digit(character) ? character - '0' : character - 'a' + 10);
        };
        result[index] = static_cast<std::uint8_t>(
            (nibble(value[index * 2u]) << 4u) | nibble(value[index * 2u + 1u]));
    }
    return result;
}

ZipFixtureEntry parse_entry(const std::string& value,
                            std::size_t line_number,
                            const std::string& case_id,
                            std::size_t entry_index)
{
    const std::vector<std::string> fields = split(value, '|');
    const std::string prefix = "entry_" + std::to_string(entry_index) + ".";
    if (fields.size() != 9u)
    {
        fail_manifest(line_number, case_id, "entries",
                      "entry " + std::to_string(entry_index) +
                          " must contain exactly 9 pipe-separated fields");
    }
    ZipFixtureEntry entry;
    entry.raw_name = parse_hex(fields[0], line_number, case_id,
                               (prefix + "raw_name").c_str());
    if (entry.raw_name.empty())
    {
        fail_manifest(line_number, case_id, "entries", "successful entry name is empty");
    }
    entry.central_extra = parse_hex(fields[1], line_number, case_id,
                                    (prefix + "central_extra").c_str());
    entry.local_header_offset = parse_bounded<std::int32_t>(
        fields[2], line_number, case_id, (prefix + "local_header_offset").c_str(),
        0u, static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()));
    entry.flags = parse_bounded<std::uint16_t>(
        fields[3], line_number, case_id, (prefix + "flags").c_str(), 0u, 0xFFFFu);
    entry.method = parse_bounded<std::uint16_t>(
        fields[4], line_number, case_id, (prefix + "method").c_str(), 0u, 0xFFFFu);
    entry.crc32 = parse_bounded<std::uint32_t>(
        fields[5], line_number, case_id, (prefix + "crc32").c_str(), 0u, 0xFFFFFFFFu);
    entry.compressed_size = parse_bounded<std::uint32_t>(
        fields[6], line_number, case_id, (prefix + "compressed_size").c_str(),
        0u, 0xFFFFFFFFu);
    entry.uncompressed_size = parse_bounded<std::uint32_t>(
        fields[7], line_number, case_id, (prefix + "uncompressed_size").c_str(),
        0u, 0xFFFFFFFFu);
    entry.directory = parse_bool(fields[8], line_number, case_id,
                                 (prefix + "directory").c_str());
    return entry;
}

bool valid_error_code(const std::string& value)
{
    static const std::unordered_set<std::string> values = {
        "INVALID_ZIP", "PACKAGE_LIMIT_EXCEEDED", "ENTRY_LIMIT_EXCEEDED",
        "INFLATED_LIMIT_EXCEEDED", "PAYLOAD_LIMIT_EXCEEDED", "NAME_LIMIT_EXCEEDED",
        "RATIO_LIMIT_EXCEEDED", "ENCRYPTED", "UNSUPPORTED_COMPRESSION", "ENTRY_MISSING",
    };
    return values.count(value) == 1u;
}

std::vector<std::uint8_t> read_regular_file(const fs::path& path,
                                            std::size_t line_number,
                                            const std::string& case_id,
                                            const char* field)
{
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error || !fs::exists(status))
    {
        fail_manifest(line_number, case_id, field,
                      "missing file '" + path.filename().string() + "'");
    }
    if (fs::is_symlink(status) || !fs::is_regular_file(status))
    {
        fail_manifest(line_number, case_id, field,
                      "expected a regular non-symlink file: '" +
                          path.filename().string() + "'");
    }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        fail_manifest(line_number, case_id, field, "could not open file");
    }
    const std::streamoff length = input.tellg();
    if (length < 0 || static_cast<std::uintmax_t>(length) >
                          static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
    {
        fail_manifest(line_number, case_id, field, "file has an unsupported length");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty())
    {
        if (bytes.size() > static_cast<std::size_t>(
                               std::numeric_limits<std::streamsize>::max()))
        {
            fail_manifest(line_number, case_id, field, "file exceeds stream read limits");
        }
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input)
        {
            fail_manifest(line_number, case_id, field, "could not read complete file");
        }
    }
    return bytes;
}

struct ParsedRow final
{
    ZipOpenFixture fixture;
    std::string blob_name;
    std::string expected_sha256;
    std::size_t line_number = 0u;
};

ParsedRow parse_row(const std::string& line, std::size_t line_number)
{
    const std::vector<std::string> fields = split(line, '\t');
    const std::string case_id = fields.size() > 1u ? fields[1] : std::string{};
    if (fields.size() != 15u)
    {
        fail_manifest(line_number, case_id, "row",
                      "expected 15 tab-separated columns, got " +
                          std::to_string(fields.size()));
    }
    if (fields[0] != "1")
    {
        fail_manifest(line_number, case_id, "schema_version",
                      "expected '1', got '" + fields[0] + "'");
    }
    if (!is_identifier(case_id))
    {
        fail_manifest(line_number, case_id, "case_id",
                      "must match [a-z][a-z0-9_]*, got '" + case_id + "'");
    }
    if (!is_safe_blob(case_id, fields[2]))
    {
        fail_manifest(line_number, case_id, "blob",
                      "must equal the safe basename '<case_id>.zip', got '" + fields[2] + "'");
    }
    if (fields[3].size() != 64u || !is_lower_hex(fields[3]))
    {
        fail_manifest(line_number, case_id, "sha256",
                      "must be exactly 64 lowercase hexadecimal characters");
    }

    ParsedRow row;
    row.fixture.case_id = case_id;
    row.blob_name = fields[2];
    row.expected_sha256 = fields[3];
    row.line_number = line_number;
    row.fixture.limits.max_package_bytes = parse_bounded<std::uint64_t>(
        fields[4], line_number, case_id, "max_package_bytes", 1u,
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
    row.fixture.limits.max_payload_bytes = parse_bounded<std::uint64_t>(
        fields[5], line_number, case_id, "max_payload_bytes", 1u,
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
    row.fixture.limits.max_zip_entries = parse_bounded<std::uint32_t>(
        fields[6], line_number, case_id, "max_zip_entries", 1u,
        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()));
    row.fixture.limits.max_cumulative_inflated_bytes = parse_bounded<std::uint64_t>(
        fields[7], line_number, case_id, "max_cumulative_inflated_bytes", 1u,
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
    row.fixture.limits.max_name_bytes = parse_bounded<std::uint32_t>(
        fields[8], line_number, case_id, "max_name_bytes", 1u, 0xFFFFu);
    row.fixture.limits.max_compression_ratio = parse_bounded<std::uint32_t>(
        fields[9], line_number, case_id, "max_compression_ratio", 1u,
        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()));
    row.fixture.limits.ratio_guard_threshold_bytes = parse_bounded<std::uint64_t>(
        fields[10], line_number, case_id, "ratio_guard_threshold_bytes", 0u,
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));

    if (fields[11] == "SUCCESS")
    {
        row.fixture.succeeds = true;
        if (fields[12] != "NONE" || fields[13] != "NONE")
        {
            fail_manifest(line_number, case_id, "outcome",
                          "SUCCESS requires NONE error code and message");
        }
        if (fields[14] == "NONE")
        {
            fail_manifest(line_number, case_id, "entries",
                          "SUCCESS requires explicit ordered entry metadata");
        }
        const std::vector<std::string> entries = split(fields[14], ';');
        for (std::size_t index = 0u; index < entries.size(); ++index)
        {
            row.fixture.entries.push_back(parse_entry(entries[index], line_number, case_id, index));
        }
    }
    else if (fields[11] == "ERROR")
    {
        if (!valid_error_code(fields[12]))
        {
            fail_manifest(line_number, case_id, "error_code",
                          "unknown value '" + fields[12] + "'");
        }
        if (is_blank(fields[13]) || fields[13] == "NONE")
        {
            fail_manifest(line_number, case_id, "error_message",
                          "ERROR requires a nonblank stable message");
        }
        if (fields[14] != "NONE")
        {
            fail_manifest(line_number, case_id, "entries",
                          "ERROR must not expose entry metadata");
        }
        row.fixture.error_code = fields[12];
        row.fixture.error_message = fields[13];
    }
    else
    {
        fail_manifest(line_number, case_id, "outcome",
                      "expected SUCCESS or ERROR, got '" + fields[11] + "'");
    }
    return row;
}

void validate_directory(const fs::path& root, const std::vector<ParsedRow>& rows)
{
    std::unordered_set<std::string> expected{"manifest.tsv"};
    for (const ParsedRow& row : rows)
    {
        expected.insert(row.blob_name);
    }
    std::error_code error;
    fs::directory_iterator iterator(root, error);
    const fs::directory_iterator end;
    if (error)
    {
        throw std::runtime_error("fixture corpus root could not be enumerated: " + root.string());
    }
    while (iterator != end)
    {
        const fs::directory_entry entry = *iterator;
        const std::string name = entry.path().filename().string();
        const fs::file_status status = entry.symlink_status(error);
        if (error || fs::is_symlink(status) || !fs::is_regular_file(status))
        {
            throw std::runtime_error("fixture corpus entry '" + name +
                                     "': expected a regular non-symlink file");
        }
        if (expected.erase(name) == 0u)
        {
            throw std::runtime_error("fixture corpus entry '" + name +
                                     "': unexpected file not referenced by manifest");
        }
        iterator.increment(error);
        if (error)
        {
            throw std::runtime_error("fixture corpus root could not be fully enumerated: " +
                                     root.string());
        }
    }
    if (!expected.empty())
    {
        throw std::runtime_error("fixture corpus entry '" + *expected.begin() + "': missing file");
    }
}

} // namespace

std::vector<ZipOpenFixture> load_zip_open_fixtures(const std::string& fixture_root)
{
    const fs::path root(fixture_root);
    std::error_code error;
    const fs::file_status root_status = fs::symlink_status(root, error);
    if (error || fs::is_symlink(root_status) || !fs::is_directory(root_status))
    {
        throw std::runtime_error("fixture corpus root is not a regular directory: " + fixture_root);
    }
    const std::vector<std::uint8_t> manifest_bytes =
        read_regular_file(root / "manifest.tsv", 1u, {}, "manifest.tsv");
    if (manifest_bytes.empty() || manifest_bytes.back() != static_cast<std::uint8_t>('\n'))
    {
        fail_manifest(1u, {}, "manifest.tsv", "manifest must end with LF");
    }
    std::string manifest;
    manifest.reserve(manifest_bytes.size());
    for (const std::uint8_t value : manifest_bytes)
    {
        if (value > 0x7Fu || value == static_cast<std::uint8_t>('\r'))
        {
            fail_manifest(1u, {}, "manifest.tsv", "manifest must be strict ASCII with LF lines");
        }
        manifest.push_back(static_cast<char>(value));
    }
    const std::vector<std::string> lines = split(manifest, '\n');
    if (lines.size() < 2u || lines.front() != manifest_header)
    {
        fail_manifest(1u, {}, "header", "unexpected manifest header");
    }
    if (!lines.back().empty())
    {
        fail_manifest(lines.size(), {}, "manifest.tsv", "manifest must end after its final LF");
    }

    std::vector<ParsedRow> rows;
    std::unordered_set<std::string> case_ids;
    std::unordered_set<std::string> blob_names;
    for (std::size_t index = 1u; index + 1u < lines.size(); ++index)
    {
        if (lines[index].empty())
        {
            fail_manifest(index + 1u, {}, "row", "empty manifest row");
        }
        ParsedRow row = parse_row(lines[index], index + 1u);
        if (!case_ids.insert(row.fixture.case_id).second)
        {
            fail_manifest(index + 1u, row.fixture.case_id, "case_id", "duplicate case ID");
        }
        if (!blob_names.insert(row.blob_name).second)
        {
            fail_manifest(index + 1u, row.fixture.case_id, "blob", "duplicate blob name");
        }
        rows.push_back(std::move(row));
    }
    if (rows.size() != 64u)
    {
        throw std::runtime_error(
            "fixture corpus field 'case count': version one must contain exactly 64 cases");
    }
    validate_directory(root, rows);

    std::vector<ZipOpenFixture> fixtures;
    fixtures.reserve(rows.size());
    for (ParsedRow& row : rows)
    {
        row.fixture.archive = read_regular_file(
            root / row.blob_name, row.line_number, row.fixture.case_id, "blob");
        if (sha256(row.fixture.archive) != row.expected_sha256)
        {
            fail_manifest(row.line_number, row.fixture.case_id, "sha256",
                          "blob digest does not match manifest value");
        }
        fixtures.push_back(std::move(row.fixture));
    }
    return fixtures;
}

} // namespace flynes::test
