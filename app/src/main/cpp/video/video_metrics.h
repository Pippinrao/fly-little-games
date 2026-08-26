#pragma once

#include <atomic>
#include <cstdint>

namespace flynes::video {

struct VideoMetricsSnapshot {
    std::uint64_t uploads;
    std::uint64_t submissions;
    std::uint64_t skipped_sequences;
    std::int64_t last_sequence;
    std::uint64_t surface_epoch;
};

class VideoMetrics {
public:
    void on_upload() { uploads_.fetch_add(1, std::memory_order_relaxed); }
    void on_submit(std::uint64_t sequence) {
        last_sequence_.store(static_cast<std::int64_t>(sequence), std::memory_order_release);
        submissions_.fetch_add(1, std::memory_order_relaxed);
    }
    void on_skipped(std::uint64_t count) {
        skipped_.fetch_add(count, std::memory_order_relaxed);
    }
    void on_surface_epoch(std::uint64_t epoch) {
        surface_epoch_.store(epoch, std::memory_order_release);
    }
    VideoMetricsSnapshot snapshot() const {
        return {uploads_.load(std::memory_order_relaxed),
                submissions_.load(std::memory_order_relaxed),
                skipped_.load(std::memory_order_relaxed),
                last_sequence_.load(std::memory_order_acquire),
                surface_epoch_.load(std::memory_order_acquire)};
    }

private:
    std::atomic<std::uint64_t> uploads_{0};
    std::atomic<std::uint64_t> submissions_{0};
    std::atomic<std::uint64_t> skipped_{0};
    std::atomic<std::int64_t> last_sequence_{-1};
    std::atomic<std::uint64_t> surface_epoch_{0};
};

}  // namespace flynes::video
