#include "unsupported_payload_fixture.hpp"

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

#ifndef FLYNES_UNSUPPORTED_PAYLOAD_FIXTURE_DIR
#error "FLYNES_UNSUPPORTED_PAYLOAD_FIXTURE_DIR must identify the shared fixture directory"
#endif

#ifndef FLYNES_UNSUPPORTED_PAYLOAD_FIXTURE_TEST_DIR
#error "FLYNES_UNSUPPORTED_PAYLOAD_FIXTURE_TEST_DIR must identify a private scratch directory"
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
    std::vector<std::string> fields = split(lines.at(line_index), '\t');
    fields.at(column) = value;
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
        : path_(FLYNES_UNSUPPORTED_PAYLOAD_FIXTURE_TEST_DIR)
    {
        if (path_.filename() != "unsupported-payload-fixture-loader-tests")
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
             fs::directory_iterator(FLYNES_UNSUPPORTED_PAYLOAD_FIXTURE_DIR))
        {
            if (!entry.is_regular_file())
            {
                throw std::runtime_error("tracked corpus contains a non-file entry");
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
        static_cast<void>(flynes::test::load_unsupported_payload_fixtures(corpus.string()));
        check(false, test_name, "loader accepted malformed unsupported-payload corpus");
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

void test_manifest_contract(const ScratchRoot& scratch)
{
    fs::path corpus = scratch.copy_corpus("schema");
    replace_field(corpus / "manifest.tsv", 0u, 0u, "2");
    expect_error(corpus, "schema", {"schema_version", "expected '1'"});

    corpus = scratch.copy_corpus("columns");
    std::vector<std::string> lines = read_lines(corpus / "manifest.tsv");
    lines[1] += "\textra";
    write_lines(corpus / "manifest.tsv", lines);
    expect_error(corpus, "columns", {"columns", "exactly 6"});

    corpus = scratch.copy_corpus("duplicate-case");
    replace_field(corpus / "manifest.tsv", 1u, 1u, "empty");
    replace_field(corpus / "manifest.tsv", 1u, 2u, "empty.bin");
    expect_error(corpus, "duplicate case", {"case_id", "duplicate"});

    corpus = scratch.copy_corpus("unsafe-blob");
    replace_field(corpus / "manifest.tsv", 0u, 2u, "../empty.bin");
    expect_error(corpus, "unsafe blob", {"blob", "safe basename"});

    corpus = scratch.copy_corpus("hash-case");
    replace_field(
        corpus / "manifest.tsv", 0u, 4u,
        "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855");
    expect_error(corpus, "hash case", {"blob_sha256", "lowercase"});

    corpus = scratch.copy_corpus("reason");
    replace_field(corpus / "manifest.tsv", 0u, 5u, "INDEXED");
    expect_error(corpus, "reason", {"expected_reason", "canonical"});
}

void test_exact_files_and_bytes(const ScratchRoot& scratch)
{
    fs::path corpus = scratch.copy_corpus("changed-blob");
    std::ofstream changed(corpus / "plain_ascii.bin", std::ios::binary | std::ios::trunc);
    changed << 'x';
    changed.close();
    expect_error(corpus, "changed blob", {"blob_size", "mismatch"});

    corpus = scratch.copy_corpus("orphan");
    std::ofstream orphan(corpus / "orphan.bin", std::ios::binary);
    orphan << "orphan";
    orphan.close();
    expect_error(corpus, "orphan", {"corpus", "unexpected", "orphan.bin"});

    corpus = scratch.copy_corpus("directory");
    fs::create_directory(corpus / "nested");
    expect_error(corpus, "directory", {"corpus", "regular non-symlink"});

    corpus = scratch.copy_corpus("crlf");
    std::vector<std::string> lines = read_lines(corpus / "manifest.tsv");
    std::ofstream crlf(corpus / "manifest.tsv", std::ios::binary | std::ios::trunc);
    for (std::size_t index = 0u; index + 1u < lines.size(); ++index)
    {
        crlf << lines[index] << "\r\n";
    }
    crlf.close();
    expect_error(corpus, "CRLF", {"manifest", "LF"});
}

void test_filesystem_aliases(const ScratchRoot& scratch)
{
    fs::path corpus = scratch.copy_corpus("hardlink");
    const fs::path hardlink = corpus / "plain_ascii.bin";
    fs::remove(hardlink);
    std::error_code error;
    fs::create_hard_link(corpus / "lowercase_mz.bin", hardlink, error);
    if (!error)
    {
        expect_error(corpus, "hard link", {"hard link"});
    }

    corpus = scratch.copy_corpus("symlink");
    const fs::path outside = scratch.copy_corpus("outside") / "plain_ascii.bin";
    const fs::path symlink = corpus / "plain_ascii.bin";
    fs::remove(symlink);
    error.clear();
    fs::create_symlink(outside, symlink, error);
    if (!error)
    {
        expect_error(corpus, "symlink", {"regular non-symlink"});
    }

    const fs::path target_root = scratch.copy_corpus("root-link-target");
    const fs::path linked_root = target_root.parent_path() / "corpus-link";
    error.clear();
    fs::create_directory_symlink(target_root, linked_root, error);
    if (!error)
    {
        expect_error(linked_root, "root symlink", {"ordinary non-symlink directory"});
    }
}

} // namespace

int main()
{
    try
    {
        const ScratchRoot scratch;
        test_manifest_contract(scratch);
        test_exact_files_and_bytes(scratch);
        test_filesystem_aliases(scratch);
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "FAIL: unsupported-payload fixture-loader harness: %s\n",
                     failure.what());
        ++failures;
    }
    if (failures == 0)
    {
        std::puts("flynes_unsupported_payload_fixture_loader_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
