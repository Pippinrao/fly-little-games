#include "zip_payload_fixture.hpp"

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

#ifndef FLYNES_ZIP_PAYLOAD_FIXTURE_DIR
#error "FLYNES_ZIP_PAYLOAD_FIXTURE_DIR must identify the shared ZIP-payload fixture directory"
#endif

#ifndef FLYNES_ZIP_PAYLOAD_FIXTURE_TEST_DIR
#error "FLYNES_ZIP_PAYLOAD_FIXTURE_TEST_DIR must identify a disposable build directory"
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
        : path_(FLYNES_ZIP_PAYLOAD_FIXTURE_TEST_DIR)
    {
        std::error_code error;
        fs::remove_all(path_, error);
        if (error)
        {
            throw std::runtime_error("could not clear ZIP-payload fixture scratch root");
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
        for (const fs::directory_entry& entry :
             fs::directory_iterator(FLYNES_ZIP_PAYLOAD_FIXTURE_DIR))
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

std::vector<std::string> read_manifest(const fs::path& corpus)
{
    std::ifstream input(corpus / "manifest.tsv", std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("could not read temporary ZIP-payload manifest");
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
        throw std::runtime_error("could not write temporary ZIP-payload manifest");
    }
    for (const std::string& line : lines)
    {
        output << line << '\n';
    }
}

void replace_field(const fs::path& corpus,
                   std::size_t row,
                   std::size_t column,
                   const std::string& replacement)
{
    std::vector<std::string> lines = read_manifest(corpus);
    std::vector<std::string> fields = split(lines.at(row), '\t');
    fields.at(column) = replacement;
    lines[row] = join(fields, '\t');
    write_manifest(corpus, lines);
}

void expect_load_error(const fs::path& corpus,
                       const std::string& label,
                       const std::vector<std::string>& fragments)
{
    try
    {
        static_cast<void>(flynes::test::load_zip_payload_fixtures(corpus.string()));
        std::fprintf(stderr, "FAIL: %s: loader unexpectedly accepted corpus\n", label.c_str());
        ++failures;
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        for (const std::string& fragment : fragments)
        {
            if (message.find(fragment) == std::string::npos)
            {
                std::fprintf(stderr,
                             "FAIL: %s: message '%s' lacks '%s'\n",
                             label.c_str(), message.c_str(), fragment.c_str());
                ++failures;
            }
        }
    }
}

void expect_load_success(const fs::path& corpus, const std::string& label)
{
    try
    {
        static_cast<void>(flynes::test::load_zip_payload_fixtures(corpus.string()));
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: %s: loader rejected corpus: %s\n",
                     label.c_str(), error.what());
        ++failures;
    }
}

void flip_first_byte(const fs::path& path)
{
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
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
        const auto baseline =
            flynes::test::load_zip_payload_fixtures(FLYNES_ZIP_PAYLOAD_FIXTURE_DIR);
        check(baseline.size() == 19u, "baseline exact ZIP-payload corpus loads");

        fs::path corpus = scratch.copy_corpus("unsafe_path");
        replace_field(corpus, 1u, 2u, "../escape.zip");
        expect_load_error(corpus, "unsafe blob path", {"manifest line 2", "blob", "safe"});

        corpus = scratch.copy_corpus("invalid_case");
        replace_field(corpus, 1u, 1u, "0case");
        expect_load_error(corpus, "invalid case", {"case_id", "[a-z][a-z0-9_]*"});

        const std::vector<std::string> invalid_selectors = {"", "-", "A0", "abc", "zz"};
        for (const std::string& selector : invalid_selectors)
        {
            corpus = scratch.copy_corpus("invalid_selector_" + std::to_string(selector.size()));
            replace_field(corpus, 1u, 11u, selector);
            expect_load_error(corpus, "invalid selector", {"selector_raw_name_hex", "lowercase"});
        }
        corpus = scratch.copy_corpus("oversized_selector");
        replace_field(corpus, 1u, 11u, std::string(0x10000u * 2u, 'a'));
        expect_load_error(corpus, "oversized selector",
                          {"selector_raw_name_hex", "ZIP name field"});

        corpus = scratch.copy_corpus("missing_blob");
        fs::remove(corpus / "stored_exact_limit_local_extra.zip");
        expect_load_error(corpus, "missing blob", {"stored_exact_limit_local_extra.zip", "missing"});

        corpus = scratch.copy_corpus("changed_blob");
        flip_first_byte(corpus / "stored_exact_limit_local_extra.zip");
        expect_load_error(corpus, "changed blob", {"sha256", "digest"});

        corpus = scratch.copy_corpus("extra_blob");
        std::ofstream(corpus / "orphan.zip", std::ios::binary) << "orphan";
        expect_load_error(corpus, "extra blob", {"orphan.zip", "unexpected"});

        const std::vector<std::size_t> long_columns = {4u, 5u, 7u, 10u};
        for (const std::size_t column : long_columns)
        {
            corpus = scratch.copy_corpus("long_max_" + std::to_string(column));
            replace_field(corpus, 1u, column, "9223372036854775807");
            expect_load_success(corpus, "Java long maximum accepted");
            corpus = scratch.copy_corpus("long_over_" + std::to_string(column));
            replace_field(corpus, 1u, column, "9223372036854775808");
            expect_load_error(corpus, "Java long overflow", {"allowed range"});
        }

        const std::vector<std::size_t> int_columns = {6u, 9u, 12u, 16u};
        for (const std::size_t column : int_columns)
        {
            corpus = scratch.copy_corpus("int_max_" + std::to_string(column));
            replace_field(corpus, 1u, column, "2147483647");
            expect_load_success(corpus, "Java int maximum accepted");
            corpus = scratch.copy_corpus("int_over_" + std::to_string(column));
            replace_field(corpus, 1u, column, "2147483648");
            expect_load_error(corpus, "Java int overflow", {"allowed range"});
        }

        for (const std::string& value : {"+1", "01", "-0"})
        {
            corpus = scratch.copy_corpus("noncanonical_" + std::to_string(value.size()));
            replace_field(corpus, 1u, 4u, value);
            expect_load_error(corpus, "noncanonical integer",
                              {"max_package_bytes", "canonical unsigned decimal"});
        }

        corpus = scratch.copy_corpus("bad_success_error");
        replace_field(corpus, 1u, 14u, "INVALID_ZIP");
        expect_load_error(corpus, "success with error", {"SUCCESS", "NONE"});

        corpus = scratch.copy_corpus("bad_success_hash");
        replace_field(corpus, 1u, 17u, "ABC");
        expect_load_error(corpus, "bad payload hash", {"payload_sha256", "lowercase"});

        corpus = scratch.copy_corpus("blank_error");
        replace_field(corpus, 6u, 15u, "   ");
        expect_load_error(corpus, "blank error", {"error_message", "nonblank"});

        corpus = scratch.copy_corpus("error_payload");
        replace_field(corpus, 6u, 16u, "0");
        expect_load_error(corpus, "error with payload", {"ERROR", "NONE payload"});

        corpus = scratch.copy_corpus("duplicate_case");
        std::vector<std::string> lines = read_manifest(corpus);
        lines.push_back(lines[1]);
        write_manifest(corpus, lines);
        expect_load_error(corpus, "duplicate case", {"case_id", "duplicate"});
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: fixture-loader harness: %s\n", error.what());
        ++failures;
    }

    if (failures == 0)
    {
        std::puts("flynes_zip_payload_fixture_loader_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
