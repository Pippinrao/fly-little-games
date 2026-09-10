#include "native_play_support.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>
#include <vector>

using flynes::harmony::PcmRingBuffer;
using flynes::harmony::SpscPcmRingBuffer;
using flynes::harmony::TemporalAudioDelayGate;
using flynes::harmony::adapt_pcm_to_queue_clock;
using flynes::harmony::audio_queue_capacity_samples;
using flynes::harmony::audio_queue_target_samples;
using flynes::harmony::audio_start_prime_samples;
using flynes::harmony::detect_source_timing;
using flynes::harmony::should_fallback_audio_latency;

int main()
{
    PcmRingBuffer queue(6);
    const std::array<std::int16_t, 4> first{1, 2, 3, 4};
    const std::array<std::int16_t, 4> second{5, 6, 7, 8};
    queue.push(first.data(), first.size());
    queue.push(second.data(), second.size());
    assert(queue.size() == 6);
    assert(queue.produced_samples() == 8);
    assert(queue.consumed_samples() == 0);
    assert(queue.high_water_samples() == 6);
    assert(queue.dropped_samples() == 2);
    std::array<std::int16_t, 8> output{};
    const auto count = queue.pop(output.data(), output.size());
    assert(count == 6);
    assert(queue.consumed_samples() == 6);
    assert((output == std::array<std::int16_t, 8>{3, 4, 5, 6, 7, 8, 0, 0}));
    queue.clear();
    assert(queue.size() == 0);

    const std::array<std::int16_t, 100> tone{};
    const auto steady = adapt_pcm_to_queue_clock(tone.data(), tone.size(), 2400, 9600);
    const auto slow_sink = adapt_pcm_to_queue_clock(tone.data(), tone.size(), 9000, 9600);
    const auto starved_sink = adapt_pcm_to_queue_clock(tone.data(), tone.size(), 0, 9600);
    assert(steady.size() == tone.size());
    assert(slow_sink.size() < tone.size());
    assert(slow_sink.size() >= 95);
    assert(starved_sink.size() > tone.size());
    assert(starved_sink.size() <= 103);
    const auto emulator_backpressure = adapt_pcm_to_queue_clock(
        tone.data(), tone.size(), 36'000, 48'000);
    assert(emulator_backpressure.size() <= 95);
    assert(audio_start_prime_samples(960) == 11'520);
    assert(audio_queue_target_samples(960, true) == 2'400);
    assert(audio_queue_target_samples(960, false) == 5'760);
    const auto normal_steady = adapt_pcm_to_queue_clock(
        tone.data(), tone.size(), 5'760, 48'000, 5'760);
    assert(normal_steady.size() == tone.size());
    assert(!should_fallback_audio_latency(2, true));
    assert(should_fallback_audio_latency(3, true));
    assert(!should_fallback_audio_latency(30, false));

    // The official Harmony phone emulator was observed pausing its audio sink
    // for 375 ms. Keep the normal queue target at 50 ms, but reserve enough
    // bounded storage for a one-second device scheduling excursion so the
    // producer does not overwrite PCM while the sink catches up.
    constexpr std::size_t sample_rate = 48'000;
    const std::size_t resilient_capacity = audio_queue_capacity_samples(sample_rate);
    assert(resilient_capacity >= sample_rate);
    PcmRingBuffer jitter_queue(resilient_capacity);
    const std::vector<std::int16_t> prime(2'400, 1);
    const std::vector<std::int16_t> observed_stall(18'000, 2);
    jitter_queue.push(prime.data(), prime.size());
    jitter_queue.push(observed_stall.data(), observed_stall.size());
    assert(jitter_queue.dropped_samples() == 0);
    std::array<std::int16_t, 960> callback{};
    const std::array<std::int16_t, 960> frame_audio{};
    for (int callback_index = 0; callback_index < 800; ++callback_index)
    {
        assert(jitter_queue.pop(callback.data(), callback.size()) == callback.size());
        const auto corrected = adapt_pcm_to_queue_clock(
            frame_audio.data(), frame_audio.size(), jitter_queue.size(),
            resilient_capacity);
        jitter_queue.push(corrected.data(), corrected.size());
    }
    assert(jitter_queue.dropped_samples() == 0);
    assert(jitter_queue.size() <= 3'600);

    // The real-time audio callback must not take the simulation mutex. Verify
    // the single-producer/single-consumer queue preserves order under overlap.
    constexpr std::size_t concurrent_count = 100'000;
    SpscPcmRingBuffer spsc(concurrent_count + 1);
    assert(spsc.lock_free());
    std::atomic<bool> producer_done{false};
    std::vector<std::int16_t> concurrent_output;
    concurrent_output.reserve(concurrent_count);
    std::thread producer([&] {
        for (std::size_t begin = 0; begin < concurrent_count; begin += 100)
        {
            std::array<std::int16_t, 100> chunk{};
            for (std::size_t index = 0; index < chunk.size(); ++index)
            {
                chunk[index] = static_cast<std::int16_t>((begin + index) % 30'000);
            }
            spsc.push(chunk.data(), chunk.size());
        }
        producer_done.store(true, std::memory_order_release);
    });
    std::thread consumer([&] {
        std::array<std::int16_t, 257> chunk{};
        while (!producer_done.load(std::memory_order_acquire) || spsc.size() > 0)
        {
            const auto read = spsc.pop(chunk.data(), chunk.size());
            concurrent_output.insert(
                concurrent_output.end(), chunk.begin(), chunk.begin() + read);
            if (read == 0) std::this_thread::yield();
        }
    });
    producer.join();
    consumer.join();
    assert(spsc.dropped_samples() == 0);
    assert(concurrent_output.size() == concurrent_count);
    for (std::size_t index = 0; index < concurrent_output.size(); ++index)
    {
        assert(concurrent_output[index] == static_cast<std::int16_t>(index % 30'000));
    }

    TemporalAudioDelayGate delay(800, 80);
    assert(delay.readable_samples(4000, 960) == 960);
    delay.set_motion_enabled(true);
    assert(delay.readable_samples(800, 960) == 0);
    assert(delay.state() == "PRIMING");
    assert(delay.intentionally_silent());
    assert(delay.readable_samples(2000, 960) == 960);
    assert(delay.state() == "MOTION");
    delay.set_motion_enabled(false);
    assert(delay.readable_samples(2000, 960) == 960);
    assert(delay.current_delay_samples() == 720);
    assert(delay.state() == "DRAIN");
    for (int index = 0; index < 9; ++index) {
        (void)delay.readable_samples(2000, 960);
    }
    assert(delay.current_delay_samples() == 0);
    assert(delay.state() == "NATIVE");
    assert(!delay.intentionally_silent());
    delay.reset();
    assert(delay.current_delay_samples() == 0);

    std::array<std::uint8_t, 16> ntsc{'N', 'E', 'S', 0x1A};
    auto ntsc_timing = detect_source_timing(ntsc.data(), ntsc.size());
    assert(ntsc_timing.label == "NTSC");
    assert(ntsc_timing.frames_per_second > 60.0);

    auto pal = ntsc;
    pal[9] = 1;
    auto pal_timing = detect_source_timing(pal.data(), pal.size());
    assert(pal_timing.label == "PAL");
    assert(pal_timing.frames_per_second > 49.9 && pal_timing.frames_per_second < 50.1);

    auto nes2 = ntsc;
    nes2[7] = 0x08;
    nes2[12] = 1;
    assert(detect_source_timing(nes2.data(), nes2.size()).label == "PAL");
    return 0;
}
