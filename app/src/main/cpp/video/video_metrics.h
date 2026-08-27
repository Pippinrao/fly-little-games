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
    std::int64_t last_presentation_ns;
    std::uint64_t real_slots;
    std::uint64_t interpolated_slots;
    std::uint64_t held_slots;
    std::uint64_t warped_slots;
    std::uint64_t cadence_adjustments;
    std::uint64_t swappy_total_frames;
    std::int64_t swappy_last_progress_ns;
    std::uint64_t motion_queue_depth;
    std::int64_t last_pair_a_sequence;
    std::int64_t last_pair_b_sequence;
    std::uint64_t actual_presentation_count;
    std::int64_t last_actual_presentation_ns;
    std::int64_t last_actual_real_sequence;
    std::int64_t last_actual_real_presentation_ns;
    int pacing_owner;
    std::uint64_t last_transition_id;
    int temporal_state;
    std::uint64_t shadow_pair_count;
    std::uint64_t shadow_unsafe_ppm_sum;
    std::uint64_t shadow_peak_unsafe_ppm;
    int shadow_gpu_timing_status;
    std::int64_t shadow_last_gpu_duration_ns;
    std::uint64_t shadow_gpu_valid_sample_count;
    std::int64_t shadow_peak_gpu_duration_ns;
};

class VideoMetrics {
public:
    void on_upload() { uploads_.fetch_add(1, std::memory_order_relaxed); }
    void on_submit(std::uint64_t sequence, std::int64_t presentation_ns) {
        last_sequence_.store(static_cast<std::int64_t>(sequence), std::memory_order_release);
        last_presentation_ns_.store(presentation_ns, std::memory_order_release);
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
    void on_real_slot(std::uint64_t sequence) {
        last_pair_a_sequence_.store(static_cast<std::int64_t>(sequence),
                                    std::memory_order_release);
        real_slots_.fetch_add(1, std::memory_order_relaxed);
    }
    void on_interpolated_slot(std::uint64_t a_sequence, std::uint64_t b_sequence,
                              bool warped) {
        last_pair_a_sequence_.store(static_cast<std::int64_t>(a_sequence),
                                    std::memory_order_release);
        last_pair_b_sequence_.store(static_cast<std::int64_t>(b_sequence),
                                    std::memory_order_release);
        interpolated_slots_.fetch_add(1, std::memory_order_relaxed);
        (warped ? warped_slots_ : held_slots_).fetch_add(1, std::memory_order_relaxed);
    }
    void on_cadence_adjustment() {
        cadence_adjustments_.fetch_add(1, std::memory_order_relaxed);
    }
    void on_motion_queue_depth(std::uint64_t depth) {
        motion_queue_depth_.store(depth, std::memory_order_release);
    }
    void on_swappy_progress(std::uint64_t total_frames, std::int64_t progress_ns) {
        swappy_total_frames_.store(total_frames, std::memory_order_release);
        swappy_last_progress_ns_.store(progress_ns, std::memory_order_release);
    }
    void on_actual_presentation(std::uint64_t sequence, std::int64_t presentation_ns,
                                bool real_slot) {
        last_actual_presentation_ns_.store(presentation_ns, std::memory_order_release);
        if (real_slot) {
            last_actual_real_sequence_.store(
                    static_cast<std::int64_t>(sequence), std::memory_order_release);
            last_actual_real_presentation_ns_.store(presentation_ns,
                                                     std::memory_order_release);
        }
        actual_presentation_count_.fetch_add(1, std::memory_order_relaxed);
    }
    void on_pacing_owner(int owner) {
        pacing_owner_.store(owner, std::memory_order_release);
    }
    void on_transition(std::uint64_t id, int temporal_state) {
        temporal_state_.store(temporal_state, std::memory_order_release);
        last_transition_id_.store(id, std::memory_order_release);
    }
    void reset_shadow() {
        shadow_pair_count_.store(0, std::memory_order_release);
        shadow_unsafe_ppm_sum_.store(0, std::memory_order_release);
        shadow_peak_unsafe_ppm_.store(0, std::memory_order_release);
        shadow_gpu_timing_status_.store(1, std::memory_order_release);
        shadow_last_gpu_duration_ns_.store(-1, std::memory_order_release);
        shadow_gpu_valid_sample_count_.store(0, std::memory_order_release);
        shadow_peak_gpu_duration_ns_.store(-1, std::memory_order_release);
    }
    void on_shadow_gpu_timing(int status, std::int64_t duration_ns) {
        shadow_gpu_timing_status_.store(status, std::memory_order_release);
        shadow_last_gpu_duration_ns_.store(duration_ns, std::memory_order_release);
        if (status != 2 || duration_ns <= 0) return;
        shadow_gpu_valid_sample_count_.fetch_add(1, std::memory_order_relaxed);
        auto peak = shadow_peak_gpu_duration_ns_.load(std::memory_order_relaxed);
        while (peak < duration_ns && !shadow_peak_gpu_duration_ns_.compare_exchange_weak(
                peak, duration_ns, std::memory_order_release, std::memory_order_relaxed)) { }
    }
    void on_shadow_pair(double unsafe_ratio) {
        const auto ppm = static_cast<std::uint64_t>(
                unsafe_ratio <= 0.0 ? 0.0
                        : unsafe_ratio >= 1.0 ? 1'000'000.0
                        : unsafe_ratio * 1'000'000.0 + 0.5);
        shadow_pair_count_.fetch_add(1, std::memory_order_relaxed);
        shadow_unsafe_ppm_sum_.fetch_add(ppm, std::memory_order_relaxed);
        auto peak = shadow_peak_unsafe_ppm_.load(std::memory_order_relaxed);
        while (peak < ppm && !shadow_peak_unsafe_ppm_.compare_exchange_weak(
                peak, ppm, std::memory_order_release, std::memory_order_relaxed)) { }
    }
    VideoMetricsSnapshot snapshot() const {
        // Acquire the commit generation before reading the transaction fields it guards.
        // on_transition stores this id last with release semantics.
        const auto committed_transition_id = last_transition_id_.load(
                std::memory_order_acquire);
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
                frame_rate_vote_status_.load(std::memory_order_acquire),
                last_presentation_ns_.load(std::memory_order_acquire),
                real_slots_.load(std::memory_order_relaxed),
                interpolated_slots_.load(std::memory_order_relaxed),
                held_slots_.load(std::memory_order_relaxed),
                warped_slots_.load(std::memory_order_relaxed),
                cadence_adjustments_.load(std::memory_order_relaxed),
                swappy_total_frames_.load(std::memory_order_acquire),
                swappy_last_progress_ns_.load(std::memory_order_acquire),
                motion_queue_depth_.load(std::memory_order_acquire),
                last_pair_a_sequence_.load(std::memory_order_acquire),
                last_pair_b_sequence_.load(std::memory_order_acquire),
                actual_presentation_count_.load(std::memory_order_relaxed),
                last_actual_presentation_ns_.load(std::memory_order_acquire),
                last_actual_real_sequence_.load(std::memory_order_acquire),
                last_actual_real_presentation_ns_.load(std::memory_order_acquire),
                pacing_owner_.load(std::memory_order_acquire),
                committed_transition_id,
                temporal_state_.load(std::memory_order_acquire),
                shadow_pair_count_.load(std::memory_order_acquire),
                shadow_unsafe_ppm_sum_.load(std::memory_order_acquire),
                shadow_peak_unsafe_ppm_.load(std::memory_order_acquire),
                shadow_gpu_timing_status_.load(std::memory_order_acquire),
                shadow_last_gpu_duration_ns_.load(std::memory_order_acquire),
                shadow_gpu_valid_sample_count_.load(std::memory_order_acquire),
                shadow_peak_gpu_duration_ns_.load(std::memory_order_acquire)};
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
    std::atomic<std::int64_t> last_presentation_ns_{-1};
    std::atomic<std::uint64_t> real_slots_{0};
    std::atomic<std::uint64_t> interpolated_slots_{0};
    std::atomic<std::uint64_t> held_slots_{0};
    std::atomic<std::uint64_t> warped_slots_{0};
    std::atomic<std::uint64_t> cadence_adjustments_{0};
    std::atomic<std::uint64_t> swappy_total_frames_{0};
    std::atomic<std::int64_t> swappy_last_progress_ns_{-1};
    std::atomic<std::uint64_t> motion_queue_depth_{0};
    std::atomic<std::int64_t> last_pair_a_sequence_{-1};
    std::atomic<std::int64_t> last_pair_b_sequence_{-1};
    std::atomic<std::uint64_t> actual_presentation_count_{0};
    std::atomic<std::int64_t> last_actual_presentation_ns_{-1};
    std::atomic<std::int64_t> last_actual_real_sequence_{-1};
    std::atomic<std::int64_t> last_actual_real_presentation_ns_{-1};
    std::atomic<int> pacing_owner_{0};
    std::atomic<std::uint64_t> last_transition_id_{0};
    std::atomic<int> temporal_state_{0};
    std::atomic<std::uint64_t> shadow_pair_count_{0};
    std::atomic<std::uint64_t> shadow_unsafe_ppm_sum_{0};
    std::atomic<std::uint64_t> shadow_peak_unsafe_ppm_{0};
    std::atomic<int> shadow_gpu_timing_status_{0};
    std::atomic<std::int64_t> shadow_last_gpu_duration_ns_{-1};
    std::atomic<std::uint64_t> shadow_gpu_valid_sample_count_{0};
    std::atomic<std::int64_t> shadow_peak_gpu_duration_ns_{-1};
};

}  // namespace flynes::video
