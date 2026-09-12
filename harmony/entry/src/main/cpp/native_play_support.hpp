#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <algorithm>
#include <string>
#include <vector>

namespace flynes::harmony {

class PcmRingBuffer final
{
public:
    explicit PcmRingBuffer(std::size_t capacity_samples);
    void push(const std::int16_t* samples, std::size_t count);
    std::size_t pop(std::int16_t* destination, std::size_t capacity);
    void clear() noexcept;
    std::size_t size() const noexcept { return size_; }
    std::uint64_t dropped_samples() const noexcept { return dropped_samples_; }
    std::uint64_t produced_samples() const noexcept { return produced_samples_; }
    std::uint64_t consumed_samples() const noexcept { return consumed_samples_; }
    std::size_t high_water_samples() const noexcept { return high_water_samples_; }

private:
    std::vector<std::int16_t> samples_;
    std::size_t read_ = 0;
    std::size_t size_ = 0;
    std::uint64_t dropped_samples_ = 0;
    std::uint64_t produced_samples_ = 0;
    std::uint64_t consumed_samples_ = 0;
    std::size_t high_water_samples_ = 0;
};

// Wait-free SPSC queue for the simulation producer and OHAudio callback.
// When full it rejects the newest samples and reports the loss; callers size
// the queue to absorb device scheduling jitter before this path is reached.
class SpscPcmRingBuffer final
{
public:
    explicit SpscPcmRingBuffer(std::size_t capacity_samples);
    void push(const std::int16_t* samples, std::size_t count) noexcept;
    std::size_t pop(std::int16_t* destination, std::size_t capacity) noexcept;
    // Call only after the audio renderer is paused/stopped and the producer has
    // observed its pause/mute flag.
    void clear() noexcept;
    std::size_t size() const noexcept;
    std::uint64_t dropped_samples() const noexcept
    {
        return dropped_samples_.load(std::memory_order_relaxed);
    }
    std::uint64_t produced_samples() const noexcept
    {
        return produced_samples_.load(std::memory_order_relaxed);
    }
    std::uint64_t consumed_samples() const noexcept
    {
        return consumed_samples_.load(std::memory_order_relaxed);
    }
    std::size_t high_water_samples() const noexcept
    {
        return high_water_samples_.load(std::memory_order_relaxed);
    }
    bool lock_free() const noexcept;

private:
    std::vector<std::int16_t> samples_;
    std::atomic<std::uint64_t> read_sequence_{0};
    std::atomic<std::uint64_t> write_sequence_{0};
    std::atomic<std::uint64_t> dropped_samples_{0};
    std::atomic<std::uint64_t> produced_samples_{0};
    std::atomic<std::uint64_t> consumed_samples_{0};
    std::atomic<std::size_t> high_water_samples_{0};
};

struct SourceTiming final
{
    double frames_per_second = 60.0988138974405;
    std::int64_t frame_period_ns = 16'639'268;
    std::string label = "NTSC";
};

SourceTiming detect_source_timing(const std::uint8_t* rom, std::size_t size);

// Keeps one second of bounded storage so a delayed device callback does not
// overwrite PCM. Queue-clock adaptation still targets 50 ms of steady-state
// latency; this capacity is only scheduling-jitter headroom.
constexpr std::size_t audio_queue_capacity_samples(std::size_t sample_rate) noexcept
{
    return std::max<std::size_t>(1, sample_rate);
}

// OHAudio may request several callbacks immediately to seed its internal
// device buffer. Prime twelve callbacks before starting so those requests are
// supplied as audio instead of being misreported as runtime starvation.
constexpr std::size_t audio_start_prime_samples(std::size_t callback_frames) noexcept
{
    return std::max<std::size_t>(2'880, callback_frames * 12);
}

constexpr std::size_t audio_queue_target_samples(
    std::size_t callback_frames, bool fast_path) noexcept
{
    return fast_path ? 2'400 :
        std::max<std::size_t>(2'400, callback_frames * 6);
}

constexpr bool should_fallback_audio_latency(
    std::uint64_t underflows, bool fast_path) noexcept
{
    return fast_path && underflows >= 3;
}

// Keeps a bounded callback queue near 50 ms without changing emulation cadence.
// Normal correction remains small; a nearly full queue may contract by up to
// 5% to recover from a device callback stall before bounded storage overflows.
std::vector<std::int16_t> adapt_pcm_to_queue_clock(
    const std::int16_t* samples,
    std::size_t count,
    std::size_t queued_samples,
    std::size_t queue_capacity_samples,
    std::size_t target_samples = 2'400);

class TemporalAudioDelayGate final
{
public:
    TemporalAudioDelayGate(std::size_t motion_delay_samples,
                           std::size_t drain_samples_per_callback);
    void set_motion_enabled(bool enabled) noexcept;
    std::size_t readable_samples(std::size_t queued_samples,
                                 std::size_t requested_samples) noexcept;
    void reset() noexcept;
    std::size_t current_delay_samples() const noexcept;
    bool intentionally_silent() const noexcept;
    std::string state() const;

private:
    enum class State : int { NATIVE = 0, PRIMING = 1, MOTION = 2, DRAIN = 3 };
    const std::size_t motion_delay_samples_;
    const std::size_t drain_samples_per_callback_;
    std::atomic<bool> motion_enabled_{false};
    std::atomic<std::size_t> current_delay_samples_{0};
    std::atomic<State> state_{State::NATIVE};
};

} // namespace flynes::harmony
