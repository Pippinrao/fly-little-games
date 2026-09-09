#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace flynes::harmony {

enum class ScanJobPhase : std::uint32_t
{
    UNKNOWN = 0,
    QUEUED = 1,
    RUNNING = 2,
    COMPLETED = 3,
    CANCELLED = 4,
    FAILED = 5,
};

struct ScanJobFile final
{
    std::string relative_path;
    std::string display_name;
    int owned_fd = -1;
    std::uint64_t declared_size = 0u;
};

struct ScanJobRequest final
{
    std::string source_uuid_hex;
    std::uint32_t source_scope = 0u;
    std::uint32_t final_completeness = 0u;
    std::vector<ScanJobFile> files;
};

struct ScanExecutionResult final
{
    ScanJobPhase phase = ScanJobPhase::FAILED;
    std::uint64_t result_count = 0u;
    std::string error;

    static ScanExecutionResult completed(std::uint64_t count);
    static ScanExecutionResult cancelled();
    static ScanExecutionResult failed(std::string detail);
};

struct ScanJobSnapshot final
{
    std::uint64_t id = 0u;
    ScanJobPhase phase = ScanJobPhase::UNKNOWN;
    std::uint64_t processed_files = 0u;
    std::uint64_t result_count = 0u;
    bool cancel_requested = false;
    std::string error;

    bool terminal() const noexcept;
};

class ScanJobQueue final
{
public:
    using CancelCheck = std::function<bool()>;
    using ProgressSink = std::function<void(std::uint64_t, std::uint64_t)>;
    using Executor = std::function<ScanExecutionResult(
        const ScanJobRequest&, const CancelCheck&, const ProgressSink&)>;
    using FdCloser = std::function<void(int)>;

    ScanJobQueue(Executor executor, FdCloser close_fd);
    ~ScanJobQueue();

    ScanJobQueue(const ScanJobQueue&) = delete;
    ScanJobQueue& operator=(const ScanJobQueue&) = delete;

    std::uint64_t start(ScanJobRequest request);
    ScanJobSnapshot status(std::uint64_t id) const;
    bool cancel(std::uint64_t id);
    std::uint64_t cancel_source(const std::string& source_uuid_hex, std::uint32_t source_scope);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace flynes::harmony
