#include "native_play_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace flynes::harmony {

PcmRingBuffer::PcmRingBuffer(std::size_t capacity_samples)
    : samples_(std::max<std::size_t>(1, capacity_samples), 0)
{
}

void PcmRingBuffer::push(const std::int16_t* samples, std::size_t count)
{
    if (samples == nullptr || count == 0) return;
    produced_samples_ += count;
    if (count >= samples_.size())
    {
        dropped_samples_ += size_ + count - samples_.size();
        samples += count - samples_.size();
        count = samples_.size();
        read_ = 0;
        size_ = 0;
    }
    const std::size_t overflow = size_ + count > samples_.size()
        ? size_ + count - samples_.size() : 0;
    if (overflow > 0)
    {
        read_ = (read_ + overflow) % samples_.size();
        size_ -= overflow;
        dropped_samples_ += overflow;
    }
    const std::size_t write = (read_ + size_) % samples_.size();
    const std::size_t first = std::min(count, samples_.size() - write);
    std::copy_n(samples, first, samples_.begin() + static_cast<std::ptrdiff_t>(write));
    std::copy_n(samples + first, count - first, samples_.begin());
    size_ += count;
    high_water_samples_ = std::max(high_water_samples_, size_);
}

std::size_t PcmRingBuffer::pop(std::int16_t* destination, std::size_t capacity)
{
    if (destination == nullptr || capacity == 0) return 0;
    const std::size_t count = std::min(capacity, size_);
    const std::size_t first = std::min(count, samples_.size() - read_);
    std::copy_n(samples_.begin() + static_cast<std::ptrdiff_t>(read_), first, destination);
    std::copy_n(samples_.begin(), count - first, destination + first);
    read_ = (read_ + count) % samples_.size();
    size_ -= count;
    consumed_samples_ += count;
    return count;
}

void PcmRingBuffer::clear() noexcept
{
    read_ = 0;
    size_ = 0;
}

SpscPcmRingBuffer::SpscPcmRingBuffer(std::size_t capacity_samples)
    : samples_(std::max<std::size_t>(1, capacity_samples), 0)
{
}

void SpscPcmRingBuffer::push(const std::int16_t* samples, std::size_t count) noexcept
{
    if (samples == nullptr || count == 0) return;
    produced_samples_.fetch_add(count, std::memory_order_relaxed);
    const std::uint64_t write = write_sequence_.load(std::memory_order_relaxed);
    const std::uint64_t read = read_sequence_.load(std::memory_order_acquire);
    const std::size_t queued = static_cast<std::size_t>(write - read);
    const std::size_t available = queued < samples_.size()
        ? samples_.size() - queued : 0;
    const std::size_t accepted = std::min(count, available);
    if (accepted < count)
    {
        dropped_samples_.fetch_add(count - accepted, std::memory_order_relaxed);
    }
    if (accepted == 0) return;
    const std::size_t offset = static_cast<std::size_t>(write % samples_.size());
    const std::size_t first = std::min(accepted, samples_.size() - offset);
    std::copy_n(samples, first, samples_.begin() + static_cast<std::ptrdiff_t>(offset));
    std::copy_n(samples + first, accepted - first, samples_.begin());
    write_sequence_.store(write + accepted, std::memory_order_release);

    const std::size_t water = queued + accepted;
    std::size_t previous = high_water_samples_.load(std::memory_order_relaxed);
    while (water > previous && !high_water_samples_.compare_exchange_weak(
        previous, water, std::memory_order_relaxed, std::memory_order_relaxed))
    {
    }
}

std::size_t SpscPcmRingBuffer::pop(
    std::int16_t* destination, std::size_t capacity) noexcept
{
    if (destination == nullptr || capacity == 0) return 0;
    const std::uint64_t read = read_sequence_.load(std::memory_order_relaxed);
    const std::uint64_t write = write_sequence_.load(std::memory_order_acquire);
    const std::size_t count = std::min(
        capacity, static_cast<std::size_t>(write - read));
    if (count == 0) return 0;
    const std::size_t offset = static_cast<std::size_t>(read % samples_.size());
    const std::size_t first = std::min(count, samples_.size() - offset);
    std::copy_n(samples_.begin() + static_cast<std::ptrdiff_t>(offset), first, destination);
    std::copy_n(samples_.begin(), count - first, destination + first);
    read_sequence_.store(read + count, std::memory_order_release);
    consumed_samples_.fetch_add(count, std::memory_order_relaxed);
    return count;
}

void SpscPcmRingBuffer::clear() noexcept
{
    read_sequence_.store(
        write_sequence_.load(std::memory_order_acquire), std::memory_order_release);
}

std::size_t SpscPcmRingBuffer::size() const noexcept
{
    const std::uint64_t write = write_sequence_.load(std::memory_order_acquire);
    const std::uint64_t read = read_sequence_.load(std::memory_order_acquire);
    return std::min(samples_.size(), static_cast<std::size_t>(write - read));
}

bool SpscPcmRingBuffer::lock_free() const noexcept
{
    return read_sequence_.is_lock_free() && write_sequence_.is_lock_free() &&
        dropped_samples_.is_lock_free() && produced_samples_.is_lock_free() &&
        consumed_samples_.is_lock_free() && high_water_samples_.is_lock_free();
}

SourceTiming detect_source_timing(const std::uint8_t* rom, std::size_t size)
{
    SourceTiming result;
    if (rom == nullptr || size < 16 || std::memcmp(rom, "NES\x1A", 4) != 0)
    {
        return result;
    }
    const bool nes2 = (rom[7] & 0x0CU) == 0x08U;
    const bool pal = nes2 ? ((rom[12] & 0x03U) == 1U) : ((rom[9] & 0x01U) != 0U);
    if (pal)
    {
        result.frames_per_second = 50.0069789081886;
        result.frame_period_ns = 19'997'209;
        result.label = "PAL";
    }
    return result;
}

std::vector<std::int16_t> adapt_pcm_to_queue_clock(
    const std::int16_t* samples,
    std::size_t count,
    std::size_t queued_samples,
    std::size_t queue_capacity_samples,
    std::size_t target_samples)
{
    if (samples == nullptr || count == 0) return {};
    const double target = std::max(1.0, std::min(
        static_cast<double>(target_samples),
        static_cast<double>(queue_capacity_samples) * 0.25));
    const double normalized_error =
        (static_cast<double>(queued_samples) - target) / target;
    const double factor = std::clamp(1.0 - normalized_error * 0.03, 0.95, 1.03);
    const std::size_t output_count = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::llround(static_cast<double>(count) * factor)));
    std::vector<std::int16_t> result(output_count);
    if (output_count == 1 || count == 1)
    {
        std::fill(result.begin(), result.end(), samples[0]);
        return result;
    }
    const double step = static_cast<double>(count - 1) /
        static_cast<double>(output_count - 1);
    for (std::size_t index = 0; index < output_count; ++index)
    {
        const double source = static_cast<double>(index) * step;
        const std::size_t left = std::min(
            static_cast<std::size_t>(source), count - 1);
        const std::size_t right = std::min(left + 1, count - 1);
        const double fraction = source - static_cast<double>(left);
        const double value = static_cast<double>(samples[left]) * (1.0 - fraction) +
            static_cast<double>(samples[right]) * fraction;
        result[index] = static_cast<std::int16_t>(std::clamp(
            std::llround(value), -32768LL, 32767LL));
    }
    return result;
}

TemporalAudioDelayGate::TemporalAudioDelayGate(
    std::size_t motion_delay_samples,
    std::size_t drain_samples_per_callback)
    : motion_delay_samples_(motion_delay_samples),
      drain_samples_per_callback_(std::max<std::size_t>(1, drain_samples_per_callback))
{
}

void TemporalAudioDelayGate::set_motion_enabled(bool enabled) noexcept
{
    motion_enabled_.store(enabled, std::memory_order_release);
}

std::size_t TemporalAudioDelayGate::readable_samples(
    std::size_t queued_samples,
    std::size_t requested_samples) noexcept
{
    std::size_t delay = current_delay_samples_.load(std::memory_order_acquire);
    if (motion_enabled_.load(std::memory_order_acquire))
    {
        if (delay < motion_delay_samples_)
        {
            delay = motion_delay_samples_;
            current_delay_samples_.store(delay, std::memory_order_release);
        }
        if (queued_samples <= delay)
        {
            state_.store(State::PRIMING, std::memory_order_release);
            return 0;
        }
        state_.store(State::MOTION, std::memory_order_release);
    }
    else if (delay > 0)
    {
        delay = delay > drain_samples_per_callback_
            ? delay - drain_samples_per_callback_ : 0;
        current_delay_samples_.store(delay, std::memory_order_release);
        state_.store(delay == 0 ? State::NATIVE : State::DRAIN,
                     std::memory_order_release);
    }
    else
    {
        state_.store(State::NATIVE, std::memory_order_release);
    }
    return queued_samples > delay
        ? std::min(requested_samples, queued_samples - delay) : 0;
}

void TemporalAudioDelayGate::reset() noexcept
{
    motion_enabled_.store(false, std::memory_order_release);
    current_delay_samples_.store(0, std::memory_order_release);
    state_.store(State::NATIVE, std::memory_order_release);
}

std::size_t TemporalAudioDelayGate::current_delay_samples() const noexcept
{
    return current_delay_samples_.load(std::memory_order_acquire);
}

bool TemporalAudioDelayGate::intentionally_silent() const noexcept
{
    return state_.load(std::memory_order_acquire) == State::PRIMING;
}

std::string TemporalAudioDelayGate::state() const
{
    switch (state_.load(std::memory_order_acquire))
    {
    case State::PRIMING: return "PRIMING";
    case State::MOTION: return "MOTION";
    case State::DRAIN: return "DRAIN";
    default: return "NATIVE";
    }
}

} // namespace flynes::harmony
