#include "native_play_runtime.hpp"
#include "native_audio_test_backend.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
using flynes::harmony::NativePlayRuntime;

// Enabled only on the audio control thread by the failing platform Release.
// Other threads (including the status assertion) keep their normal allocator.
thread_local bool reject_worker_allocations = false;
std::atomic<unsigned> rejected_worker_allocations{0};
void* operator new(std::size_t size) {
    if (reject_worker_allocations) {
        rejected_worker_allocations.fetch_add(1);
        throw std::bad_alloc();
    }
    if (void* value = std::malloc(size == 0 ? 1 : size)) return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }
void operator delete(void* value, const std::nothrow_t&) noexcept { std::free(value); }
void operator delete[](void* value, const std::nothrow_t&) noexcept { std::free(value); }

struct OH_AudioStreamBuilder {
    OH_AudioRenderer_OnWriteDataCallback callback = nullptr;
    void* user = nullptr;
    int fast = 1;
};
enum class BackendState { PREPARED, RUNNING, PAUSED, STOPPED, RELEASED };
struct OH_AudioRendererStruct : OH_AudioStreamBuilder { BackendState state = BackendState::PREPARED; };
namespace {
std::mutex backend_mutex;
std::condition_variable backend_wake;
OH_AudioRenderer* current = nullptr;
bool block_release = false;
bool release_entered = false;
bool allow_release = false;
int live_renderers = 0;
int starts = 0;
enum class Operation { NONE, CREATE, START, TIMESTAMP };
Operation blocked_operation = Operation::NONE;
bool operation_entered = false;
bool allow_operation = false;
bool fail_create = false;
bool fail_start = false;
int release_failures = 0;
int peak_renderers = 0;
int renderer_creations = 0;
int illegal_state_calls = 0;
bool reject_allocations_after_release_failure = false;
void gate(Operation operation) {
    std::unique_lock lock(backend_mutex);
    if (blocked_operation == operation) {
        operation_entered = true; backend_wake.notify_all();
        backend_wake.wait(lock, [] { return allow_operation; });
    }
}
void reset_backend() {
    std::lock_guard lock(backend_mutex);
    if (live_renderers != 0) throw std::runtime_error("previous test leaked renderer");
    block_release = release_entered = allow_release = false;
    blocked_operation = Operation::NONE;
    operation_entered = allow_operation = fail_create = fail_start = false;
    starts = release_failures = peak_renderers = renderer_creations = illegal_state_calls = 0;
    reject_allocations_after_release_failure = false;
}
void unblock() {
    { std::lock_guard lock(backend_mutex); allow_release = allow_operation = true; }
    backend_wake.notify_all();
}
struct UnblockOnExit { ~UnblockOnExit() { unblock(); } };
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Predicate> bool wait_for(Predicate predicate, std::chrono::milliseconds timeout = 2s) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    do { if (predicate()) return true; std::this_thread::sleep_for(1ms); } while (std::chrono::steady_clock::now() < end);
    return predicate();
}
std::vector<std::uint8_t> rom() {
    std::ifstream input(FLYNES_HARMONY_RUNTIME_ROM_FIXTURE, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void callback(OH_AudioRenderer* renderer, std::size_t samples) {
    std::vector<std::int16_t> output(samples, 123);
    renderer->callback(renderer, renderer->user, output.data(), static_cast<int>(output.size() * 2));
}
void stalled_fallback_preserves_short_input() {
    auto bytes = rom();
    auto runtime = NativePlayRuntime::open(bytes.data(), bytes.size());
    UnblockOnExit cleanup;
    require(wait_for([&] { return runtime->status().audio_started; }), "initial sink did not start");
    OH_AudioRenderer* old;
    { std::lock_guard lock(backend_mutex); old = current; block_release = true; }
    for (int i = 0; i < 3; ++i) callback(old, 48000);
    { std::unique_lock lock(backend_mutex); require(backend_wake.wait_for(lock, 2s, [] { return release_entered; }), "fallback did not release"); }
    const auto before = runtime->status().source_frames;
    const auto input_release = std::chrono::steady_clock::now() + 60ms;
    runtime->set_buttons(8);
    const bool observed = wait_for([&] { return runtime->copy_latest_frame().applied_buttons == 8; }, 60ms);
    std::this_thread::sleep_until(input_release);
    runtime->set_buttons(0);
    std::this_thread::sleep_for(140ms);
    const auto after = runtime->status().source_frames;
    const auto consumed = runtime->status().audio_consumed_samples;
    callback(old, 960);
    const bool retired_silent = runtime->status().audio_consumed_samples == consumed;
    { std::lock_guard lock(backend_mutex); allow_release = true; }
    backend_wake.notify_all();
    runtime->close();
    std::cout << "blocked release: source " << before << " -> " << after << ", short input consumed=" << observed << '\n';
    require(after >= before + 5, "source stepping stalled during 200 ms audio release");
    require(observed, "real source frame did not consume short START input during release");
    require(retired_silent, "retired callback consumed PCM during release");
    require(live_renderers == 0, "close leaked renderer");
}
void pending_create_obeys_pause_and_mute(bool mute) {
    { std::lock_guard lock(backend_mutex); blocked_operation = Operation::CREATE; }
    auto bytes = rom();
    auto runtime = NativePlayRuntime::open(bytes.data(), bytes.size());
    UnblockOnExit cleanup;
    { std::unique_lock lock(backend_mutex); require(backend_wake.wait_for(lock, 2s, [] { return operation_entered; }), "create did not enter"); }
    const auto before = runtime->status().source_frames;
    const auto input_release = std::chrono::steady_clock::now() + 60ms;
    runtime->set_buttons(8);
    const bool observed = wait_for([&] { return runtime->copy_latest_frame().applied_buttons == 8; }, 60ms);
    std::this_thread::sleep_until(input_release);
    runtime->set_buttons(0);
    std::this_thread::sleep_for(140ms);
    const auto after = runtime->status().source_frames;
    if (mute) runtime->set_muted(true); else runtime->set_paused(true);
    unblock();
    std::this_thread::sleep_for(50ms);
    const bool stale_started = runtime->status().audio_ready || runtime->status().audio_started;
    const auto discarded = runtime->status().audio_dropped_samples;
    if (mute) runtime->set_muted(false); else runtime->set_paused(false);
    const bool recovered = wait_for([&] { return runtime->status().audio_started; });
    runtime->close();
    require(!stale_started, "obsolete create published after pause/mute");
    require(observed && after >= before + 5, "slow Create blocked real source frames/input");
    require(recovered, "resume/unmute failed to recover sink");
    require(discarded > 0, "pause/mute queue clear was not counted as dropped");
    require(peak_renderers <= 1 && live_renderers == 0, "generation created overlapping/leaked sink");
}
void pending_start_does_not_block_source_or_restart_after_pause() {
    { std::lock_guard lock(backend_mutex); blocked_operation = Operation::START; }
    auto bytes = rom();
    auto runtime = NativePlayRuntime::open(bytes.data(), bytes.size());
    UnblockOnExit cleanup;
    { std::unique_lock lock(backend_mutex); require(backend_wake.wait_for(lock, 2s, [] { return operation_entered; }), "start did not enter"); }
    runtime->set_buttons(8);
    const bool observed = wait_for([&] { return runtime->copy_latest_frame().applied_buttons == 8; }, 60ms);
    runtime->set_buttons(0);
    runtime->set_paused(true);
    unblock();
    std::this_thread::sleep_for(50ms);
    const bool late_started = runtime->status().audio_started;
    runtime->close();
    require(observed, "slow Start blocked source input");
    require(!late_started, "Start completion restarted paused sink");
    require(live_renderers == 0, "close leaked pending Start renderer");
}
void close_joins_pending_create_without_start() {
    { std::lock_guard lock(backend_mutex); blocked_operation = Operation::CREATE; }
    auto bytes = rom();
    auto runtime = NativePlayRuntime::open(bytes.data(), bytes.size());
    UnblockOnExit cleanup;
    { std::unique_lock lock(backend_mutex); require(backend_wake.wait_for(lock, 2s, [] { return operation_entered; }), "create did not enter"); }
    std::atomic<bool> closed{false};
    std::thread closer([&] { runtime->close(); closed.store(true); });
    std::this_thread::sleep_for(30ms);
    const bool joined_early = closed.load();
    unblock(); closer.join();
    require(!joined_early, "close returned before control worker was joined");
    require(starts == 0 && live_renderers == 0, "pending create started/leaked after close");
}
void failed_create_can_recover_on_new_generation() {
    { std::lock_guard lock(backend_mutex); fail_create = true; }
    auto bytes = rom();
    auto runtime = NativePlayRuntime::open(bytes.data(), bytes.size());
    require(wait_for([&] { auto s = runtime->status(); return !s.audio_fast_path && !s.audio_fallback_reason.empty(); }), "failed creation has no fallback reason");
    const bool falsely_ready = runtime->status().audio_ready;
    runtime->set_muted(true);
    { std::lock_guard lock(backend_mutex); fail_create = false; }
    runtime->set_muted(false);
    const bool recovered = wait_for([&] { return runtime->status().audio_started; });
    runtime->close();
    require(!falsely_ready && recovered, "failed creation readiness/recovery is incorrect");
}
void failed_start_does_not_spin() {
    { std::lock_guard lock(backend_mutex); fail_start = true; }
    auto bytes = rom();
    auto runtime = NativePlayRuntime::open(bytes.data(), bytes.size());
    require(wait_for([&] { return runtime->status().audio_fallback_reason == "audio renderer start failed"; }), "failed Start not reported");
    std::this_thread::sleep_for(80ms);
    int attempts;
    { std::lock_guard lock(backend_mutex); attempts = starts; }
    runtime->close();
    require(attempts == 1, "failed Start spins instead of waiting for recovery generation");
}
void failed_release_retains_ownership_until_retry() {
    auto bytes = rom();
    auto runtime = NativePlayRuntime::open(bytes.data(), bytes.size());
    require(wait_for([&] { return runtime->status().audio_started; }), "initial sink did not start");
    OH_AudioRenderer* old;
    { std::lock_guard lock(backend_mutex); release_failures = 1; old = current; }
    for (int i = 0; i < 3; ++i) callback(old, 48000);
    const bool recovered = wait_for([&] { auto s = runtime->status(); return !s.audio_fast_path && s.audio_started; });
    runtime->close();
    require(recovered, "transient Release failure did not recover");
    require(live_renderers == 0 && peak_renderers <= 1, "failed Release lost ownership or overlapped replacement");
}
void persistent_release_failure_is_bounded_and_disables_creation() {
    auto bytes = rom();
    auto runtime = NativePlayRuntime::open(bytes.data(), bytes.size());
    require(wait_for([&] { return runtime->status().audio_started; }), "initial sink did not start");
    OH_AudioRenderer* old;
    { std::lock_guard lock(backend_mutex); release_failures = 20; old = current; }
    for (int i = 0; i < 3; ++i) callback(old, 48000);
    const bool disabled = wait_for([&] { return runtime->status().audio_fallback_reason.find("disabled for this process") != std::string::npos; });
    const bool unavailable = !runtime->status().audio_ready && !runtime->status().audio_started;
    const auto begin = std::chrono::steady_clock::now();
    runtime->close();
    const auto close_time = std::chrono::steady_clock::now() - begin;
    runtime.reset();
    auto next = NativePlayRuntime::open(bytes.data(), bytes.size());
    const bool explained = wait_for([&] { return next->status().audio_fallback_reason.find("disabled for this process") != std::string::npos; });
    next->close();
    require(disabled && unavailable && explained, "persistent Release failure did not expose unavailable reason");
    require(close_time < 500ms, "close retried Release without a bound");
    require(renderer_creations == 1 && live_renderers == 1 && release_failures == 17,
        "permanent failure created a replacement or retried outside 2+1 bound");
}
void slow_timestamp_does_not_block_input_or_publish_after_mute() {
    auto bytes = rom();
    auto runtime = NativePlayRuntime::open(bytes.data(), bytes.size());
    UnblockOnExit cleanup;
    require(wait_for([&] { return runtime->status().audio_started; }), "initial sink did not start");
    require(wait_for([&] { return runtime->status().audio_timestamp_valid; }), "first timestamp unavailable");
    { std::lock_guard lock(backend_mutex); blocked_operation = Operation::TIMESTAMP; }
    bool entered;
    { std::unique_lock lock(backend_mutex); entered = backend_wake.wait_for(lock, 2s, [] { return operation_entered; }); }
    if (!entered) { runtime->close(); require(false, "timestamp did not enter platform boundary"); }
    runtime->set_buttons(8);
    const bool observed = wait_for([&] { return runtime->copy_latest_frame().applied_buttons == 8; }, 60ms);
    runtime->set_buttons(0);
    runtime->set_muted(true);
    unblock();
    std::this_thread::sleep_for(30ms);
    const bool stale = runtime->status().audio_timestamp_valid;
    runtime->close();
    require(observed && !stale, "slow timestamp stalled source or published stale muted position");
}
void stalled_output_counts_queue_overflow() {
    { std::lock_guard lock(backend_mutex); blocked_operation = Operation::CREATE; }
    auto bytes = rom();
    auto runtime = NativePlayRuntime::open(bytes.data(), bytes.size());
    UnblockOnExit cleanup;
    const bool overflow = wait_for([&] { return runtime->status().audio_dropped_samples > 0; }, 1600ms);
    const auto status = runtime->status();
    unblock(); runtime->close();
    require(overflow && status.source_frames > 60 && status.audio_produced_samples > status.audio_queued_samples,
        "stalled output did not advance source and count rejected PCM");
    require(status.audio_queued_samples <= 48000, "PCM exceeded bounded capacity");
}
void close_waits_for_retired_callback_and_prevents_replacement() {
    auto bytes = rom();
    auto runtime = NativePlayRuntime::open(bytes.data(), bytes.size());
    UnblockOnExit cleanup;
    require(wait_for([&] { return runtime->status().audio_started; }), "initial sink did not start");
    OH_AudioRenderer* old;
    { std::lock_guard lock(backend_mutex); old = current; block_release = true; }
    std::atomic<bool> closed{false};
    std::thread closer([&] { runtime->close(); closed.store(true); });
    bool entered;
    { std::unique_lock lock(backend_mutex); entered = backend_wake.wait_for(lock, 2s, [] { return release_entered; }); }
    const auto consumed = runtime->status().audio_consumed_samples;
    if (entered) callback(old, 960);
    const bool silent = runtime->status().audio_consumed_samples == consumed;
    const bool returned_early = closed.load();
    unblock(); closer.join(); runtime.reset();
    require(entered && silent && !returned_early, "close failed callback quiescence boundary");
    require(renderer_creations == 1 && live_renderers == 0, "close created/leaked a replacement sink");
}
void pause_and_mute_reuse_renderer() {
    auto bytes = rom();
    auto runtime = NativePlayRuntime::open(bytes.data(), bytes.size());
    require(wait_for([&] { return runtime->status().audio_started; }), "initial sink did not start");
    runtime->set_paused(true);
    std::this_thread::sleep_for(30ms);
    runtime->set_paused(false);
    require(wait_for([&] { return runtime->status().audio_started; }), "resume did not restart");
    runtime->set_muted(true);
    std::this_thread::sleep_for(30ms);
    runtime->set_muted(false);
    const bool recovered = wait_for([&] { return runtime->status().audio_started; });
    runtime->close();
    require(recovered && renderer_creations == 1 && illegal_state_calls == 0,
        "pause/mute recreated a healthy renderer or called an illegal platform state");
}
void prepared_sink_and_stale_start_preserve_actual_state() {
    auto bytes = rom();
    auto runtime = NativePlayRuntime::open(bytes.data(), bytes.size());
    UnblockOnExit cleanup;
    require(wait_for([&] { return runtime->status().audio_ready; }), "prepared renderer missing");
    runtime->set_muted(true);
    std::this_thread::sleep_for(20ms);
    runtime->set_paused(true);
    runtime->set_muted(true);
    std::this_thread::sleep_for(20ms);
    { std::lock_guard lock(backend_mutex); blocked_operation = Operation::START; }
    runtime->set_muted(false);
    runtime->set_paused(false);
    { std::unique_lock lock(backend_mutex); require(backend_wake.wait_for(lock, 2s, [] { return operation_entered; }), "Start did not block"); }
    runtime->set_paused(true);
    unblock();
    const bool physically_paused = wait_for([] {
        std::lock_guard lock(backend_mutex);
        return current != nullptr && current->state == BackendState::PAUSED;
    });
    const bool wrongly_published = runtime->status().audio_started;
    runtime->set_paused(false);
    const bool resumed = wait_for([&] { return runtime->status().audio_started; });
    runtime->close();
    require(physically_paused && !wrongly_published && resumed,
        "stale successful Start lost actual RUNNING state or failed to pause/resume");
    require(renderer_creations == 1 && illegal_state_calls == 0,
        "Prepared/Paused interleave used illegal control call or recreated sink");
}
void first_close_release_failure_needs_no_worker_allocation() {
    auto bytes = rom();
    auto runtime = NativePlayRuntime::open(bytes.data(), bytes.size());
    require(wait_for([&] { return runtime->status().audio_started; }), "initial FAST sink did not start");
    require(runtime->status().audio_fallback_reason.empty(), "first-close test requires unused reason storage");
    {
        std::lock_guard lock(backend_mutex);
        release_failures = 1;
        reject_allocations_after_release_failure = true;
    }
    runtime->close();
    const auto closed = runtime->status();
    runtime.reset();
    require(!closed.running && !closed.audio_ready &&
        closed.audio_fallback_reason.find("disabled for this process") != std::string::npos,
        "first close failure did not finish with explicit unavailable state");
    require(rejected_worker_allocations.load() == 0,
        "close failure path attempted a worker allocation after Release failed");
    auto next = NativePlayRuntime::open(bytes.data(), bytes.size());
    const bool disabled = wait_for([&] {
        return next->status().audio_fallback_reason.find("disabled for this process") != std::string::npos;
    });
    next->close();
    require(disabled && live_renderers == 1 && renderer_creations == 1 && release_failures == 0,
        "failed close did not retain the isolated resource and suppress later creation");
}
}
int OH_AudioStreamBuilder_Create(OH_AudioStreamBuilder** value, int) { *value = new OH_AudioStreamBuilder; return 0; }
int OH_AudioStreamBuilder_Destroy(OH_AudioStreamBuilder* value) { delete value; return 0; }
#define SETTER(name) int name(OH_AudioStreamBuilder*, int) { return 0; }
SETTER(OH_AudioStreamBuilder_SetSamplingRate)
SETTER(OH_AudioStreamBuilder_SetChannelCount)
SETTER(OH_AudioStreamBuilder_SetSampleFormat)
SETTER(OH_AudioStreamBuilder_SetEncodingType)
SETTER(OH_AudioStreamBuilder_SetRendererInfo)
SETTER(OH_AudioStreamBuilder_SetFrameSizeInCallback)
#undef SETTER
int OH_AudioStreamBuilder_SetLatencyMode(OH_AudioStreamBuilder* value, int mode) { value->fast = mode; return 0; }
int OH_AudioStreamBuilder_SetRendererWriteDataCallback(OH_AudioStreamBuilder* value, OH_AudioRenderer_OnWriteDataCallback callback, void* user) { value->callback = callback; value->user = user; return 0; }
int OH_AudioStreamBuilder_GenerateRenderer(OH_AudioStreamBuilder* value, OH_AudioRenderer** result) {
    gate(Operation::CREATE);
    { std::lock_guard lock(backend_mutex); if (fail_create) return -1; }
    auto* renderer = new OH_AudioRenderer;
    static_cast<OH_AudioStreamBuilder&>(*renderer) = *value;
    { std::lock_guard lock(backend_mutex); current = renderer; ++live_renderers; ++renderer_creations; peak_renderers = std::max(peak_renderers, live_renderers); }
    *result = renderer; return 0;
}
int OH_AudioRenderer_Start(OH_AudioRenderer* renderer) {
    gate(Operation::START); std::lock_guard lock(backend_mutex); ++starts;
    if (renderer->state != BackendState::PREPARED && renderer->state != BackendState::PAUSED &&
        renderer->state != BackendState::STOPPED) { ++illegal_state_calls; return -1; }
    if (fail_start) return -1;
    renderer->state = BackendState::RUNNING; return 0;
}
int OH_AudioRenderer_Pause(OH_AudioRenderer* renderer) {
    std::lock_guard lock(backend_mutex);
    if (renderer->state != BackendState::RUNNING) { ++illegal_state_calls; return -1; }
    renderer->state = BackendState::PAUSED; return 0;
}
int OH_AudioRenderer_Flush(OH_AudioRenderer* renderer) {
    std::lock_guard lock(backend_mutex);
    if (renderer->state != BackendState::RUNNING && renderer->state != BackendState::PAUSED &&
        renderer->state != BackendState::STOPPED) { ++illegal_state_calls; return -1; }
    return 0;
}
int OH_AudioRenderer_Stop(OH_AudioRenderer* renderer) {
    std::lock_guard lock(backend_mutex);
    renderer->state = BackendState::STOPPED; return 0;
}
int OH_AudioRenderer_Release(OH_AudioRenderer* renderer) {
    std::unique_lock lock(backend_mutex);
    if (block_release && renderer->fast) {
        release_entered = true; backend_wake.notify_all();
        backend_wake.wait(lock, [] { return allow_release; });
    }
    if (release_failures > 0) {
        --release_failures;
        if (reject_allocations_after_release_failure) reject_worker_allocations = true;
        return -1;
    }
    if (current == renderer) current = nullptr;
    --live_renderers; delete renderer; return 0;
}
int OH_AudioRenderer_GetFrameSizeInCallback(OH_AudioRenderer*, std::int32_t* value) { *value = 960; return 0; }
int OH_AudioRenderer_GetSamplingRate(OH_AudioRenderer*, std::int32_t* value) { *value = 48000; return 0; }
int OH_AudioRenderer_GetChannelCount(OH_AudioRenderer*, std::int32_t* value) { *value = 1; return 0; }
int OH_AudioRenderer_GetAudioTimestampInfo(OH_AudioRenderer*, std::int64_t* position, std::int64_t* timestamp) {
    gate(Operation::TIMESTAMP); *position = 48000; *timestamp = 1000000000; return 0;
}
int main(int argc, char** argv) {
    try {
        reset_backend();
        const std::string test = argc > 1 ? argv[1] : "fallback";
        if (test == "fallback") stalled_fallback_preserves_short_input();
        else if (test == "pause") pending_create_obeys_pause_and_mute(false);
        else if (test == "mute") pending_create_obeys_pause_and_mute(true);
        else if (test == "start") pending_start_does_not_block_source_or_restart_after_pause();
        else if (test == "close") close_joins_pending_create_without_start();
        else if (test == "create_failure") failed_create_can_recover_on_new_generation();
        else if (test == "start_failure") failed_start_does_not_spin();
        else if (test == "release_failure") failed_release_retains_ownership_until_retry();
        else if (test == "reuse") pause_and_mute_reuse_renderer();
        else if (test == "persistent_release") persistent_release_failure_is_bounded_and_disables_creation();
        else if (test == "timestamp") slow_timestamp_does_not_block_input_or_publish_after_mute();
        else if (test == "overflow") stalled_output_counts_queue_overflow();
        else if (test == "close_callback") close_waits_for_retired_callback_and_prevents_replacement();
        else if (test == "state_interleave") prepared_sink_and_stale_start_preserve_actual_state();
        else if (test == "close_noalloc") first_close_release_failure_needs_no_worker_allocation();
        else throw std::runtime_error("unknown test");
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    std::cout << "PASS native runtime audio control\n";
}
