#include "unsupported_payload_fixture.hpp"

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

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace flynes::test {
namespace {

namespace fs = std::filesystem;

constexpr const char* manifest_header =
    "schema_version\tcase_id\tblob\tblob_size\tblob_sha256\texpected_reason";

const std::unordered_set<std::string> required_cases = {
    "empty",
    "zip_local_exact", "zip_local_trailing",
    "zip_empty_exact", "zip_empty_trailing",
    "zip_spanned_exact", "zip_spanned_trailing",
    "pk_central_nonmatch", "short_p", "short_pk", "short_pk03",
    "mz_exact", "mz_trailing", "lowercase_mz", "mz_game_boy_priority",
    "game_boy_minimum", "game_boy_longer", "game_boy_logo_byte_diff",
    "game_boy_offset_minus_one", "zip_game_boy_priority",
    "plain_ascii", "allowed_controls", "nul_prefix",
    "forbidden_c0_01", "forbidden_c0_0b", "forbidden_c0_1f",
    "del_text", "c1_text", "high_bytes_text", "invalid_utf8_text",
    "text_length_4095", "text_length_4096", "text_length_4097",
    "forbidden_at_4095", "forbidden_at_4096",
    "nul_at_4095", "nul_at_4096", "ordinary_binary",
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

bool lower_hex(const std::string& value, std::size_t expected_length)
{
    if (value.size() != expected_length)
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

std::uint64_t parse_unsigned(const std::string& value,
                             std::size_t line,
                             const std::string& case_id)
{
    if (value.empty() || (value.size() > 1u && value.front() == '0'))
    {
        fail_manifest(line, case_id, "blob_size", "expected canonical unsigned decimal");
    }
    std::uint64_t result = 0u;
    for (const char character : value)
    {
        if (!decimal_digit(character))
        {
            fail_manifest(line, case_id, "blob_size", "expected canonical unsigned decimal");
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10u)
        {
            fail_manifest(line, case_id, "blob_size", "unsigned decimal is out of range");
        }
        result = result * 10u + digit;
    }
    return result;
}

void require_regular_single_link(const fs::path& path,
                                 std::size_t line,
                                 const std::string& case_id,
                                 const char* field)
{
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    bool reparse_point = false;
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        fail_manifest(line, case_id, field,
                      "file attributes could not be read: " + path.filename().string());
    }
    reparse_point = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u;
#endif
    if (error || fs::is_symlink(status) || reparse_point || !fs::is_regular_file(status))
    {
        fail_manifest(line, case_id, field,
                      "expected a regular non-symlink file: " + path.filename().string());
    }
    const std::uintmax_t links = fs::hard_link_count(path, error);
    if (error || links != 1u)
    {
        fail_manifest(line, case_id, field,
                      "fixture must have exactly one hard link: " + path.filename().string());
    }
}

std::vector<std::uint8_t> read_regular_file(const fs::path& path,
                                            std::size_t line,
                                            const std::string& case_id,
                                            const char* field)
{
    require_regular_single_link(path, line, case_id, field);
    std::error_code error;
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

std::vector<std::string> read_manifest(const fs::path& path)
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
    if (lines.size() < 2u || lines.front() != manifest_header)
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

ExpectedUnsupportedPayloadReason parse_reason(const std::string& value,
                                              std::size_t line,
                                              const std::string& case_id)
{
    if (value == "NESTED_ARCHIVE")
    {
        return ExpectedUnsupportedPayloadReason::NESTED_ARCHIVE;
    }
    if (value == "EXECUTABLE")
    {
        return ExpectedUnsupportedPayloadReason::EXECUTABLE;
    }
    if (value == "GAME_BOY")
    {
        return ExpectedUnsupportedPayloadReason::GAME_BOY;
    }
    if (value == "SIDECAR")
    {
        return ExpectedUnsupportedPayloadReason::SIDECAR;
    }
    if (value == "UNKNOWN_FORMAT")
    {
        return ExpectedUnsupportedPayloadReason::UNKNOWN_FORMAT;
    }
    fail_manifest(line, case_id, "expected_reason", "expected canonical reason");
}

struct ParsedRow final
{
    UnsupportedPayloadFixture fixture;
    std::size_t line = 0u;
    std::uint64_t expected_size = 0u;
    std::string expected_sha256;
};

ParsedRow parse_row(const std::string& row, std::size_t line)
{
    const std::vector<std::string> fields = split(row, '\t');
    if (fields.size() != 6u)
    {
        fail_manifest(line, {}, "columns", "expected exactly 6 tab-separated fields");
    }
    const std::string& case_id = fields[1];
    if (fields[0] != "1")
    {
        fail_manifest(line, case_id, "schema_version", "expected '1'");
    }
    if (!valid_case_id(case_id))
    {
        fail_manifest(line, case_id, "case_id", "expected [a-z][a-z0-9_]*");
    }
    if (fields[2] != case_id + ".bin" || fs::path(fields[2]).filename().string() != fields[2])
    {
        fail_manifest(line, case_id, "blob", "expected safe basename <case_id>.bin");
    }
    if (!lower_hex(fields[4], 64u))
    {
        fail_manifest(line, case_id, "blob_sha256", "expected 64 lowercase hex digits");
    }
    ParsedRow parsed;
    parsed.fixture.case_id = case_id;
    parsed.fixture.blob_name = fields[2];
    parsed.fixture.expected_reason = parse_reason(fields[5], line, case_id);
    parsed.line = line;
    parsed.expected_size = parse_unsigned(fields[3], line, case_id);
    parsed.expected_sha256 = fields[4];
    return parsed;
}

std::unordered_set<std::string> list_exact_files(
    const fs::path& root, const std::unordered_set<std::string>& expected)
{
    std::error_code error;
    const fs::file_status root_status = fs::symlink_status(root, error);
    if (error || fs::is_symlink(root_status) || !fs::is_directory(root_status))
    {
        fail_manifest(1u, {}, "corpus", "expected ordinary non-symlink directory");
    }
    std::unordered_set<std::string> actual;
    for (const fs::directory_entry& entry : fs::directory_iterator(root))
    {
        const std::string name = entry.path().filename().string();
        require_regular_single_link(entry.path(), 1u, {}, "corpus");
        if (!actual.insert(name).second)
        {
            fail_manifest(1u, {}, "corpus", "duplicate directory entry: " + name);
        }
        if (expected.find(name) == expected.end())
        {
            fail_manifest(1u, {}, "corpus", "unexpected corpus entry: " + name);
        }
    }
    if (actual != expected)
    {
        fail_manifest(1u, {}, "corpus", "exact corpus filename set mismatch");
    }
    return actual;
}

} // namespace

std::vector<UnsupportedPayloadFixture> load_unsupported_payload_fixtures(
    const std::string& fixture_root)
{
    const fs::path root(fixture_root);
    std::error_code root_error;
    const fs::file_status root_status = fs::symlink_status(root, root_error);
    bool root_reparse_point = false;
#if defined(_WIN32)
    const DWORD root_attributes = GetFileAttributesW(root.c_str());
    if (root_attributes == INVALID_FILE_ATTRIBUTES)
    {
        fail_manifest(1u, {}, "corpus", "directory attributes could not be read");
    }
    root_reparse_point = (root_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u;
#endif
    if (root_error || fs::is_symlink(root_status) || root_reparse_point ||
        !fs::is_directory(root_status))
    {
        fail_manifest(1u, {}, "corpus", "expected ordinary non-symlink directory");
    }
    const std::vector<std::string> lines = read_manifest(root / "manifest.tsv");
    std::vector<ParsedRow> rows;
    rows.reserve(lines.size() - 2u);
    std::unordered_set<std::string> case_ids;
    std::unordered_set<std::string> blob_names;
    for (std::size_t index = 1u; index + 1u < lines.size(); ++index)
    {
        ParsedRow row = parse_row(lines[index], index + 1u);
        if (!case_ids.insert(row.fixture.case_id).second)
        {
            fail_manifest(row.line, row.fixture.case_id, "case_id", "duplicate case_id");
        }
        if (!blob_names.insert(row.fixture.blob_name).second)
        {
            fail_manifest(row.line, row.fixture.case_id, "blob", "duplicate blob");
        }
        rows.push_back(std::move(row));
    }
    if (rows.size() != required_cases.size() || case_ids != required_cases)
    {
        fail_manifest(1u, {}, "case_id", "exact frozen case set mismatch");
    }
    std::unordered_set<std::string> expected_files = blob_names;
    expected_files.insert("manifest.tsv");
    static_cast<void>(list_exact_files(root, expected_files));

    std::vector<UnsupportedPayloadFixture> fixtures;
    fixtures.reserve(rows.size());
    for (ParsedRow& row : rows)
    {
        row.fixture.blob = read_regular_file(
            root / row.fixture.blob_name, row.line, row.fixture.case_id, "blob");
        if (row.expected_size != static_cast<std::uint64_t>(row.fixture.blob.size()))
        {
            fail_manifest(row.line, row.fixture.case_id, "blob_size", "size mismatch");
        }
        const catalog::TextResult digest = catalog::sha256_hex(
            {row.fixture.blob.data(), row.fixture.blob.size()});
        if (!digest.ok() || digest.value.size() != 64u)
        {
            fail_manifest(row.line, row.fixture.case_id, "blob_sha256",
                          "could not calculate digest");
        }
        std::string lowercase_digest = digest.value;
        for (char& character : lowercase_digest)
        {
            if (character >= 'A' && character <= 'F')
            {
                character = static_cast<char>(character + ('a' - 'A'));
            }
        }
        if (lowercase_digest != row.expected_sha256)
        {
            fail_manifest(row.line, row.fixture.case_id, "blob_sha256", "digest mismatch");
        }
        fixtures.push_back(std::move(row.fixture));
    }
    return fixtures;
}

} // namespace flynes::test
