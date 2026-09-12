#include "scan_job_queue.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

int failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename Predicate>
bool wait_until(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return predicate();
}

flynes::harmony::ScanJobRequest request_for(std::string source, int fd)
{
    flynes::harmony::ScanJobRequest request;
    request.source_uuid_hex = std::move(source);
    request.source_scope = 4u;
    request.final_completeness = 1u;
    request.files.push_back({"slow.zip", "slow.zip", fd, 1024u});
    return request;
}

void test_same_source_is_merged_and_every_fd_is_closed()
{
    std::mutex gate_mutex;
    std::condition_variable gate_changed;
    bool release = false;
    std::atomic<int> runs{0};
    std::vector<int> closed;
    std::mutex closed_mutex;

    flynes::harmony::ScanJobQueue queue(
        [&](const flynes::harmony::ScanJobRequest&,
            const flynes::harmony::ScanJobQueue::CancelCheck& cancelled,
            const flynes::harmony::ScanJobQueue::ProgressSink& progress) {
            ++runs;
            std::unique_lock<std::mutex> lock(gate_mutex);
            gate_changed.wait(lock, [&]() { return release || cancelled(); });
            if (cancelled())
            {
                return flynes::harmony::ScanExecutionResult::cancelled();
            }
            progress(1u, 2u);
            return flynes::harmony::ScanExecutionResult::completed(2u);
        },
        [&](int fd) {
            std::lock_guard<std::mutex> lock(closed_mutex);
            closed.push_back(fd);
        });

    const std::uint64_t first = queue.start(request_for("11111111111111111111111111111111", 10));
    expect(wait_until([&]() { return queue.status(first).phase == flynes::harmony::ScanJobPhase::RUNNING; }),
           "first scan reaches RUNNING");
    const std::uint64_t merged = queue.start(request_for("11111111111111111111111111111111", 11));
    expect(merged == first, "same source request returns the active job id");

    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        release = true;
    }
    gate_changed.notify_all();
    expect(wait_until([&]() { return queue.status(first).terminal(); }), "merged scan completes");
    const auto status = queue.status(first);
    expect(status.phase == flynes::harmony::ScanJobPhase::COMPLETED, "scan completes successfully");
    expect(status.processed_files == 1u, "progress records processed files");
    expect(status.result_count == 2u, "completion records result count");
    expect(runs.load() == 1, "same source is executed once");
    expect(wait_until([&]() {
        std::lock_guard<std::mutex> lock(closed_mutex);
        return closed.size() == 2u;
    }), "both the active and merged request descriptors are closed");
}

void test_different_sources_are_serial_and_cancel_at_boundary()
{
    std::mutex mutex;
    std::condition_variable changed;
    bool release_first = false;
    std::vector<std::string> started;
    std::vector<int> closed;

    flynes::harmony::ScanJobQueue queue(
        [&](const flynes::harmony::ScanJobRequest& request,
            const flynes::harmony::ScanJobQueue::CancelCheck& cancelled,
            const flynes::harmony::ScanJobQueue::ProgressSink&) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                started.push_back(request.source_uuid_hex);
            }
            changed.notify_all();
            if (request.source_uuid_hex[0] == 'a')
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&]() { return release_first; });
            }
            if (cancelled()) return flynes::harmony::ScanExecutionResult::cancelled();
            return flynes::harmony::ScanExecutionResult::completed(1u);
        },
        [&](int fd) {
            std::lock_guard<std::mutex> lock(mutex);
            closed.push_back(fd);
        });

    const auto first = queue.start(request_for("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 20));
    const auto second = queue.start(request_for("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 21));
    expect(wait_until([&]() { return queue.status(first).phase == flynes::harmony::ScanJobPhase::RUNNING; }),
           "first source starts");
    expect(queue.status(second).phase == flynes::harmony::ScanJobPhase::QUEUED,
           "different source remains queued");
    expect(queue.cancel_source("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 4u) == 1u,
           "source removal cancels its queued job");
    expect(wait_until([&]() { return queue.status(second).terminal(); }), "queued cancellation becomes terminal");
    expect(queue.status(second).phase == flynes::harmony::ScanJobPhase::CANCELLED,
           "queued job is cancelled without execution");

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_first = true;
    }
    changed.notify_all();
    expect(wait_until([&]() { return queue.status(first).terminal(); }), "first source completes");
    {
        std::lock_guard<std::mutex> lock(mutex);
        expect(started.size() == 1u, "cancelled queued source never executes");
    }
    expect(!queue.cancel(first), "terminal job rejects cancellation");
    expect(wait_until([&]() {
        std::lock_guard<std::mutex> lock(mutex);
        return closed.size() == 2u;
    }), "all descriptors close after completion or cancellation");
}

} // namespace

int main()
{
    test_same_source_is_merged_and_every_fd_is_closed();
    test_different_sources_are_serial_and_cancel_at_boundary();
    if (failures != 0)
    {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: Harmony scan job queue\n";
    return EXIT_SUCCESS;
}
