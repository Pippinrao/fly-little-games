#include "native_play_runtime.hpp"

#if defined(FLYNES_HOST_AUDIO_TEST)
#include "native_audio_test_backend.hpp"
#else
#include "harmony_renderer.hpp"
#include <ohaudio/native_audiostreambuilder.h>
#include <ohaudio/native_audiorenderer.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <new>
#include <stdexcept>

namespace flynes::harmony {

// Allocated before platform creation. If Release permanently fails, retaining
// this record at process scope needs no allocation on the close/destructor path.
struct AudioHandleRecord {
    enum class State { PREPARED, RUNNING, PAUSED, STOPPED };
    OH_AudioRenderer* renderer = nullptr;
    AudioHandleRecord* next = nullptr;
    // Physical state belongs to the control thread, independently of whether
    // a result is still current enough to publish audio_started_ to consumers.
    State state = State::PREPARED;
};
namespace {

std::atomic<bool> audio_creation_disabled{false};
std::atomic<AudioHandleRecord*> quarantined_audio_handles{nullptr};

void quarantine_audio_handle(AudioHandleRecord* record) noexcept
{
    auto* head = quarantined_audio_handles.load(std::memory_order_relaxed);
    do { record->next = head; }
    while (!quarantined_audio_handles.compare_exchange_weak(
        head, record, std::memory_order_release, std::memory_order_relaxed));
}

bool audio_ok(OH_AudioStream_Result result)
{
    return result == AUDIOSTREAM_SUCCESS;
}

using GetAudioTimestamp = OH_AudioStream_Result (*)(OH_AudioRenderer*,
                                                     std::int64_t*, std::int64_t*);

GetAudioTimestamp get_audio_timestamp_api()
{
#if defined(FLYNES_HOST_AUDIO_TEST)
    return &OH_AudioRenderer_GetAudioTimestampInfo;
#else
    static void* library = dlopen("libohaudio.so", RTLD_NOW | RTLD_LOCAL);
    static auto api = library == nullptr ? nullptr : reinterpret_cast<GetAudioTimestamp>(
        dlsym(library, "OH_AudioRenderer_GetAudioTimestampInfo"));
    return api;
#endif
}

} // namespace

std::unique_ptr<NativePlayRuntime> NativePlayRuntime::open(
    const std::uint8_t* rom, std::size_t size)
{
    SourceTiming timing = detect_source_timing(rom, size);
    return open_session(PlaySession::open(rom, size), std::move(timing));
}

std::unique_ptr<NativePlayRuntime> NativePlayRuntime::open_session(
    std::unique_ptr<PlaySession> session, SourceTiming timing)
{
    auto result = std::unique_ptr<NativePlayRuntime>(
        new NativePlayRuntime(std::move(session), std::move(timing)));
    result->audio_thread_ = std::thread([runtime = result.get()] { runtime->run_audio(); });
    result->running_.store(true, std::memory_order_release);
    result->thread_ = std::thread([runtime = result.get()] { runtime->run(); });
    {
        std::unique_lock lock(result->latest_mutex_);
        if (!result->first_frame_ready_.wait_for(lock, std::chrono::seconds(1), [&] {
                return !result->latest_.rgb565.empty() ||
                       !result->running_.load(std::memory_order_acquire);
            }) || result->latest_.rgb565.empty())
        {
            throw std::runtime_error("native simulation did not produce its first frame");
        }
    }
    return result;
}

NativePlayRuntime::NativePlayRuntime(std::unique_ptr<PlaySession> session,
                                     SourceTiming timing)
    : session_(std::move(session)), timing_(std::move(timing))
{
}

NativePlayRuntime::~NativePlayRuntime()
{
    close();
}

void NativePlayRuntime::set_buttons(std::uint32_t buttons) noexcept
{
    buttons_.store(buttons, std::memory_order_release);
}

void NativePlayRuntime::set_paused(bool paused)
{
    {
        std::lock_guard lock(audio_mutex_);
        if (paused_.exchange(paused, std::memory_order_acq_rel) != paused)
        {
            ++audio_generation_;
            audio_started_.store(false, std::memory_order_release);
            audio_timestamp_valid_.store(false, std::memory_order_release);
            if (paused) discard_audio_locked();
        }
    }
    audio_wake_.notify_all();
    wake_.notify_all();
}

void NativePlayRuntime::set_muted(bool muted) noexcept
{
    {
        std::lock_guard lock(audio_mutex_);
        if (muted_.exchange(muted, std::memory_order_acq_rel) != muted)
        {
            ++audio_generation_;
            audio_started_.store(false, std::memory_order_release);
            audio_timestamp_valid_.store(false, std::memory_order_release);
            if (muted) discard_audio_locked();
        }
    }
    audio_wake_.notify_all();
}

PlayStepResult NativePlayRuntime::copy_latest_frame() const
{
    std::lock_guard lock(latest_mutex_);
    if (latest_.rgb565.empty())
    {
        throw std::runtime_error("native simulation has not produced a frame");
    }
    return latest_;
}

std::vector<std::uint8_t> NativePlayRuntime::save_checkpoint()
{
    std::lock_guard lock(session_mutex_);
    return session_->save_checkpoint();
}

void NativePlayRuntime::load_checkpoint(const std::uint8_t* bytes, std::size_t size)
{
    const bool was_paused = paused_.load(std::memory_order_acquire);
    set_paused(true);
    try
    {
        std::lock_guard lock(session_mutex_);
        session_->load_checkpoint(bytes, size);
    }
    catch (...)
    {
        if (!was_paused) set_paused(false);
        throw;
    }
    if (!was_paused) set_paused(false);
}

NativePlayStatus NativePlayRuntime::status() const
{
    NativePlayStatus result;
    result.running = running_.load(std::memory_order_acquire);
    result.paused = paused_.load(std::memory_order_acquire);
    result.audio_ready = audio_ready_.load(std::memory_order_acquire);
    result.audio_started = audio_started_.load(std::memory_order_acquire);
    result.audio_timestamp_valid = audio_timestamp_valid_.load(std::memory_order_acquire);
    result.source_frames = source_frames_.load(std::memory_order_acquire);
    result.audio_underflows = audio_underflows_.load(std::memory_order_acquire);
    if (!audio_fast_path_.load(std::memory_order_acquire))
    {
        const auto baseline = audio_fallback_underflow_baseline_.load(
            std::memory_order_acquire);
        result.audio_post_fallback_underflows = result.audio_underflows >= baseline
            ? result.audio_underflows - baseline : 0;
    }
    result.audio_lock_misses = audio_lock_misses_.load(std::memory_order_acquire);
    result.audio_short_reads = audio_short_reads_.load(std::memory_order_acquire);
    result.audio_priming_callbacks = audio_priming_callbacks_.load(std::memory_order_acquire);
    result.audio_dropped_samples = audio_dropped_samples_.load(std::memory_order_acquire);
    result.audio_produced_samples = audio_produced_samples_.load(std::memory_order_acquire);
    result.audio_consumed_samples = audio_consumed_samples_.load(std::memory_order_acquire);
    result.audio_queued_samples = audio_queued_samples_.load(std::memory_order_acquire);
    result.audio_high_water_samples = audio_high_water_samples_.load(std::memory_order_acquire);
    result.audio_callback_frames = audio_callback_frames_.load(std::memory_order_acquire);
    result.audio_sample_rate = audio_sample_rate_.load(std::memory_order_acquire);
    result.audio_channel_count = audio_channel_count_.load(std::memory_order_acquire);
    result.audio_last_callback_bytes = audio_last_callback_bytes_.load(std::memory_order_acquire);
    result.audio_callback_count = audio_callback_count_.load(std::memory_order_acquire);
    result.audio_fast_path = audio_fast_path_.load(std::memory_order_acquire);
    {
        std::lock_guard lock(audio_mutex_);
        result.audio_fallback_reason = audio_fallback_reason_;
        result.audio_delay_samples = temporal_audio_.current_delay_samples();
        result.audio_temporal_state = temporal_audio_.state();
    }
    result.audio_frame_position = audio_frame_position_.load(std::memory_order_acquire);
    result.audio_timestamp_ns = audio_timestamp_ns_.load(std::memory_order_acquire);
    result.source_fps = timing_.frames_per_second;
    result.source_standard = timing_.label;
    {
        std::lock_guard lock(error_mutex_);
        result.error = error_;
    }
    return result;
}

void NativePlayRuntime::close()
{
    stop_.store(true, std::memory_order_release);
    wake_.notify_all();
    audio_wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    if (audio_thread_.joinable()) audio_thread_.join();
    session_.reset();
    running_.store(false, std::memory_order_release);
}

void NativePlayRuntime::run()
{
    running_.store(true, std::memory_order_release);
    auto deadline = std::chrono::steady_clock::now();
    const auto period = std::chrono::nanoseconds(timing_.frame_period_ns);
    try
    {
        while (!stop_.load(std::memory_order_acquire))
        {
            if (paused_.load(std::memory_order_acquire))
            {
                std::unique_lock lock(error_mutex_);
                wake_.wait(lock, [&] {
                    return stop_.load(std::memory_order_acquire) ||
                           !paused_.load(std::memory_order_acquire);
                });
                deadline = std::chrono::steady_clock::now();
                continue;
            }
            PlayStepResult step;
            {
                std::lock_guard lock(session_mutex_);
                session_->set_port0_buttons(buttons_.load(std::memory_order_acquire));
                step = session_->step();
            }
            harmony_renderer().submit_frame(
                step.frame_index, step.width, step.height, step.rgb565);
            {
                const bool motion = harmony_renderer().status().display.motion_qualified;
                std::lock_guard lock(audio_mutex_);
                temporal_audio_.set_motion_enabled(motion);
            }
            if (!paused_.load(std::memory_order_acquire) &&
                !muted_.load(std::memory_order_acquire) && !step.pcm.empty())
            {
                const auto adjusted = adapt_pcm_to_queue_clock(
                    step.pcm.data(), step.pcm.size(),
                    static_cast<std::size_t>(audio_queued_samples_.load(
                        std::memory_order_acquire)), kPcmCapacitySamples,
                    audio_queue_target_samples(
                        static_cast<std::size_t>(std::max(
                            1, audio_callback_frames_.load(std::memory_order_acquire))),
                        audio_fast_path_.load(std::memory_order_acquire)));
                {
                    std::lock_guard lock(audio_mutex_);
                    if (!paused_.load(std::memory_order_acquire) &&
                        !muted_.load(std::memory_order_acquire))
                        pcm_.push(adjusted.data(), adjusted.size());
                    audio_dropped_samples_.store(pcm_.dropped_samples() + audio_discarded_samples_, std::memory_order_release);
                    audio_produced_samples_.store(pcm_.produced_samples(), std::memory_order_release);
                    audio_queued_samples_.store(pcm_.size(), std::memory_order_release);
                    audio_high_water_samples_.store(
                        pcm_.high_water_samples(), std::memory_order_release);
                }
                audio_wake_.notify_all();
            }
            step.pcm.clear();
            step.pcm_sample_count = 0;
            {
                std::lock_guard lock(latest_mutex_);
                latest_ = step;
            }
            first_frame_ready_.notify_all();
            source_frames_.fetch_add(1, std::memory_order_acq_rel);
            if (should_fallback_audio_latency(
                    audio_underflows_.load(std::memory_order_acquire),
                    audio_fast_path_.load(std::memory_order_acquire)))
            {
                std::lock_guard lock(audio_mutex_);
                audio_fast_path_.store(false, std::memory_order_release);
                ++audio_generation_;
                audio_started_.store(false, std::memory_order_release);
                audio_fallback_reason_ = "fast audio path underrun; switching to normal latency";
                audio_fallback_underflow_baseline_.store(
                    audio_underflows_.load(std::memory_order_acquire),
                    std::memory_order_release);
                audio_wake_.notify_all();
            }

            deadline += period;
            const auto now = std::chrono::steady_clock::now();
            if (deadline < now - period) deadline = now;
            std::unique_lock lock(error_mutex_);
            wake_.wait_until(lock, deadline, [&] {
                return stop_.load(std::memory_order_acquire) ||
                       paused_.load(std::memory_order_acquire);
            });
        }
    }
    catch (const std::exception& error)
    {
        std::lock_guard lock(error_mutex_);
        error_ = error.what();
    }
    catch (...)
    {
        std::lock_guard lock(error_mutex_);
        error_ = "native simulation failed";
    }
    running_.store(false, std::memory_order_release);
    first_frame_ready_.notify_all();
}

void NativePlayRuntime::run_audio()
{
    std::uint64_t attempted_generation = static_cast<std::uint64_t>(-1);
    auto next_timestamp = std::chrono::steady_clock::now();
    while (!stop_.load(std::memory_order_acquire))
    {
        std::uint64_t generation;
        bool enabled;
        bool fast_path;
        {
            std::lock_guard lock(audio_mutex_);
            generation = audio_generation_;
            enabled = !paused_.load() && !muted_.load();
            fast_path = audio_fast_path_.load();
        }
        if (generation != attempted_generation)
        {
            if (audio_renderer_ != nullptr &&
                audio_renderer_fast_path_ == fast_path && audio_ready_.load())
            {
                // Preserve the established pause/mute path and healthy sink.
                // Source/callback already observe the new desired state; these
                // potentially slow platform operations run without their lock.
                // Pause is legal only for a RUNNING renderer. PREPARED has no
                // device data to flush; PAUSED was already paused/flushed and
                // can resume directly. Desired-generation changes alone do not
                // imply a physical state transition.
                if (audio_handle_->state == AudioHandleRecord::State::RUNNING)
                {
                    const bool paused = audio_ok(OH_AudioRenderer_Pause(audio_renderer_));
                    if (paused) audio_handle_->state = AudioHandleRecord::State::PAUSED;
                    const bool flushed = paused && audio_ok(OH_AudioRenderer_Flush(audio_renderer_));
                    if (!flushed) release_audio();
                }
            }
            else if (audio_handle_ != nullptr) release_audio();
            attempted_generation = generation;
            if (enabled && !stop_.load() && audio_handle_ == nullptr)
                initialize_audio(fast_path, generation);
        }
        OH_AudioRenderer* renderer = nullptr;
        {
            std::lock_guard lock(audio_mutex_);
            if (generation == audio_generation_ && !stop_.load() &&
                !paused_.load() && !muted_.load() && audio_renderer_ != nullptr && audio_ready_.load() &&
                !audio_started_.load() && pcm_.size() >= audio_start_prime_samples(
                    static_cast<std::size_t>(std::max(1, audio_callback_frames_.load()))))
                renderer = audio_renderer_;
        }
        if (renderer != nullptr)
        {
            const bool started = audio_ok(OH_AudioRenderer_Start(renderer));
            // Even a now-stale successful Start really started the device. The
            // next generation must Pause it before any later Start is legal.
            if (started) audio_handle_->state = AudioHandleRecord::State::RUNNING;
            std::lock_guard lock(audio_mutex_);
            if (generation == audio_generation_ && !stop_.load() &&
                !paused_.load() && !muted_.load())
            {
                audio_started_.store(started, std::memory_order_release);
                if (started)
                {
                    audio_fallback_reason_ = fast_path ? "" :
                        "fast audio path unavailable; using normal latency";
                }
                else
                {
                    audio_ready_.store(false, std::memory_order_release);
                    audio_fallback_reason_ = "audio renderer start failed";
                    // Do not spin Start on a failing device. A state change
                    // creates a new generation and permits recovery.
                    attempted_generation = audio_generation_;
                }
            }
        }
        if (std::chrono::steady_clock::now() >= next_timestamp)
        {
            sample_audio_timestamp();
            next_timestamp = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        }
        std::unique_lock lock(audio_mutex_);
        audio_wake_.wait_for(lock, std::chrono::milliseconds(5));
    }
    release_audio(true);
    std::lock_guard lock(audio_mutex_);
    discard_audio_locked();
}

void NativePlayRuntime::initialize_audio(bool fast_path, std::uint64_t generation)
{
    if (audio_creation_disabled.load(std::memory_order_acquire))
    {
        std::lock_guard lock(audio_mutex_);
        audio_fallback_reason_ = "audio disabled for this process after renderer release failure";
        return;
    }
    audio_handle_ = new (std::nothrow) AudioHandleRecord;
    if (audio_handle_ == nullptr)
    {
        std::lock_guard lock(audio_mutex_);
        audio_fallback_reason_ = "audio renderer ownership allocation failed";
        return;
    }
    OH_AudioRenderer* renderer = nullptr;
    OH_AudioStreamBuilder* builder = nullptr;
    if (!audio_ok(OH_AudioStreamBuilder_Create(&builder, AUDIOSTREAM_TYPE_RENDERER)) ||
        builder == nullptr)
    {
        delete audio_handle_;
        audio_handle_ = nullptr;
        std::lock_guard lock(audio_mutex_);
        if (generation == audio_generation_)
        {
            audio_fallback_reason_ = "audio renderer creation failed";
            if (fast_path) { audio_fast_path_.store(false); ++audio_generation_; }
        }
        return;
    }
    const bool configured =
        audio_ok(OH_AudioStreamBuilder_SetSamplingRate(builder, 48'000)) &&
        audio_ok(OH_AudioStreamBuilder_SetChannelCount(builder, 1)) &&
        audio_ok(OH_AudioStreamBuilder_SetSampleFormat(builder, AUDIOSTREAM_SAMPLE_S16LE)) &&
        audio_ok(OH_AudioStreamBuilder_SetEncodingType(builder, AUDIOSTREAM_ENCODING_TYPE_RAW)) &&
        audio_ok(OH_AudioStreamBuilder_SetLatencyMode(
            builder, fast_path ? AUDIOSTREAM_LATENCY_MODE_FAST :
                                 AUDIOSTREAM_LATENCY_MODE_NORMAL)) &&
        audio_ok(OH_AudioStreamBuilder_SetRendererInfo(builder, AUDIOSTREAM_USAGE_GAME)) &&
        audio_ok(OH_AudioStreamBuilder_SetFrameSizeInCallback(builder, 960)) &&
        audio_ok(OH_AudioStreamBuilder_SetRendererWriteDataCallback(
            builder, reinterpret_cast<OH_AudioRenderer_OnWriteDataCallback>(&NativePlayRuntime::audio_write),
            this)) &&
        audio_ok(OH_AudioStreamBuilder_GenerateRenderer(builder, &renderer));
    (void)OH_AudioStreamBuilder_Destroy(builder);
    audio_handle_->renderer = renderer;
    if (!configured || renderer == nullptr)
    {
        if (!release_audio()) return;
        std::lock_guard lock(audio_mutex_);
        if (generation == audio_generation_)
        {
            audio_fallback_reason_ = "audio renderer configuration failed";
            if (fast_path) { audio_fast_path_.store(false); ++audio_generation_; }
        }
        return;
    }
    std::int32_t callback_frames = 0;
    (void)OH_AudioRenderer_GetFrameSizeInCallback(renderer, &callback_frames);
    std::int32_t sample_rate = 0;
    (void)OH_AudioRenderer_GetSamplingRate(renderer, &sample_rate);
    std::int32_t channel_count = 0;
    (void)OH_AudioRenderer_GetChannelCount(renderer, &channel_count);
    {
        std::lock_guard lock(audio_mutex_);
        if (generation == audio_generation_ && !stop_.load() &&
            !audio_creation_disabled.load(std::memory_order_acquire) &&
            !paused_.load() && !muted_.load())
        {
            audio_renderer_ = renderer;
            audio_renderer_fast_path_ = fast_path;
            audio_callback_frames_.store(callback_frames, std::memory_order_release);
            audio_sample_rate_.store(sample_rate, std::memory_order_release);
            audio_channel_count_.store(channel_count, std::memory_order_release);
            audio_ready_.store(true, std::memory_order_release);
            return;
        }
    }
    release_audio();
}

bool NativePlayRuntime::release_audio(bool closing)
{
    {
        std::lock_guard lock(audio_mutex_);
        audio_renderer_ = nullptr;
        audio_ready_.store(false, std::memory_order_release);
        audio_started_.store(false, std::memory_order_release);
        audio_timestamp_valid_.store(false, std::memory_order_release);
    }
    // Called only by audio_thread_, never the source or callback. OpenHarmony
    // 6.0 AudioRendererPrivate::Release joins its callback loop on this path.
    // The identity is detached before Stop/Release, so late old callbacks fill
    // silence. Release returns before another sink may be published or runtime
    // storage destroyed. Keep queued PCM across fallback; overflow is counted.
    if (audio_handle_ == nullptr) return true;
    auto* renderer = audio_handle_->renderer;
    if (renderer != nullptr)
    {
        if (audio_release_failed_ && !closing) return false;
        if (audio_ok(OH_AudioRenderer_Stop(renderer)))
            audio_handle_->state = AudioHandleRecord::State::STOPPED;
        const int attempts = closing ? 1 : 2;
        bool released = false;
        for (int attempt = 0; attempt < attempts; ++attempt)
        {
            if (audio_ok(OH_AudioRenderer_Release(renderer))) { released = true; break; }
            if (attempt + 1 < attempts) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (!released)
        {
            audio_release_failed_ = true;
            audio_creation_disabled.store(true, std::memory_order_release);
            std::lock_guard lock(audio_mutex_);
            audio_fallback_reason_ = "audio renderer release failed; audio disabled for this process";
            if (closing)
            {
                // The verified noncallback Release path has joined callbacks,
                // even on failure. Keep the unreleased opaque platform resource
                // until process exit, without losing its ownership record.
                // No further sinks may be created, bounding retained records
                // to sinks already alive/in creation at the first failure.
                quarantine_audio_handle(audio_handle_);
                audio_handle_ = nullptr;
            }
            return false;
        }
    }
    delete audio_handle_;
    audio_handle_ = nullptr;
    audio_release_failed_ = false;
    return true;
}

void NativePlayRuntime::discard_audio_locked()
{
    audio_discarded_samples_ += pcm_.size();
    pcm_.clear();
    temporal_audio_.reset();
    audio_dropped_samples_.store(pcm_.dropped_samples() + audio_discarded_samples_, std::memory_order_release);
    audio_queued_samples_.store(0, std::memory_order_release);
}

void NativePlayRuntime::sample_audio_timestamp()
{
    GetAudioTimestamp api = get_audio_timestamp_api();
    OH_AudioRenderer* renderer;
    std::uint64_t generation;
    {
        std::lock_guard lock(audio_mutex_);
        renderer = audio_renderer_;
        generation = audio_generation_;
        if (api == nullptr || renderer == nullptr || !audio_started_.load()) return;
    }
    std::int64_t position = 0;
    std::int64_t timestamp = 0;
    const bool valid = api(renderer, &position, &timestamp) == AUDIOSTREAM_SUCCESS &&
        position >= 0 && timestamp > 0;
    std::lock_guard lock(audio_mutex_);
    if (generation == audio_generation_ && !stop_.load() && !paused_.load() && !muted_.load())
    {
        if (valid)
        {
            audio_frame_position_.store(position, std::memory_order_release);
            audio_timestamp_ns_.store(timestamp, std::memory_order_release);
        }
        audio_timestamp_valid_.store(valid, std::memory_order_release);
    }
}

int NativePlayRuntime::audio_write(OH_AudioRenderer* renderer, void* user_data,
                                   void* buffer, std::int32_t bytes)
{
    if (user_data == nullptr || buffer == nullptr || bytes <= 0)
    {
        return AUDIO_DATA_CALLBACK_RESULT_INVALID;
    }
    auto* self = static_cast<NativePlayRuntime*>(user_data);
    self->audio_last_callback_bytes_.store(bytes, std::memory_order_relaxed);
    self->audio_callback_count_.fetch_add(1, std::memory_order_relaxed);
    std::memset(buffer, 0, static_cast<std::size_t>(bytes));
    std::unique_lock lock(self->audio_mutex_, std::try_to_lock);
    if (!lock.owns_lock())
    {
        self->audio_lock_misses_.fetch_add(1, std::memory_order_relaxed);
        return AUDIO_DATA_CALLBACK_RESULT_VALID;
    }
    if (renderer != self->audio_renderer_ || self->stop_.load(std::memory_order_acquire))
        return AUDIO_DATA_CALLBACK_RESULT_VALID;
    if (!self->audio_started_.load(std::memory_order_acquire))
    {
        self->audio_priming_callbacks_.fetch_add(1, std::memory_order_relaxed);
        return AUDIO_DATA_CALLBACK_RESULT_VALID;
    }
    if (self->paused_.load(std::memory_order_acquire) ||
        self->muted_.load(std::memory_order_acquire))
    {
        return AUDIO_DATA_CALLBACK_RESULT_VALID;
    }
    const std::size_t requested = static_cast<std::size_t>(bytes) / sizeof(std::int16_t);
    const std::size_t readable = self->temporal_audio_.readable_samples(
        self->pcm_.size(), requested);
    const std::size_t read = self->pcm_.pop(
        static_cast<std::int16_t*>(buffer), readable);
    self->audio_consumed_samples_.store(
        self->pcm_.consumed_samples(), std::memory_order_release);
    self->audio_queued_samples_.store(self->pcm_.size(), std::memory_order_release);
    if (read < requested && !self->temporal_audio_.intentionally_silent())
    {
        self->audio_short_reads_.fetch_add(1, std::memory_order_relaxed);
        self->audio_underflows_.fetch_add(1, std::memory_order_relaxed);
    }
    return AUDIO_DATA_CALLBACK_RESULT_VALID;
}

} // namespace flynes::harmony
