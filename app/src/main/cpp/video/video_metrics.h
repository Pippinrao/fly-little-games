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
    std::uint64_t runtime_failure_count;
    int runtime_failure_code;
    int gpu_timing_status;
    std::int64_t last_gpu_duration_ns;
    int requested_frame_rate_millihz;
    int frame_rate_vote_status;
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
    void on_runtime_failure(int code) {
        runtime_failure_code_.store(code, std::memory_order_release);
        runtime_failure_count_.fetch_add(1, std::memory_order_relaxed);
    }
    void on_gpu_timing(int status, std::int64_t duration_ns) {
        gpu_timing_status_.store(status, std::memory_order_release);
        last_gpu_duration_ns_.store(duration_ns, std::memory_order_release);
    }
    void on_frame_rate_vote(int requested_millihz, int status) {
        requested_frame_rate_millihz_.store(requested_millihz,
                                             std::memory_order_release);
        frame_rate_vote_status_.store(status, std::memory_order_release);
    }
    VideoMetricsSnapshot snapshot() const {
        return {uploads_.load(std::memory_order_relaxed),
                submissions_.load(std::memory_order_relaxed),
                skipped_.load(std::memory_order_relaxed),
                last_sequence_.load(std::memory_order_acquire),
                surface_epoch_.load(std::memory_order_acquire),
                runtime_failure_count_.load(std::memory_order_relaxed),
                runtime_failure_code_.load(std::memory_order_acquire),
                gpu_timing_status_.load(std::memory_order_acquire),
                last_gpu_duration_ns_.load(std::memory_order_acquire),
                requested_frame_rate_millihz_.load(std::memory_order_acquire),
                frame_rate_vote_status_.load(std::memory_order_acquire)};
    }

private:
    std::atomic<std::uint64_t> uploads_{0};
    std::atomic<std::uint64_t> submissions_{0};
    std::atomic<std::uint64_t> skipped_{0};
    std::atomic<std::int64_t> last_sequence_{-1};
    std::atomic<std::uint64_t> surface_epoch_{0};
    std::atomic<std::uint64_t> runtime_failure_count_{0};
    std::atomic<int> runtime_failure_code_{0};
    std::atomic<int> gpu_timing_status_{0};
    std::atomic<std::int64_t> last_gpu_duration_ns_{-1};
    std::atomic<int> requested_frame_rate_millihz_{0};
    std::atomic<int> frame_rate_vote_status_{0};
};

}  // namespace flynes::video
