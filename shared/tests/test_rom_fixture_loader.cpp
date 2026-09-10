#include "rom_fixture.hpp"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef FLYNES_ROM_FIXTURE_DIR
#error "FLYNES_ROM_FIXTURE_DIR must identify the tracked ROM fixture directory"
#endif

#ifndef FLYNES_ROM_FIXTURE_TEST_DIR
#error "FLYNES_ROM_FIXTURE_TEST_DIR must identify an isolated build directory"
#endif

namespace {

namespace fs = std::filesystem;

int failures = 0;

void check(bool condition, const char* test_name, const std::string& detail)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s: %s\n", test_name, detail.c_str());
        ++failures;
    }
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

std::vector<std::string> read_manifest(const fs::path& corpus)
{
    std::ifstream input(corpus / "manifest.tsv", std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("could not read temporary manifest");
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

void write_manifest(const fs::path& corpus, const std::vector<std::string>& lines)
{
    std::ofstream output(corpus / "manifest.tsv", std::ios::binary | std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error("could not write temporary manifest");
    }
    for (const std::string& line : lines)
    {
        output << line << '\n';
    }
    if (!output)
    {
        throw std::runtime_error("could not finish temporary manifest");
    }
}

void replace_field(const fs::path& corpus,
                   std::size_t data_row,
                   std::size_t column,
                   const std::string& value)
{
    std::vector<std::string> lines = read_manifest(corpus);
    const std::size_t line_index = data_row + 1u;
    if (line_index >= lines.size())
    {
        throw std::runtime_error("temporary manifest row is out of range");
    }
    std::vector<std::string> fields = split(lines[line_index], '\t');
    if (column >= fields.size())
    {
        throw std::runtime_error("temporary manifest column is out of range");
    }
    fields[column] = value;
    std::string rebuilt;
    for (std::size_t index = 0; index < fields.size(); ++index)
    {
        if (index != 0u)
        {
            rebuilt.push_back('\t');
        }
        rebuilt += fields[index];
    }
    lines[line_index] = rebuilt;
    write_manifest(corpus, lines);
}

class ScratchRoot final
{
public:
    ScratchRoot()
        : path_(FLYNES_ROM_FIXTURE_TEST_DIR)
    {
        if (path_.filename() != "rom-fixture-loader-tests")
        {
            throw std::runtime_error("unexpected fixture-loader scratch directory");
        }
        std::error_code error;
        fs::remove_all(path_, error);
        if (error)
        {
            throw std::runtime_error("could not reset fixture-loader scratch directory");
        }
        if (!fs::create_directories(path_, error) || error)
        {
            throw std::runtime_error("could not create fixture-loader scratch directory");
        }
    }

    ~ScratchRoot()
    {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    ScratchRoot(const ScratchRoot&) = delete;
    ScratchRoot& operator=(const ScratchRoot&) = delete;

    fs::path copy_corpus(const std::string& scenario) const
    {
        const fs::path corpus = path_ / scenario / "corpus";
        std::error_code error;
        if (!fs::create_directories(corpus, error) || error)
        {
            throw std::runtime_error("could not create temporary fixture corpus");
        }
        for (const fs::directory_entry& entry : fs::directory_iterator(FLYNES_ROM_FIXTURE_DIR))
        {
            if (!entry.is_regular_file())
            {
                throw std::runtime_error("tracked fixture corpus contains a non-file entry");
            }
            fs::copy_file(entry.path(), corpus / entry.path().filename());
        }
        return corpus;
    }

private:
    fs::path path_;
};

void expect_load_error(const fs::path& corpus,
                       const char* test_name,
                       std::initializer_list<const char*> required_fragments)
{
    try
    {
        static_cast<void>(flynes::test::load_rom_fixtures(corpus.string()));
        check(false, test_name, "loader accepted malformed fixture corpus");
    }
    catch (const std::exception& failure)
    {
        const std::string message = failure.what();
        for (const char* fragment : required_fragments)
        {
            check(message.find(fragment) != std::string::npos,
                  test_name,
                  "error did not contain '" + std::string(fragment) + "': " + message);
        }
    }
}

void copy_traversal_target(const fs::path& corpus, const std::string& manifest_path)
{
    const fs::path source = corpus / "playable_ines_metadata.bin";
    const fs::path destination = corpus / fs::path(manifest_path);
    std::error_code error;
    fs::create_directories(destination.parent_path(), error);
    if (error)
    {
        throw std::runtime_error("could not create traversal target parent");
    }
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
}

void test_slash_traversal(const ScratchRoot& scratch)
{
    const fs::path corpus = scratch.copy_corpus("slash-traversal");
    const std::string traversal = "../escape.bin";
    copy_traversal_target(corpus, traversal);
    replace_field(corpus, 0u, 2u, traversal);
    expect_load_error(corpus,
                      "slash traversal",
                      {"manifest line 2", "playable_ines_metadata", "blob"});
}

void test_backslash_traversal(const ScratchRoot& scratch)
{
    const fs::path corpus = scratch.copy_corpus("backslash-traversal");
    const std::string traversal = "..\\escape.bin";
    copy_traversal_target(corpus, traversal);
    replace_field(corpus, 0u, 2u, traversal);
    expect_load_error(corpus,
                      "backslash traversal",
                      {"manifest line 2", "playable_ines_metadata", "blob"});
}

void test_noncanonical_case_id(const ScratchRoot& scratch)
{
    const fs::path corpus = scratch.copy_corpus("case-id");
    replace_field(corpus, 0u, 1u, "Bad-Case");
    expect_load_error(corpus, "case ID grammar", {"manifest line 2", "case_id", "Bad-Case"});
}

void test_negative_unsigned_number(const ScratchRoot& scratch)
{
    const fs::path corpus = scratch.copy_corpus("negative-unsigned");
    replace_field(corpus, 0u, 8u, "-1");
    expect_load_error(corpus,
                      "negative unsigned number",
                      {"manifest line 2", "playable_ines_metadata", "expected_bytes"});
}

void test_signed_unsigned_number(const ScratchRoot& scratch)
{
    const fs::path corpus = scratch.copy_corpus("signed-unsigned");
    replace_field(corpus, 0u, 8u, "+16400");
    expect_load_error(corpus,
                      "signed unsigned number",
                      {"manifest line 2", "playable_ines_metadata", "expected_bytes"});
}

void test_numeric_trailing_junk(const ScratchRoot& scratch)
{
    const fs::path corpus = scratch.copy_corpus("numeric-junk");
    replace_field(corpus, 0u, 8u, "16400junk");
    expect_load_error(corpus,
                      "numeric trailing junk",
                      {"manifest line 2", "playable_ines_metadata", "expected_bytes"});
}

void test_noncanonical_signed_mapper(const ScratchRoot& scratch)
{
    const fs::path corpus = scratch.copy_corpus("signed-mapper");
    replace_field(corpus, 0u, 12u, "+65");
    expect_load_error(corpus,
                      "noncanonical signed mapper",
                      {"manifest line 2", "playable_ines_metadata", "mapper"});
}

void test_negative_disk_sides(const ScratchRoot& scratch)
{
    const fs::path corpus = scratch.copy_corpus("negative-disk-sides");
    replace_field(corpus, 0u, 16u, "-1");
    expect_load_error(corpus,
                      "negative disk sides",
                      {"manifest line 2", "playable_ines_metadata", "disk_sides"});
}

void test_malformed_sha256(const ScratchRoot& scratch)
{
    const fs::path corpus = scratch.copy_corpus("malformed-sha");
    replace_field(corpus,
                  0u,
                  3u,
                  "85F249036032ADB3BA8ACD317594538EA78FD3222663287D846F83635D38284A");
    expect_load_error(corpus,
                      "malformed SHA-256",
                      {"manifest line 2", "playable_ines_metadata", "sha256"});
}

void test_duplicate_case_id(const ScratchRoot& scratch)
{
    const fs::path corpus = scratch.copy_corpus("duplicate-case");
    replace_field(corpus, 1u, 1u, "playable_ines_metadata");
    expect_load_error(corpus,
                      "duplicate case ID",
                      {"manifest line 3", "playable_ines_metadata", "case_id", "duplicate"});
}

void test_duplicate_blob_name(const ScratchRoot& scratch)
{
    const fs::path corpus = scratch.copy_corpus("duplicate-blob");
    replace_field(corpus, 1u, 2u, "playable_ines_metadata.bin");
    expect_load_error(corpus,
                      "duplicate blob name",
                      {"manifest line 3", "dirty_header_ines", "blob", "duplicate"});
}

void test_unexpected_orphan_file(const ScratchRoot& scratch)
{
    const fs::path corpus = scratch.copy_corpus("orphan-file");
    std::ofstream orphan(corpus / "orphan.bin", std::ios::binary);
    orphan.put('x');
    orphan.close();
    expect_load_error(corpus,
                      "unexpected orphan file",
                      {"corpus entry", "orphan.bin", "unexpected"});
}

void test_unexpected_subdirectory(const ScratchRoot& scratch)
{
    const fs::path corpus = scratch.copy_corpus("subdirectory");
    fs::create_directory(corpus / "nested");
    expect_load_error(corpus,
                      "unexpected subdirectory",
                      {"corpus entry", "nested", "regular file"});
}

void test_missing_referenced_blob(const ScratchRoot& scratch)
{
    const fs::path corpus = scratch.copy_corpus("missing-blob");
    fs::remove(corpus / "playable_ines_metadata.bin");
    expect_load_error(corpus,
                      "missing referenced blob",
                      {"manifest line 2", "playable_ines_metadata", "blob", "missing"});
}

void test_referenced_blob_is_directory(const ScratchRoot& scratch)
{
    const fs::path corpus = scratch.copy_corpus("referenced-blob-directory");
    const fs::path blob = corpus / "playable_ines_metadata.bin";
    fs::remove(blob);
    fs::create_directory(blob);
    expect_load_error(corpus,
                      "referenced blob is a directory",
                      {"manifest line 2", "playable_ines_metadata", "blob", "regular"});
}

} // namespace

int main()
{
    try
    {
        const ScratchRoot scratch;
        test_slash_traversal(scratch);
        test_backslash_traversal(scratch);
        test_noncanonical_case_id(scratch);
        test_negative_unsigned_number(scratch);
        test_signed_unsigned_number(scratch);
        test_numeric_trailing_junk(scratch);
        test_noncanonical_signed_mapper(scratch);
        test_negative_disk_sides(scratch);
        test_malformed_sha256(scratch);
        test_duplicate_case_id(scratch);
        test_duplicate_blob_name(scratch);
        test_unexpected_orphan_file(scratch);
        test_unexpected_subdirectory(scratch);
        test_missing_referenced_blob(scratch);
        test_referenced_blob_is_directory(scratch);
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "FAIL: fixture-loader test harness: %s\n", failure.what());
        ++failures;
    }

    if (failures == 0)
    {
        std::puts("flynes_rom_fixture_loader_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
