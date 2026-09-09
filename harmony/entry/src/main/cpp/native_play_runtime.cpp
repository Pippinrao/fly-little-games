#include "native_play_runtime.hpp"

#include "harmony_renderer.hpp"

#include <ohaudio/native_audiostreambuilder.h>
#include <ohaudio/native_audiorenderer.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <stdexcept>

namespace flynes::harmony {
namespace {

bool audio_ok(OH_AudioStream_Result result)
{
    return result == AUDIOSTREAM_SUCCESS;
}

using GetAudioTimestamp = OH_AudioStream_Result (*)(OH_AudioRenderer*,
                                                     std::int64_t*, std::int64_t*);

GetAudioTimestamp get_audio_timestamp_api()
{
    static void* library = dlopen("libohaudio.so", RTLD_NOW | RTLD_LOCAL);
    static auto api = library == nullptr ? nullptr : reinterpret_cast<GetAudioTimestamp>(
        dlsym(library, "OH_AudioRenderer_GetAudioTimestampInfo"));
    return api;
}

} // namespace

std::unique_ptr<NativePlayRuntime> NativePlayRuntime::open(
    const std::uint8_t* rom, std::size_t size)
{
    SourceTiming timing = detect_source_timing(rom, size);
    auto result = std::unique_ptr<NativePlayRuntime>(
        new NativePlayRuntime(PlaySession::open(rom, size), std::move(timing)));
    result->initialize_audio(true);
    if (!result->audio_ready_.load(std::memory_order_acquire))
    {
        result->audio_fast_path_.store(false, std::memory_order_release);
        result->release_audio();
        result->initialize_audio(false);
    }
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
    paused_.store(paused, std::memory_order_release);
    if (paused)
    {
        std::lock_guard lock(audio_mutex_);
        if (audio_renderer_ != nullptr)
        {
            (void)OH_AudioRenderer_Pause(audio_renderer_);
            (void)OH_AudioRenderer_Flush(audio_renderer_);
            audio_started_.store(false, std::memory_order_release);
        }
        pcm_.clear();
        temporal_audio_.reset();
        audio_queued_samples_.store(0, std::memory_order_release);
    }
    else
    {
        std::lock_guard lock(audio_mutex_);
        // The simulation worker restarts the sink after refilling its prime.
    }
    wake_.notify_all();
}

void NativePlayRuntime::set_muted(bool muted) noexcept
{
    muted_.store(muted, std::memory_order_release);
    if (muted)
    {
        std::lock_guard lock(audio_mutex_);
        if (audio_renderer_ != nullptr && audio_started_.load(std::memory_order_acquire))
        {
            (void)OH_AudioRenderer_Pause(audio_renderer_);
            (void)OH_AudioRenderer_Flush(audio_renderer_);
            audio_started_.store(false, std::memory_order_release);
        }
        pcm_.clear();
        audio_queued_samples_.store(0, std::memory_order_release);
    }
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
    if (!result.audio_fast_path)
    {
        result.audio_fallback_reason = "fast audio path underrun; using normal latency";
    }
    result.audio_delay_samples = temporal_audio_.current_delay_samples();
    result.audio_temporal_state = temporal_audio_.state();
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
    if (thread_.joinable()) thread_.join();
    release_audio();
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
            temporal_audio_.set_motion_enabled(
                harmony_renderer().status().display.motion_qualified);
            if (!paused_.load(std::memory_order_acquire) &&
                !muted_.load(std::memory_order_acquire) && !step.pcm.empty())
            {
                bool start_audio = false;
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
                    pcm_.push(adjusted.data(), adjusted.size());
                    audio_dropped_samples_.store(pcm_.dropped_samples(), std::memory_order_release);
                    audio_produced_samples_.store(pcm_.produced_samples(), std::memory_order_release);
                    audio_queued_samples_.store(pcm_.size(), std::memory_order_release);
                    audio_high_water_samples_.store(
                        pcm_.high_water_samples(), std::memory_order_release);
                    const auto callback_frames = static_cast<std::size_t>(std::max(
                        1, audio_callback_frames_.load(std::memory_order_acquire)));
                    start_audio = audio_renderer_ != nullptr &&
                        pcm_.size() >= audio_start_prime_samples(callback_frames) &&
                        !audio_started_.load(std::memory_order_acquire);
                    if (start_audio && audio_renderer_ != nullptr &&
                        !paused_.load(std::memory_order_acquire) &&
                        !muted_.load(std::memory_order_acquire) &&
                        audio_ok(OH_AudioRenderer_Start(audio_renderer_)))
                    {
                        audio_started_.store(true, std::memory_order_release);
                    }
                }
            }
            step.pcm.clear();
            step.pcm_sample_count = 0;
            {
                std::lock_guard lock(latest_mutex_);
                latest_ = step;
            }
            first_frame_ready_.notify_all();
            const std::uint64_t count = source_frames_.fetch_add(
                1, std::memory_order_acq_rel) + 1;
            if (count % 60 == 0) sample_audio_timestamp();
            if (should_fallback_audio_latency(
                    audio_underflows_.load(std::memory_order_acquire),
                    audio_fast_path_.load(std::memory_order_acquire)))
            {
                audio_fast_path_.store(false, std::memory_order_release);
                release_audio();
                audio_fallback_underflow_baseline_.store(
                    audio_underflows_.load(std::memory_order_acquire),
                    std::memory_order_release);
                initialize_audio(false);
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

void NativePlayRuntime::initialize_audio(bool fast_path)
{
    OH_AudioStreamBuilder* builder = nullptr;
    if (!audio_ok(OH_AudioStreamBuilder_Create(&builder, AUDIOSTREAM_TYPE_RENDERER)) ||
        builder == nullptr)
    {
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
        audio_ok(OH_AudioStreamBuilder_GenerateRenderer(builder, &audio_renderer_));
    (void)OH_AudioStreamBuilder_Destroy(builder);
    if (!configured || audio_renderer_ == nullptr)
    {
        release_audio();
        return;
    }
    std::int32_t callback_frames = 0;
    if (audio_ok(OH_AudioRenderer_GetFrameSizeInCallback(
            audio_renderer_, &callback_frames)) && callback_frames > 0)
    {
        audio_callback_frames_.store(callback_frames, std::memory_order_release);
    }
    std::int32_t sample_rate = 0;
    if (audio_ok(OH_AudioRenderer_GetSamplingRate(audio_renderer_, &sample_rate)) &&
        sample_rate > 0)
    {
        audio_sample_rate_.store(sample_rate, std::memory_order_release);
    }
    std::int32_t channel_count = 0;
    if (audio_ok(OH_AudioRenderer_GetChannelCount(audio_renderer_, &channel_count)) &&
        channel_count > 0)
    {
        audio_channel_count_.store(channel_count, std::memory_order_release);
    }
    audio_ready_.store(true, std::memory_order_release);
}

void NativePlayRuntime::release_audio()
{
    std::lock_guard lock(audio_mutex_);
    if (audio_renderer_ != nullptr)
    {
        (void)OH_AudioRenderer_Stop(audio_renderer_);
        (void)OH_AudioRenderer_Release(audio_renderer_);
        audio_renderer_ = nullptr;
    }
    pcm_.clear();
    temporal_audio_.reset();
    audio_queued_samples_.store(0, std::memory_order_release);
    audio_ready_.store(false, std::memory_order_release);
    audio_started_.store(false, std::memory_order_release);
    audio_timestamp_valid_.store(false, std::memory_order_release);
}

void NativePlayRuntime::sample_audio_timestamp()
{
    GetAudioTimestamp api = get_audio_timestamp_api();
    if (api == nullptr || audio_renderer_ == nullptr) return;
    std::int64_t position = 0;
    std::int64_t timestamp = 0;
    if (api(audio_renderer_, &position, &timestamp) == AUDIOSTREAM_SUCCESS &&
        position >= 0 && timestamp > 0)
    {
        audio_frame_position_.store(position, std::memory_order_release);
        audio_timestamp_ns_.store(timestamp, std::memory_order_release);
        audio_timestamp_valid_.store(true, std::memory_order_release);
    }
}

int NativePlayRuntime::audio_write(OH_AudioRenderer*, void* user_data,
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
