#include "zip_payload_fixture.hpp"

#include <array>
#include <cctype>
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
    "max_compression_ratio\tratio_guard_threshold_bytes\tselector_raw_name_hex\t"
    "selector_local_header_offset\toutcome\terror_code\terror_message\t"
    "payload_length\tpayload_sha256";

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

[[noreturn]] void fail_manifest(std::size_t line,
                                const std::string& case_id,
                                const std::string& field,
                                const std::string& message)
{
    std::ostringstream output;
    output << "manifest line " << line;
    if (!case_id.empty())
    {
        output << " case '" << case_id << "'";
    }
    output << " field '" << field << "': " << message;
    throw std::runtime_error(output.str());
}

bool is_lower_hex(const std::string& value, std::size_t exact_length = 0u)
{
    if ((exact_length != 0u && value.size() != exact_length) || value.empty())
    {
        return false;
    }
    for (const char character : value)
    {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f')))
        {
            return false;
        }
    }
    return true;
}

bool valid_case_id(const std::string& value)
{
    if (value.empty() || value.front() < 'a' || value.front() > 'z')
    {
        return false;
    }
    for (const char character : value)
    {
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '_'))
        {
            return false;
        }
    }
    return true;
}

bool is_blank(const std::string& value)
{
    for (const unsigned char character : value)
    {
        if (std::isspace(character) == 0)
        {
            return false;
        }
    }
    return true;
}

std::uint64_t parse_unsigned(const std::string& value,
                             std::size_t line,
                             const std::string& case_id,
                             const std::string& field,
                             std::uint64_t minimum,
                             std::uint64_t maximum)
{
    if (value.empty() || (value.size() > 1u && value.front() == '0'))
    {
        fail_manifest(line, case_id, field,
                      "expected canonical unsigned decimal, got '" + value + "'");
    }
    std::uint64_t parsed = 0u;
    for (const char character : value)
    {
        if (character < '0' || character > '9')
        {
            fail_manifest(line, case_id, field,
                          "expected canonical unsigned decimal, got '" + value + "'");
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (parsed > (maximum - digit) / 10u)
        {
            fail_manifest(line, case_id, field, "value is outside the allowed range");
        }
        parsed = parsed * 10u + digit;
    }
    if (parsed < minimum || parsed > maximum)
    {
        fail_manifest(line, case_id, field, "value is outside the allowed range");
    }
    return parsed;
}

std::vector<std::uint8_t> parse_hex(const std::string& value,
                                    std::size_t line,
                                    const std::string& case_id,
                                    const std::string& field)
{
    if (!is_lower_hex(value) || value.size() % 2u != 0u || value.size() > 0xFFFFu * 2u)
    {
        fail_manifest(line, case_id, field,
                      "expected nonempty lowercase whole-byte hex within the ZIP name field");
    }
    std::vector<std::uint8_t> result;
    result.reserve(value.size() / 2u);
    const auto nibble = [](char character) {
        return static_cast<std::uint8_t>(character <= '9'
                                             ? character - '0'
                                             : character - 'a' + 10);
    };
    for (std::size_t index = 0u; index < value.size(); index += 2u)
    {
        result.push_back(static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(nibble(value[index]) << 4u) |
            nibble(value[index + 1u])));
    }
    return result;
}

bool valid_error_code(const std::string& value)
{
    static const std::unordered_set<std::string> codes = {
        "INVALID_ZIP", "PACKAGE_LIMIT_EXCEEDED", "ENTRY_LIMIT_EXCEEDED",
        "INFLATED_LIMIT_EXCEEDED", "PAYLOAD_LIMIT_EXCEEDED",
        "NAME_LIMIT_EXCEEDED", "RATIO_LIMIT_EXCEEDED", "ENCRYPTED",
        "UNSUPPORTED_COMPRESSION", "ENTRY_MISSING",
    };
    return codes.count(value) != 0u;
}

std::vector<std::uint8_t> read_regular_file(const fs::path& path,
                                            std::size_t line,
                                            const std::string& case_id,
                                            const std::string& field)
{
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error || fs::is_symlink(status) || !fs::is_regular_file(status))
    {
        fail_manifest(line, case_id, field,
                      "expected a regular non-symlink file: " + path.filename().string());
    }
    const std::uintmax_t size = fs::file_size(path, error);
    if (error || size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
    {
        fail_manifest(line, case_id, field, "file size cannot be represented in memory");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        fail_manifest(line, case_id, field, "file could not be opened");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty())
    {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!input || input.peek() != std::ifstream::traits_type::eof())
    {
        fail_manifest(line, case_id, field, "file could not be read exactly");
    }
    return bytes;
}

struct ParsedRow final
{
    ZipPayloadFixture fixture;
    std::size_t line = 0u;
    std::string blob;
    std::string expected_archive_sha256;
};

ParsedRow parse_row(const std::string& line, std::size_t line_number)
{
    const std::vector<std::string> fields = split(line, '\t');
    if (fields.size() != 18u)
    {
        fail_manifest(line_number, {}, "columns", "expected exactly 18 tab-separated fields");
    }
    const std::string& case_id = fields[1];
    if (fields[0] != "1")
    {
        fail_manifest(line_number, case_id, "schema_version", "expected '1'");
    }
    if (!valid_case_id(case_id))
    {
        fail_manifest(line_number, case_id, "case_id", "expected [a-z][a-z0-9_]*");
    }
    if (fields[2] != case_id + ".zip" ||
        fs::path(fields[2]).filename().string() != fields[2])
    {
        fail_manifest(line_number, case_id, "blob", "expected safe basename <case_id>.zip");
    }
    if (!is_lower_hex(fields[3], 64u))
    {
        fail_manifest(line_number, case_id, "sha256", "expected 64 lowercase hex digits");
    }

    constexpr std::uint64_t java_long_max =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    constexpr std::uint64_t java_int_max =
        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
    ParsedRow row;
    row.line = line_number;
    row.blob = fields[2];
    row.expected_archive_sha256 = fields[3];
    row.fixture.case_id = case_id;
    row.fixture.limits.max_package_bytes = parse_unsigned(
        fields[4], line_number, case_id, "max_package_bytes", 1u, java_long_max);
    row.fixture.limits.max_payload_bytes = parse_unsigned(
        fields[5], line_number, case_id, "max_payload_bytes", 1u, java_long_max);
    row.fixture.limits.max_zip_entries = static_cast<std::uint32_t>(parse_unsigned(
        fields[6], line_number, case_id, "max_zip_entries", 1u, java_int_max));
    row.fixture.limits.max_cumulative_inflated_bytes = parse_unsigned(
        fields[7], line_number, case_id, "max_cumulative_inflated_bytes", 1u,
        java_long_max);
    row.fixture.limits.max_name_bytes = static_cast<std::uint32_t>(parse_unsigned(
        fields[8], line_number, case_id, "max_name_bytes", 1u, 0xFFFFu));
    row.fixture.limits.max_compression_ratio = static_cast<std::uint32_t>(parse_unsigned(
        fields[9], line_number, case_id, "max_compression_ratio", 1u, java_int_max));
    row.fixture.limits.ratio_guard_threshold_bytes = parse_unsigned(
        fields[10], line_number, case_id, "ratio_guard_threshold_bytes", 0u,
        java_long_max);
    row.fixture.selector_raw_name = parse_hex(
        fields[11], line_number, case_id, "selector_raw_name_hex");
    row.fixture.selector_local_header_offset = static_cast<std::int32_t>(parse_unsigned(
        fields[12], line_number, case_id, "selector_local_header_offset", 0u,
        java_int_max));

    if (fields[13] == "SUCCESS")
    {
        row.fixture.succeeds = true;
        if (fields[14] != "NONE" || fields[15] != "NONE")
        {
            fail_manifest(line_number, case_id, "outcome",
                          "SUCCESS requires NONE error code and message");
        }
        row.fixture.payload_length = static_cast<std::uint32_t>(parse_unsigned(
            fields[16], line_number, case_id, "payload_length", 0u, java_int_max));
        if (!is_lower_hex(fields[17], 64u))
        {
            fail_manifest(line_number, case_id, "payload_sha256",
                          "expected 64 lowercase hex digits");
        }
        row.fixture.payload_sha256 = fields[17];
    }
    else if (fields[13] == "ERROR")
    {
        if (!valid_error_code(fields[14]))
        {
            fail_manifest(line_number, case_id, "error_code", "unknown value '" +
                              fields[14] + "'");
        }
        if (is_blank(fields[15]) || fields[15] == "NONE")
        {
            fail_manifest(line_number, case_id, "error_message",
                          "ERROR requires a nonblank stable message");
        }
        if (fields[16] != "NONE" || fields[17] != "NONE")
        {
            fail_manifest(line_number, case_id, "outcome",
                          "ERROR requires NONE payload fields");
        }
        row.fixture.error_code = fields[14];
        row.fixture.error_message = fields[15];
    }
    else
    {
        fail_manifest(line_number, case_id, "outcome",
                      "expected SUCCESS or ERROR, got '" + fields[13] + "'");
    }
    return row;
}

void validate_directory(const fs::path& root, const std::vector<ParsedRow>& rows)
{
    std::unordered_set<std::string> expected{"manifest.tsv"};
    for (const ParsedRow& row : rows)
    {
        expected.insert(row.blob);
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
        throw std::runtime_error("fixture corpus entry '" + *expected.begin() +
                                 "': missing file");
    }
}

} // namespace

std::string fixture_sha256(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() > std::numeric_limits<std::uint64_t>::max() / 8u)
    {
        throw std::runtime_error("fixture is too large for SHA-256 length encoding");
    }
    const std::uint64_t bit_length = static_cast<std::uint64_t>(bytes.size()) * 8u;
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
    for (std::size_t chunk = 0u; chunk < padded.size(); chunk += 64u)
    {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0u; index < 16u; ++index)
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
        for (std::size_t index = 0u; index < words.size(); ++index)
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

std::vector<ZipPayloadFixture> load_zip_payload_fixtures(
    const std::string& fixture_root)
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
    std::unordered_set<std::string> blobs;
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
        if (!blobs.insert(row.blob).second)
        {
            fail_manifest(index + 1u, row.fixture.case_id, "blob", "duplicate blob name");
        }
        rows.push_back(std::move(row));
    }
    if (rows.size() != 19u)
    {
        throw std::runtime_error(
            "fixture corpus field 'case count': version one must contain exactly 19 cases");
    }
    validate_directory(root, rows);

    std::vector<ZipPayloadFixture> fixtures;
    fixtures.reserve(rows.size());
    for (ParsedRow& row : rows)
    {
        row.fixture.archive = read_regular_file(
            root / row.blob, row.line, row.fixture.case_id, "blob");
        if (fixture_sha256(row.fixture.archive) != row.expected_archive_sha256)
        {
            fail_manifest(row.line, row.fixture.case_id, "sha256",
                          "blob digest does not match manifest value");
        }
        fixtures.push_back(std::move(row.fixture));
    }
    return fixtures;
}

} // namespace flynes::test
