#include "identity_fixture.hpp"

#include "catalog/content_identity.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

constexpr const char* hash_header =
    "schema_version\tcase_id\tblob\tblob_size\tblob_sha256\texpected_sha1\t"
    "expected_sha256\texpected_crc32";
constexpr const char* stable_header =
    "schema_version\tcase_id\toperation\targ0_utf8_hex\targ1_utf8_hex\t"
    "arg2_utf8_hex\toutcome\texpected_value\texpected_message";

const std::unordered_set<std::string> required_hash_cases = {
    "empty", "abc", "binary_00_ff", "pad_55", "pad_56", "pad_63", "pad_64",
    "pad_65", "million_a", "digits_123456789", "ownership_payload",
    "ownership_physical",
};

const std::unordered_set<std::string> required_stable_cases = {
    "package_exact_golden", "source_exact_golden", "variant_exact_golden",
    "game_lowercase_hash", "entry_exact_golden", "framing_ab_c", "framing_a_bc",
    "chinese", "emoji", "nfc_distinct", "nfd_distinct", "embedded_nul",
    "edge_spaces_preserved", "nonblank_mongolian_vowel_separator",
    "nonblank_next_line", "nonblank_zero_width_space", "nonblank_bom",
    "variant_lowercase_hash", "blank_empty", "blank_ascii_controls",
    "blank_file_separators", "blank_no_break_space", "blank_ogham",
    "blank_en_quad_through_hair", "blank_line_paragraph", "blank_narrow_no_break",
    "blank_medium_math", "blank_ideographic", "fullwidth_hex_rejected",
    "arabic_hex_rejected", "emoji_hex_rejected", "short_hash_rejected",
    "variant_bad_hash_precedes_blank",
    "invalid_utf8_overlong", "invalid_utf8_truncated", "invalid_utf8_surrogate",
    "invalid_utf8_too_large", "invalid_utf8_continuation",
};

[[noreturn]] void fail_manifest(std::size_t line,
                                const std::string& case_id,
                                const char* field,
                                const std::string& detail)
{
    std::ostringstream message;
    message << "manifest line " << line;
    if (!case_id.empty())
    {
        message << ", case '" << case_id << "'";
    }
    message << ", field '" << field << "': " << detail;
    throw std::runtime_error(message.str());
}

std::vector<std::string> split(const std::string& value, char delimiter)
{
    std::vector<std::string> result;
    std::size_t start = 0u;
    while (true)
    {
        const std::size_t end = value.find(delimiter, start);
        result.push_back(value.substr(start, end - start));
        if (end == std::string::npos)
        {
            return result;
        }
        start = end + 1u;
    }
}

bool lower_ascii(char value)
{
    return value >= 'a' && value <= 'z';
}

bool decimal_digit(char value)
{
    return value >= '0' && value <= '9';
}

bool valid_case_id(const std::string& value)
{
    if (value.empty() || !lower_ascii(value.front()))
    {
        return false;
    }
    for (const char character : value)
    {
        if (!lower_ascii(character) && !decimal_digit(character) && character != '_')
        {
            return false;
        }
    }
    return true;
}

bool is_lower_hex(const std::string& value, std::size_t length, bool allow_empty = false)
{
    if (value.size() != length || (value.empty() && !allow_empty))
    {
        return false;
    }
    for (const char character : value)
    {
        if (!decimal_digit(character) && !(character >= 'a' && character <= 'f'))
        {
            return false;
        }
    }
    return true;
}

bool is_upper_hex(const std::string& value, std::size_t length)
{
    if (value.size() != length)
    {
        return false;
    }
    for (const char character : value)
    {
        if (!decimal_digit(character) && !(character >= 'A' && character <= 'F'))
        {
            return false;
        }
    }
    return true;
}

std::uint64_t parse_unsigned(const std::string& value,
                             std::size_t line,
                             const std::string& case_id,
                             const char* field)
{
    if (value.empty() || (value.size() > 1u && value.front() == '0'))
    {
        fail_manifest(line, case_id, field, "expected canonical unsigned decimal");
    }
    std::uint64_t result = 0u;
    for (const char character : value)
    {
        if (!decimal_digit(character))
        {
            fail_manifest(line, case_id, field, "expected canonical unsigned decimal");
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10u)
        {
            fail_manifest(line, case_id, field, "unsigned decimal is out of range");
        }
        result = result * 10u + digit;
    }
    return result;
}

std::vector<std::uint8_t> read_regular_file(const fs::path& path,
                                            std::size_t line,
                                            const std::string& case_id,
                                            const char* field)
{
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error || fs::is_symlink(status) || !fs::is_regular_file(status))
    {
        fail_manifest(line,
                      case_id,
                      field,
                      "expected a regular non-symlink file: " + path.filename().string());
    }
    const std::uintmax_t size = fs::file_size(path, error);
    if (error || size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()) ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
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

std::vector<std::string> read_manifest(const fs::path& path, const char* expected_header)
{
    const std::vector<std::uint8_t> bytes = read_regular_file(path, 1u, {}, "manifest");
    std::string text;
    text.reserve(bytes.size());
    for (const std::uint8_t value : bytes)
    {
        if (value > 0x7Fu || value == static_cast<std::uint8_t>('\r'))
        {
            fail_manifest(1u, {}, "manifest", "manifest must be strict ASCII with LF lines");
        }
        text.push_back(static_cast<char>(value));
    }
    const std::vector<std::string> lines = split(text, '\n');
    if (lines.size() < 2u || lines.front() != expected_header)
    {
        fail_manifest(1u, {}, "header", "unexpected manifest header");
    }
    if (!lines.back().empty())
    {
        fail_manifest(lines.size(), {}, "manifest", "manifest must end after its final LF");
    }
    for (std::size_t index = 1u; index + 1u < lines.size(); ++index)
    {
        if (lines[index].empty())
        {
            fail_manifest(index + 1u, {}, "row", "empty manifest row");
        }
    }
    return lines;
}

std::string lowercase_ascii(std::string value)
{
    for (char& character : value)
    {
        if (character >= 'A' && character <= 'F')
        {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    return value;
}

struct HashRow final
{
    IdentityHashFixture fixture;
    std::size_t line = 0u;
    std::uint64_t expected_size = 0u;
    std::string expected_blob_sha256;
};

HashRow parse_hash_row(const std::string& line, std::size_t line_number)
{
    const std::vector<std::string> fields = split(line, '\t');
    if (fields.size() != 8u)
    {
        fail_manifest(line_number, {}, "columns", "expected exactly 8 tab-separated fields");
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
    if (fields[2] != case_id + ".bin" || fs::path(fields[2]).filename().string() != fields[2])
    {
        fail_manifest(line_number, case_id, "blob", "expected safe basename <case_id>.bin");
    }
    if (!is_lower_hex(fields[4], 64u))
    {
        fail_manifest(line_number, case_id, "blob_sha256", "expected 64 lowercase hex digits");
    }
    if (!is_upper_hex(fields[5], 40u))
    {
        fail_manifest(line_number, case_id, "expected_sha1", "expected 40 uppercase hex digits");
    }
    if (!is_upper_hex(fields[6], 64u))
    {
        fail_manifest(line_number, case_id, "expected_sha256", "expected 64 uppercase hex digits");
    }
    if (!is_upper_hex(fields[7], 8u))
    {
        fail_manifest(line_number, case_id, "expected_crc32", "expected 8 uppercase hex digits");
    }
    HashRow result;
    result.fixture.case_id = case_id;
    result.fixture.blob_name = fields[2];
    result.line = line_number;
    result.expected_size = parse_unsigned(fields[3], line_number, case_id, "blob_size");
    result.expected_blob_sha256 = fields[4];
    result.fixture.expected_sha1 = fields[5];
    result.fixture.expected_sha256 = fields[6];
    result.fixture.expected_crc32 = fields[7];
    return result;
}

std::pair<StableIdOperation, std::size_t> parse_operation(const std::string& value,
                                                          std::size_t line,
                                                          const std::string& case_id)
{
    if (value == "package_id")
    {
        return {StableIdOperation::PACKAGE_ID, 2u};
    }
    if (value == "saf_source_id")
    {
        return {StableIdOperation::SAF_SOURCE_ID, 1u};
    }
    if (value == "variant_id")
    {
        return {StableIdOperation::VARIANT_ID, 3u};
    }
    if (value == "provisional_game_id")
    {
        return {StableIdOperation::PROVISIONAL_GAME_ID, 1u};
    }
    if (value == "entry_outcome_id")
    {
        return {StableIdOperation::ENTRY_OUTCOME_ID, 2u};
    }
    fail_manifest(line, case_id, "operation", "unknown stable-ID operation");
}

std::string operation_prefix(StableIdOperation operation)
{
    switch (operation)
    {
    case StableIdOperation::PACKAGE_ID:
        return "pkg:";
    case StableIdOperation::SAF_SOURCE_ID:
        return "source:";
    case StableIdOperation::VARIANT_ID:
        return "variant:";
    case StableIdOperation::PROVISIONAL_GAME_ID:
        return "game:";
    case StableIdOperation::ENTRY_OUTCOME_ID:
        return "entry:";
    }
    return {};
}

std::string decode_hex(const std::string& value,
                       std::size_t line,
                       const std::string& case_id,
                       const char* field)
{
    if (value.size() % 2u != 0u || !is_lower_hex(value, value.size(), true))
    {
        fail_manifest(line, case_id, field, "expected canonical even-length lowercase hex");
    }
    const auto nibble = [](char character) -> std::uint8_t {
        if (character >= '0' && character <= '9')
        {
            return static_cast<std::uint8_t>(character - '0');
        }
        return static_cast<std::uint8_t>(character - 'a' + 10);
    };
    std::string result(value.size() / 2u, '\0');
    for (std::size_t index = 0u; index < result.size(); ++index)
    {
        result[index] = static_cast<char>(
            static_cast<std::uint8_t>(nibble(value[index * 2u]) << 4u) |
            nibble(value[index * 2u + 1u]));
    }
    return result;
}

StableIdFixture parse_stable_row(const std::string& line, std::size_t line_number)
{
    const std::vector<std::string> fields = split(line, '\t');
    if (fields.size() != 9u)
    {
        fail_manifest(line_number, {}, "columns", "expected exactly 9 tab-separated fields");
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
    const auto operation = parse_operation(fields[2], line_number, case_id);
    StableIdFixture fixture;
    fixture.case_id = case_id;
    fixture.operation = operation.first;
    fixture.arguments.reserve(operation.second);
    for (std::size_t index = 0u; index < 3u; ++index)
    {
        const std::string& encoded = fields[3u + index];
        if (index >= operation.second)
        {
            if (encoded != "NONE")
            {
                fail_manifest(line_number,
                              case_id,
                              "argument",
                              "unused arguments must be NONE");
            }
        }
        else
        {
            if (encoded == "NONE")
            {
                fail_manifest(line_number,
                              case_id,
                              "argument",
                              "required arguments must be encoded as hex");
            }
            fixture.arguments.push_back(decode_hex(
                encoded, line_number, case_id, index == 0u ? "arg0_utf8_hex" :
                                                  index == 1u ? "arg1_utf8_hex" :
                                                                "arg2_utf8_hex"));
        }
    }

    if (fields[6] == "SUCCESS")
    {
        fixture.succeeds = true;
        const std::string prefix = operation_prefix(fixture.operation);
        if (fields[8] != "NONE" || fields[7].size() != prefix.size() + 64u ||
            fields[7].compare(0u, prefix.size(), prefix) != 0 ||
            !is_upper_hex(fields[7].substr(prefix.size()), 64u))
        {
            fail_manifest(line_number,
                          case_id,
                          "outcome",
                          "SUCCESS requires a canonical value and NONE message");
        }
        fixture.expected_value = fields[7];
    }
    else if (fields[6] == "ERROR")
    {
        fixture.succeeds = false;
        if (fields[7] != "NONE" || fields[8].empty() || fields[8] == "NONE")
        {
            fail_manifest(line_number,
                          case_id,
                          "outcome",
                          "ERROR requires NONE value and a nonempty message");
        }
        for (const char character : fields[8])
        {
            if (static_cast<unsigned char>(character) < 0x20u ||
                static_cast<unsigned char>(character) > 0x7Eu)
            {
                fail_manifest(line_number,
                              case_id,
                              "expected_message",
                              "expected printable ASCII");
            }
        }
        fixture.expected_message = fields[8];
    }
    else
    {
        fail_manifest(line_number, case_id, "outcome", "expected SUCCESS or ERROR");
    }
    return fixture;
}

void validate_exact_case_set(const std::unordered_set<std::string>& actual,
                             const std::unordered_set<std::string>& required,
                             const char* label)
{
    if (actual != required)
    {
        throw std::runtime_error(std::string("fixture corpus field '") + label +
                                 "': version one case set/count mismatch");
    }
}

void validate_directory(const fs::path& root, const std::unordered_set<std::string>& blobs)
{
    std::unordered_set<std::string> expected = blobs;
    expected.insert("hashes.tsv");
    expected.insert("stable_ids.tsv");
    std::unordered_set<std::string> actual;
    std::error_code error;
    fs::directory_iterator iterator(root, error);
    const fs::directory_iterator end;
    if (error)
    {
        throw std::runtime_error("fixture corpus directory could not be enumerated");
    }
    for (; iterator != end; iterator.increment(error))
    {
        if (error)
        {
            throw std::runtime_error("fixture corpus directory enumeration failed");
        }
        const fs::directory_entry& entry = *iterator;
        const std::string name = entry.path().filename().string();
        const fs::file_status status = entry.symlink_status(error);
        if (error || fs::is_symlink(status) || !fs::is_regular_file(status))
        {
            throw std::runtime_error("fixture corpus entry '" + name +
                                     "' must be a regular non-symlink file");
        }
        if (!actual.insert(name).second || expected.count(name) == 0u)
        {
            throw std::runtime_error("fixture corpus entry '" + name + "' is unexpected");
        }
    }
    if (actual != expected)
    {
        throw std::runtime_error("fixture corpus contains a missing referenced file");
    }
}

} // namespace

IdentityFixtureCorpus load_identity_fixtures(const std::string& fixture_root)
{
    const fs::path root(fixture_root);
    std::error_code error;
    const fs::file_status root_status = fs::symlink_status(root, error);
    if (error || fs::is_symlink(root_status) || !fs::is_directory(root_status))
    {
        throw std::runtime_error("identity fixture root must be a regular non-symlink directory");
    }

    const std::vector<std::string> hash_lines =
        read_manifest(root / "hashes.tsv", hash_header);
    std::vector<HashRow> hash_rows;
    std::unordered_set<std::string> hash_ids;
    std::unordered_set<std::string> blobs;
    for (std::size_t index = 1u; index + 1u < hash_lines.size(); ++index)
    {
        HashRow row = parse_hash_row(hash_lines[index], index + 1u);
        if (!hash_ids.insert(row.fixture.case_id).second)
        {
            fail_manifest(index + 1u, row.fixture.case_id, "case_id", "duplicate case ID");
        }
        if (!blobs.insert(row.fixture.blob_name).second)
        {
            fail_manifest(index + 1u, row.fixture.case_id, "blob", "duplicate blob name");
        }
        hash_rows.push_back(std::move(row));
    }
    validate_exact_case_set(hash_ids, required_hash_cases, "hash case set");

    const std::vector<std::string> stable_lines =
        read_manifest(root / "stable_ids.tsv", stable_header);
    std::vector<StableIdFixture> stable_rows;
    std::unordered_set<std::string> stable_ids;
    for (std::size_t index = 1u; index + 1u < stable_lines.size(); ++index)
    {
        StableIdFixture fixture = parse_stable_row(stable_lines[index], index + 1u);
        if (!stable_ids.insert(fixture.case_id).second)
        {
            fail_manifest(index + 1u, fixture.case_id, "case_id", "duplicate case ID");
        }
        stable_rows.push_back(std::move(fixture));
    }
    validate_exact_case_set(stable_ids, required_stable_cases, "stable-ID case set");
    validate_directory(root, blobs);

    std::vector<IdentityHashFixture> hashes;
    hashes.reserve(hash_rows.size());
    for (HashRow& row : hash_rows)
    {
        row.fixture.blob = read_regular_file(
            root / row.fixture.blob_name, row.line, row.fixture.case_id, "blob");
        if (row.fixture.blob.size() != row.expected_size)
        {
            fail_manifest(row.line, row.fixture.case_id, "blob_size", "actual size differs");
        }
        const catalog::TextResult digest = catalog::sha256_hex(
            {row.fixture.blob.empty() ? nullptr : row.fixture.blob.data(),
             row.fixture.blob.size()});
        if (!digest.ok() || lowercase_ascii(digest.value) != row.expected_blob_sha256)
        {
            fail_manifest(row.line,
                          row.fixture.case_id,
                          "blob_sha256",
                          "blob digest does not match manifest value");
        }
        hashes.push_back(std::move(row.fixture));
    }

    IdentityFixtureCorpus result;
    result.hashes = std::move(hashes);
    result.stable_ids = std::move(stable_rows);
    return result;
}

} // namespace flynes::test
