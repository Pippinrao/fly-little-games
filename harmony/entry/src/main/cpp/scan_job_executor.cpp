#include "scan_job_executor.hpp"

#include <flynes/flynes_app.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace flynes::harmony {
namespace {

struct ScanCloser final
{
    void operator()(fly_scan_t* scan) const noexcept { fly_scan_abort(scan); }
};

int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    throw std::invalid_argument("source UUID contains a non-hexadecimal digit");
}

std::array<std::uint8_t, 16> parse_uuid(const std::string& value)
{
    if (value.size() != 32u) throw std::invalid_argument("source UUID must have 32 digits");
    std::array<std::uint8_t, 16> uuid{};
    bool nonzero = false;
    for (std::size_t index = 0; index < uuid.size(); ++index)
    {
        uuid[index] = static_cast<std::uint8_t>(
            (hex_nibble(value[index * 2u]) << 4) | hex_nibble(value[index * 2u + 1u]));
        nonzero = nonzero || uuid[index] != 0u;
    }
    if (!nonzero) throw std::invalid_argument("source UUID must not be all zero");
    return uuid;
}

std::runtime_error fly_error(const char* operation, fly_result result)
{
    return std::runtime_error(std::string(operation) + " failed: fly_result=" +
                              std::to_string(static_cast<int>(result)));
}

ScanExecutionResult run_once(
    fly_app_t& app,
    const ScanJobRequest& request,
    const std::array<std::uint8_t, 16>& uuid,
    const ScanJobQueue::CancelCheck& cancelled,
    const ScanJobQueue::ProgressSink& progress,
    bool* conflict)
{
    *conflict = false;
    fly_scan_config config{};
    config.struct_size = FLY_SCAN_CONFIG_V1_SIZE;
    config.version = FLY_SCAN_CONFIG_VERSION_1;
    config.source_scope = request.source_scope;
    for (std::size_t index = 0; index < uuid.size(); ++index) config.source_uuid[index] = uuid[index];

    fly_scan_t* raw_scan = nullptr;
    const fly_result begin = fly_scan_begin(&app, &config, &raw_scan);
    if (begin != FLY_RESULT_OK) throw fly_error("fly_scan_begin", begin);
    std::unique_ptr<fly_scan_t, ScanCloser> scan(raw_scan);
    std::uint64_t result_count = 0u;
    std::uint64_t processed = 0u;
    for (const ScanJobFile& candidate : request.files)
    {
        if (cancelled()) return ScanExecutionResult::cancelled();
        fly_scan_file file{};
        file.struct_size = FLY_SCAN_FILE_V1_SIZE;
        file.version = FLY_SCAN_FILE_VERSION_1;
        file.source_relative_path_utf8 = candidate.relative_path.data();
        file.display_name_utf8 = candidate.display_name.data();
        file.source_relative_path_utf8_length =
            static_cast<std::uint32_t>(candidate.relative_path.size());
        file.display_name_utf8_length =
            static_cast<std::uint32_t>(candidate.display_name.size());
        file.borrowed_fd = candidate.owned_fd;
        file.declared_size = candidate.declared_size;
        fly_scan_file_result file_result{};
        file_result.struct_size = FLY_SCAN_FILE_RESULT_V1_SIZE;
        file_result.version = FLY_SCAN_FILE_RESULT_VERSION_1;
        const fly_result added = fly_scan_add_file(scan.get(), &file, &file_result);
        if (added != FLY_RESULT_OK) throw fly_error("fly_scan_add_file", added);
        ++processed;
        result_count += file_result.variant_count;
        progress(processed, result_count);
        if (cancelled()) return ScanExecutionResult::cancelled();
    }

    fly_scan_t* committing = scan.release();
    const fly_result commit = fly_scan_commit(committing, request.final_completeness);
    fly_scan_abort(committing);
    if (commit == FLY_RESULT_CONFLICT)
    {
        *conflict = true;
        return ScanExecutionResult::failed("catalog generation conflict");
    }
    if (commit != FLY_RESULT_OK) throw fly_error("fly_scan_commit", commit);
    return ScanExecutionResult::completed(result_count);
}

} // namespace

ScanExecutionResult execute_scan_job(
    fly_app_t& app,
    const ScanJobRequest& request,
    const ScanJobQueue::CancelCheck& cancelled,
    const ScanJobQueue::ProgressSink& progress)
{
    const auto uuid = parse_uuid(request.source_uuid_hex);
    bool conflict = false;
    ScanExecutionResult result = run_once(app, request, uuid, cancelled, progress, &conflict);
    if (!conflict || cancelled()) return result;
    return run_once(app, request, uuid, cancelled, progress, &conflict);
}

} // namespace flynes::harmony
