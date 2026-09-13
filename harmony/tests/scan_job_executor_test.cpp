#include "scan_job_executor.hpp"
#include "scan_job_queue.hpp"

#include <flynes/flynes_app.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

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

int open_read_only(const std::filesystem::path& path)
{
#ifdef _WIN32
    int fd = -1;
    if (_wsopen_s(&fd, path.c_str(), _O_RDONLY | _O_BINARY, _SH_DENYNO, 0) != 0) return -1;
    return fd;
#else
    return open(path.c_str(), O_RDONLY);
#endif
}

void close_fd(int fd)
{
#ifdef _WIN32
    _close(fd);
#else
    close(fd);
#endif
}

struct App final
{
    fly_app_t* value = nullptr;
    ~App() { fly_app_destroy(value); }
};

void test_executor_indexes_real_rom_off_caller_thread()
{
    const auto root = std::filesystem::temp_directory_path() /
        ("flynes-harmony-scan-job-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "data");
    std::filesystem::create_directories(root / "cache");

    const std::string data = (root / "data").string();
    const std::string cache = (root / "cache").string();
    const fly_platform_capabilities capabilities{
        FLY_PLATFORM_CAPABILITIES_V1_SIZE, FLY_PLATFORM_CAPABILITIES_VERSION_1, 0u};
    const fly_app_config config{
        FLY_APP_CONFIG_V1_SIZE, FLY_APP_CONFIG_VERSION_1,
        data.data(), cache.data(), &capabilities,
        static_cast<std::uint32_t>(data.size()), static_cast<std::uint32_t>(cache.size())};
    App app;
    expect(fly_app_create(&config, &app.value) == FLY_RESULT_OK, "create isolated app");

    const std::filesystem::path rom = FLYNES_HARMONY_RUNTIME_ROM_FIXTURE;
    const int fd = open_read_only(rom);
    expect(fd >= 0, "open real ROM fixture");
    flynes::harmony::ScanJobRequest request;
    request.source_uuid_hex = "1234567890abcdef1234567890abcdef";
    request.source_scope = FLY_SOURCE_SCOPE_MANAGED_LIBRARY;
    request.final_completeness = FLY_SCAN_COMPLETENESS_FULL;
    request.files.push_back({"thwaite.nes", "Thwaite", fd,
                             std::filesystem::file_size(rom)});

    const std::thread::id caller = std::this_thread::get_id();
    std::thread::id executor_thread;
    flynes::harmony::ScanJobQueue queue(
        [&](const flynes::harmony::ScanJobRequest& work,
            const flynes::harmony::ScanJobQueue::CancelCheck& cancelled,
            const flynes::harmony::ScanJobQueue::ProgressSink& progress) {
            executor_thread = std::this_thread::get_id();
            return flynes::harmony::execute_scan_job(*app.value, work, cancelled, progress);
        }, close_fd);
    const std::uint64_t id = queue.start(std::move(request));
    flynes::harmony::ScanJobSnapshot status;
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    do
    {
        status = queue.status(id);
        if (status.terminal()) break;
        std::this_thread::sleep_for(5ms);
    } while (std::chrono::steady_clock::now() < deadline);

    expect(status.phase == flynes::harmony::ScanJobPhase::COMPLETED,
           "real scan completes");
    expect(status.processed_files == 1u, "real scan reports one processed file");
    expect(status.result_count == 1u, "real scan reports one indexed variant");
    expect(executor_thread != caller, "scan executor runs off the caller thread");
    fly_catalog_snapshot_t* snapshot = nullptr;
    expect(fly_catalog_snapshot(app.value, &snapshot) == FLY_RESULT_OK, "read catalog snapshot");
    std::uint64_t count = 0u;
    expect(snapshot != nullptr && fly_catalog_snapshot_count(snapshot, &count) == FLY_RESULT_OK,
           "read catalog count");
    expect(count == 1u, "catalog publishes indexed ROM only after commit");
    fly_catalog_snapshot_release(snapshot);
    std::filesystem::remove_all(root);
}

} // namespace

int main()
{
    test_executor_indexes_real_rom_off_caller_thread();
    if (failures != 0)
    {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: Harmony scan job executor\n";
    return EXIT_SUCCESS;
}
