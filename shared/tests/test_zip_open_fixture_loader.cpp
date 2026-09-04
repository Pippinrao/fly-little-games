#include "zip_open_fixture.hpp"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifndef FLYNES_ZIP_OPEN_FIXTURE_DIR
#error "FLYNES_ZIP_OPEN_FIXTURE_DIR must identify the shared ZIP-open fixture directory"
#endif

#ifndef FLYNES_ZIP_OPEN_FIXTURE_TEST_DIR
#error "FLYNES_ZIP_OPEN_FIXTURE_TEST_DIR must identify a disposable build directory"
#endif

namespace {

namespace fs = std::filesystem;

int failures = 0;

void check(bool condition, const char* label)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", label);
        ++failures;
    }
}

class ScratchRoot final
{
public:
    ScratchRoot()
        : path_(FLYNES_ZIP_OPEN_FIXTURE_TEST_DIR)
    {
        std::error_code error;
        fs::remove_all(path_, error);
        if (error)
        {
            throw std::runtime_error("could not clear ZIP fixture-loader scratch root");
        }
        fs::create_directories(path_);
    }

    ~ScratchRoot()
    {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    fs::path copy_corpus(const std::string& test_name) const
    {
        const fs::path destination = path_ / test_name;
        fs::create_directories(destination);
        for (const fs::directory_entry& entry : fs::directory_iterator(FLYNES_ZIP_OPEN_FIXTURE_DIR))
        {
            fs::copy_file(entry.path(),
                          destination / entry.path().filename(),
                          fs::copy_options::overwrite_existing);
        }
        return destination;
    }

private:
    fs::path path_;
};

std::vector<std::string> read_manifest(const fs::path& corpus)
{
    std::ifstream input(corpus / "manifest.tsv", std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("could not read temporary ZIP manifest");
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

void write_manifest(const fs::path& corpus, const std::vector<std::string>& lines)
{
    std::ofstream output(corpus / "manifest.tsv", std::ios::binary | std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error("could not write temporary ZIP manifest");
    }
    for (const std::string& line : lines)
    {
        output << line << '\n';
    }
    if (!output)
    {
        throw std::runtime_error("could not finish temporary ZIP manifest");
    }
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

std::string join(const std::vector<std::string>& fields, char delimiter)
{
    std::string result;
    for (std::size_t index = 0u; index < fields.size(); ++index)
    {
        if (index != 0u)
        {
            result.push_back(delimiter);
        }
        result += fields[index];
    }
    return result;
}

void replace_field(const fs::path& corpus,
                   std::size_t row,
                   std::size_t column,
                   const std::string& replacement)
{
    std::vector<std::string> lines = read_manifest(corpus);
    if (row >= lines.size())
    {
        throw std::runtime_error("temporary ZIP manifest row is out of range");
    }
    std::vector<std::string> fields = split(lines[row], '\t');
    if (column >= fields.size())
    {
        throw std::runtime_error("temporary ZIP manifest column is out of range");
    }
    fields[column] = replacement;
    lines[row] = join(fields, '\t');
    write_manifest(corpus, lines);
}

void expect_load_error(const fs::path& corpus,
                       const std::string& label,
                       const std::vector<std::string>& required_fragments)
{
    try
    {
        static_cast<void>(flynes::test::load_zip_open_fixtures(corpus.string()));
        std::fprintf(stderr, "FAIL: %s: loader unexpectedly accepted corpus\n", label.c_str());
        ++failures;
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        for (const std::string& fragment : required_fragments)
        {
            if (message.find(fragment) == std::string::npos)
            {
                std::fprintf(stderr,
                             "FAIL: %s: message '%s' lacks '%s'\n",
                             label.c_str(),
                             message.c_str(),
                             fragment.c_str());
                ++failures;
            }
        }
    }
}

void flip_first_byte(const fs::path& path)
{
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file)
    {
        throw std::runtime_error("could not mutate temporary ZIP blob");
    }
    char value = 0;
    file.read(&value, 1);
    value = static_cast<char>(static_cast<unsigned char>(value) ^ 1u);
    file.seekp(0, std::ios::beg);
    file.write(&value, 1);
}

} // namespace

int main()
{
    try
    {
        const ScratchRoot scratch;

        const std::vector<flynes::test::ZipOpenFixture> baseline =
            flynes::test::load_zip_open_fixtures(FLYNES_ZIP_OPEN_FIXTURE_DIR);
        check(baseline.size() == 60u, "baseline exact corpus loads");

        fs::path corpus = scratch.copy_corpus("unsafe_path");
        replace_field(corpus, 1u, 2u, "../escape.zip");
        expect_load_error(corpus, "unsafe path", {"manifest line 2", "blob", "safe basename"});

        corpus = scratch.copy_corpus("missing_blob");
        fs::remove(corpus / "valid_stored_metadata.zip");
        expect_load_error(corpus,
                          "missing blob",
                          {"valid_stored_metadata.zip", "missing"});

        corpus = scratch.copy_corpus("changed_blob");
        flip_first_byte(corpus / "valid_stored_metadata.zip");
        expect_load_error(corpus,
                          "changed blob",
                          {"manifest line 2", "valid_stored_metadata", "sha256", "digest"});

        corpus = scratch.copy_corpus("extra_blob");
        std::ofstream(corpus / "orphan.zip", std::ios::binary) << "orphan";
        expect_load_error(corpus, "extra blob", {"orphan.zip", "unexpected"});

        corpus = scratch.copy_corpus("bad_schema");
        replace_field(corpus, 1u, 0u, "2");
        expect_load_error(corpus,
                          "schema version",
                          {"manifest line 2", "schema_version", "expected '1'"});

        corpus = scratch.copy_corpus("missing_hash");
        replace_field(corpus, 1u, 3u, "");
        expect_load_error(corpus,
                          "missing hash",
                          {"manifest line 2", "sha256", "64 lowercase"});

        corpus = scratch.copy_corpus("invalid_limit");
        replace_field(corpus, 1u, 4u, "0");
        expect_load_error(corpus,
                          "invalid limit",
                          {"manifest line 2", "max_package_bytes", "allowed range"});

        corpus = scratch.copy_corpus("invalid_entries");
        replace_field(corpus, 1u, 14u, "bad");
        expect_load_error(corpus,
                          "invalid entries",
                          {"manifest line 2", "entries", "9 pipe-separated"});

        corpus = scratch.copy_corpus("duplicate_case");
        std::vector<std::string> lines = read_manifest(corpus);
        lines.push_back(lines[1]);
        write_manifest(corpus, lines);
        expect_load_error(corpus,
                          "duplicate case",
                          {"manifest line 62", "case_id", "duplicate"});
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: fixture-loader test harness: %s\n", error.what());
        ++failures;
    }

    if (failures == 0)
    {
        std::puts("flynes_zip_open_fixture_loader_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
