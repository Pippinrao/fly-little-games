#include "identity_fixture.hpp"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef FLYNES_IDENTITY_FIXTURE_DIR
#error "FLYNES_IDENTITY_FIXTURE_DIR must identify the shared identity fixture directory"
#endif

#ifndef FLYNES_IDENTITY_FIXTURE_TEST_DIR
#error "FLYNES_IDENTITY_FIXTURE_TEST_DIR must identify a private scratch directory"
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

std::vector<std::string> read_lines(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("could not read temporary manifest");
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return split(contents.str(), '\n');
}

void write_lines(const fs::path& path, const std::vector<std::string>& lines)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error("could not write temporary manifest");
    }
    for (std::size_t index = 0u; index < lines.size(); ++index)
    {
        if (index != 0u)
        {
            output.put('\n');
        }
        output << lines[index];
    }
}

void replace_field(const fs::path& manifest,
                   std::size_t row,
                   std::size_t column,
                   const std::string& value)
{
    std::vector<std::string> lines = read_lines(manifest);
    const std::size_t line_index = row + 1u;
    if (line_index >= lines.size())
    {
        throw std::runtime_error("temporary row is out of range");
    }
    std::vector<std::string> fields = split(lines[line_index], '\t');
    if (column >= fields.size())
    {
        throw std::runtime_error("temporary column is out of range");
    }
    fields[column] = value;
    std::string rebuilt;
    for (std::size_t index = 0u; index < fields.size(); ++index)
    {
        if (index != 0u)
        {
            rebuilt.push_back('\t');
        }
        rebuilt += fields[index];
    }
    lines[line_index] = rebuilt;
    write_lines(manifest, lines);
}

class ScratchRoot final
{
public:
    ScratchRoot()
        : path_(FLYNES_IDENTITY_FIXTURE_TEST_DIR)
    {
        if (path_.filename() != "identity-fixture-loader-tests")
        {
            throw std::runtime_error("unexpected fixture-loader scratch directory");
        }
        std::error_code error;
        fs::remove_all(path_, error);
        if (error || (!fs::create_directories(path_, error) && error))
        {
            throw std::runtime_error("could not initialize fixture-loader scratch directory");
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
        for (const fs::directory_entry& entry :
             fs::directory_iterator(FLYNES_IDENTITY_FIXTURE_DIR))
        {
            if (!entry.is_regular_file())
            {
                throw std::runtime_error("tracked identity corpus contains a non-file entry");
            }
            fs::copy_file(entry.path(), corpus / entry.path().filename());
        }
        return corpus;
    }

private:
    fs::path path_;
};

void expect_error(const fs::path& corpus,
                  const char* test_name,
                  std::initializer_list<const char*> fragments)
{
    try
    {
        static_cast<void>(flynes::test::load_identity_fixtures(corpus.string()));
        check(false, test_name, "loader accepted malformed identity corpus");
    }
    catch (const std::exception& failure)
    {
        const std::string message = failure.what();
        for (const char* fragment : fragments)
        {
            check(message.find(fragment) != std::string::npos,
                  test_name,
                  "error did not contain '" + std::string(fragment) + "': " + message);
        }
    }
}

void test_hash_schema_and_header(const ScratchRoot& scratch)
{
    fs::path corpus = scratch.copy_corpus("hash-header");
    replace_field(corpus / "hashes.tsv", 0u, 0u, "2");
    expect_error(corpus, "hash schema", {"schema_version", "expected '1'"});

    corpus = scratch.copy_corpus("hash-columns");
    std::vector<std::string> lines = read_lines(corpus / "hashes.tsv");
    lines[1] += "\textra";
    write_lines(corpus / "hashes.tsv", lines);
    expect_error(corpus, "hash columns", {"columns", "exactly 8"});
}

void test_manifest_lf_and_case_sets(const ScratchRoot& scratch)
{
    fs::path corpus = scratch.copy_corpus("crlf");
    std::ifstream input(corpus / "stable_ids.tsv", std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    std::string crlf = contents.str();
    std::size_t offset = 0u;
    while ((offset = crlf.find('\n', offset)) != std::string::npos)
    {
        crlf.replace(offset, 1u, "\r\n");
        offset += 2u;
    }
    std::ofstream output(corpus / "stable_ids.tsv", std::ios::binary | std::ios::trunc);
    output << crlf;
    output.close();
    expect_error(corpus, "manifest LF", {"strict ASCII", "LF"});

    corpus = scratch.copy_corpus("missing-stable-case");
    std::vector<std::string> lines = read_lines(corpus / "stable_ids.tsv");
    lines.erase(lines.begin() + 1);
    write_lines(corpus / "stable_ids.tsv", lines);
    expect_error(corpus, "stable case set", {"stable-ID case set", "mismatch"});
}

void test_canonical_fields_and_outcome(const ScratchRoot& scratch)
{
    fs::path corpus = scratch.copy_corpus("unsafe-blob");
    replace_field(corpus / "hashes.tsv", 0u, 2u, "../empty.bin");
    expect_error(corpus, "unsafe blob", {"blob", "safe basename"});

    corpus = scratch.copy_corpus("hash-case");
    replace_field(corpus / "hashes.tsv", 0u, 4u,
                  "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855");
    expect_error(corpus, "canonical blob hash", {"blob_sha256", "lowercase"});

    corpus = scratch.copy_corpus("stable-arg-case");
    replace_field(corpus / "stable_ids.tsv", 0u, 3u, "736F757263652D61");
    expect_error(corpus, "stable argument hex", {"arg0_utf8_hex", "lowercase"});

    corpus = scratch.copy_corpus("stable-outcome");
    replace_field(corpus / "stable_ids.tsv", 0u, 8u, "unexpected message");
    expect_error(corpus, "stable outcome", {"outcome", "SUCCESS"});
}

void test_exact_files_and_digest(const ScratchRoot& scratch)
{
    fs::path corpus = scratch.copy_corpus("changed-blob");
    std::ofstream changed(corpus / "abc.bin", std::ios::binary | std::ios::trunc);
    changed << "abd";
    changed.close();
    expect_error(corpus, "changed blob", {"blob_sha256", "digest"});

    corpus = scratch.copy_corpus("orphan");
    std::ofstream orphan(corpus / "orphan.bin", std::ios::binary);
    orphan << "orphan";
    orphan.close();
    expect_error(corpus, "orphan file", {"orphan.bin", "unexpected"});

    corpus = scratch.copy_corpus("subdirectory");
    fs::create_directory(corpus / "nested");
    expect_error(corpus, "subdirectory", {"nested", "regular non-symlink"});
}

} // namespace

int main()
{
    try
    {
        const ScratchRoot scratch;
        test_hash_schema_and_header(scratch);
        test_manifest_lf_and_case_sets(scratch);
        test_canonical_fields_and_outcome(scratch);
        test_exact_files_and_digest(scratch);
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "FAIL: identity fixture-loader harness: %s\n", failure.what());
        ++failures;
    }
    if (failures == 0)
    {
        std::puts("flynes_identity_fixture_loader_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
