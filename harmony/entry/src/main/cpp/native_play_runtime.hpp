#pragma once

#include "native_play_support.hpp"
#include "play_session.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

struct OH_AudioRendererStruct;
typedef struct OH_AudioRendererStruct OH_AudioRenderer;

namespace flynes::harmony {

struct AudioHandleRecord;

struct NativePlayStatus final
{
    bool running = false;
    bool paused = false;
    bool audio_ready = false;
    bool audio_started = false;
    bool audio_timestamp_valid = false;
    std::uint64_t source_frames = 0;
    std::uint64_t audio_underflows = 0;
    std::uint64_t audio_post_fallback_underflows = 0;
    std::uint64_t audio_lock_misses = 0;
    std::uint64_t audio_short_reads = 0;
    std::uint64_t audio_priming_callbacks = 0;
    std::uint64_t audio_dropped_samples = 0;
    std::uint64_t audio_produced_samples = 0;
    std::uint64_t audio_consumed_samples = 0;
    std::uint64_t audio_queued_samples = 0;
    std::uint64_t audio_high_water_samples = 0;
    std::int32_t audio_callback_frames = 0;
    std::int32_t audio_sample_rate = 0;
    std::int32_t audio_channel_count = 0;
    std::int32_t audio_last_callback_bytes = 0;
    std::uint64_t audio_callback_count = 0;
    bool audio_fast_path = true;
    std::string audio_fallback_reason;
    std::uint64_t audio_delay_samples = 0;
    std::string audio_temporal_state;
    std::int64_t audio_frame_position = 0;
    std::int64_t audio_timestamp_ns = 0;
    double source_fps = 0.0;
    std::string source_standard;
    std::string error;
};

class NativePlayRuntime final
{
public:
    static std::unique_ptr<NativePlayRuntime> open(
        const std::uint8_t* rom, std::size_t size);
    static std::unique_ptr<NativePlayRuntime> open_session(
        std::unique_ptr<PlaySession> session, SourceTiming timing);
    ~NativePlayRuntime();
    NativePlayRuntime(const NativePlayRuntime&) = delete;
    NativePlayRuntime& operator=(const NativePlayRuntime&) = delete;

    void set_buttons(std::uint32_t buttons) noexcept;
    void set_paused(bool paused);
    void set_muted(bool muted) noexcept;
    PlayStepResult copy_latest_frame() const;
    std::vector<std::uint8_t> save_checkpoint();
    void load_checkpoint(const std::uint8_t* bytes, std::size_t size);
    NativePlayStatus status() const;
    void close();

private:
    static constexpr std::size_t kPcmCapacitySamples =
        audio_queue_capacity_samples(48'000);
    NativePlayRuntime(std::unique_ptr<PlaySession> session, SourceTiming timing);
    void run();
    void run_audio();
    void initialize_audio(bool fast_path, std::uint64_t generation);
    bool release_audio(bool closing = false);
    void sample_audio_timestamp();
    void discard_audio_locked();
    static int audio_write(OH_AudioRenderer*, void* user_data,
                           void* buffer, std::int32_t bytes);

    std::unique_ptr<PlaySession> session_;
    SourceTiming timing_;
    mutable std::mutex session_mutex_;
    mutable std::mutex latest_mutex_;
    mutable std::mutex audio_mutex_;
    mutable std::mutex error_mutex_;
    std::condition_variable wake_;
    std::condition_variable first_frame_ready_;
    std::condition_variable audio_wake_;
    std::thread thread_;
    std::thread audio_thread_;
    PlayStepResult latest_;
    SpscPcmRingBuffer pcm_{kPcmCapacitySamples};
    TemporalAudioDelayGate temporal_audio_{800, 80};
    OH_AudioRenderer* audio_renderer_ = nullptr;
    AudioHandleRecord* audio_handle_ = nullptr; // control thread ownership
    bool audio_release_failed_ = false;
    bool audio_renderer_fast_path_ = true;
    // audio_mutex_ protects publication, PCM/gate and generation only. Never
    // hold it across an OHAudio call or a thread join. Only audio_thread_ may
    // call platform control APIs; callback and source never do so.
    std::uint64_t audio_generation_ = 0;
    std::uint64_t audio_discarded_samples_ = 0;
    // Static-lifetime literals only. Status copies under audio_mutex_; audio
    // control, especially failed close/quarantine, never allocates a message.
    const char* audio_fallback_reason_ = "";
    std::atomic<std::uint32_t> buttons_{0};
    std::atomic<bool> stop_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> muted_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> audio_ready_{false};
    std::atomic<bool> audio_started_{false};
    std::atomic<std::uint64_t> source_frames_{0};
    std::atomic<std::uint64_t> audio_underflows_{0};
    std::atomic<std::uint64_t> audio_fallback_underflow_baseline_{0};
    std::atomic<std::uint64_t> audio_lock_misses_{0};
    std::atomic<std::uint64_t> audio_short_reads_{0};
    std::atomic<std::uint64_t> audio_priming_callbacks_{0};
    std::atomic<std::uint64_t> audio_dropped_samples_{0};
    std::atomic<std::uint64_t> audio_produced_samples_{0};
    std::atomic<std::uint64_t> audio_consumed_samples_{0};
    std::atomic<std::uint64_t> audio_queued_samples_{0};
    std::atomic<std::uint64_t> audio_high_water_samples_{0};
    std::atomic<std::int32_t> audio_callback_frames_{0};
    std::atomic<std::int32_t> audio_sample_rate_{0};
    std::atomic<std::int32_t> audio_channel_count_{0};
    std::atomic<std::int32_t> audio_last_callback_bytes_{0};
    std::atomic<std::uint64_t> audio_callback_count_{0};
    std::atomic<bool> audio_fast_path_{true};
    std::atomic<std::int64_t> audio_frame_position_{0};
    std::atomic<std::int64_t> audio_timestamp_ns_{0};
    std::atomic<bool> audio_timestamp_valid_{false};
    std::string error_;
};

} // namespace flynes::harmony
