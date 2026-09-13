// Include the real implementation to hold its private core mutex without test hooks.
#include "../src/runtime/flynes_runtime.cpp"

#include <array>
#include <cstdio>
#include <future>
#include <thread>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

fly_pcm_block_v1 make_block()
{
    fly_pcm_block_v1 block{};
    block.struct_size = FLY_PCM_BLOCK_V1_SIZE;
    block.version = FLY_PCM_BLOCK_VERSION_1;
    block.sample_count = 99u;
    block.reserved = 99u;
    block.first_sample_sequence = 99u;
    block.media_time_ns = 99u;
    return block;
}

void check_silence(const std::array<int16_t, 8>& samples, const fly_pcm_block_v1& block)
{
    check(std::all_of(samples.begin(), samples.end(), [](int16_t value) { return value == 0; }),
          "silence clears the entire requested sample buffer");
    check(block.struct_size == FLY_PCM_BLOCK_V1_SIZE &&
              block.version == FLY_PCM_BLOCK_VERSION_1 && block.sample_count == samples.size() &&
              block.reserved == 0u && block.first_sample_sequence == 0u && block.media_time_ns == 0u,
          "silence initializes metadata with sequence/time zero and full sample count");
}

} // namespace

int main()
{
    fly_runtime_config config{};
    config.struct_size = FLY_RUNTIME_CONFIG_V1_SIZE;
    config.version = FLY_RUNTIME_CONFIG_VERSION_1;
    fly_runtime_t* runtime = nullptr;
    check(fly_runtime_create(&config, &runtime) == FLY_RESULT_OK && runtime != nullptr,
          "real runtime creation succeeds");
    if (runtime == nullptr) return 1;

    constexpr std::uint64_t first_sequence = 48000u;
    const std::array<int16_t, 5> queued{{123, -234, 345, -456, 567}};
    std::array<int16_t, 8> samples;
    samples.fill(777);
    auto block = make_block();

    // Holding the same mutex as stepping/checkpoints makes contention deterministic.
    std::unique_lock<std::mutex> core_lock(runtime->mutex);
    runtime->pcm_producer_sequence = first_sequence;
    runtime->clear_pcm();
    runtime->push_pcm(queued.data(), static_cast<std::uint32_t>(queued.size()));
    std::promise<void> started;
    auto started_future = started.get_future();
    std::promise<fly_result> finished;
    auto finished_future = finished.get_future();
    std::thread callback([&] {
        started.set_value();
        finished.set_value(fly_runtime_pull_pcm(runtime, samples.data(),
                                               static_cast<std::uint32_t>(samples.size()), &block));
    });
    started_future.wait();
    const bool returned_while_held =
        finished_future.wait_for(std::chrono::milliseconds(1500)) == std::future_status::ready;
    // Always release and join before checking: the old blocking code fails without deadlocking.
    core_lock.unlock();
    callback.join();
    check(returned_while_held, "PCM pull returns while the core mutex remains held");
    check(finished_future.get() == FLY_RESULT_OK, "contended PCM pull succeeds");
    check_silence(samples, block);
    check(runtime->pcm_count == queued.size() && runtime->pcm_read == 0u &&
              runtime->pcm_read_sequence == first_sequence &&
              runtime->pcm_producer_sequence == first_sequence + queued.size(),
          "contention preserves queued samples and canonical producer/read cursors");

    block = make_block();
    check(fly_runtime_pull_pcm(runtime, samples.data(), 2u, &block) == FLY_RESULT_OK,
          "pull after releasing the core mutex succeeds");
    check(samples[0] == queued[0] && samples[1] == queued[1] && block.sample_count == 2u &&
              block.first_sample_sequence == first_sequence &&
              block.media_time_ns == first_sequence * 1000000000ull / runtime->sample_rate &&
              block.reserved == 0u,
          "first partial pull retains exact queued samples and sequence/time");
    samples.fill(777);
    check(fly_runtime_pull_pcm(runtime, samples.data(), 8u, &block) == FLY_RESULT_OK,
          "remaining partial drain succeeds");
    check(block.sample_count == 3u && block.first_sample_sequence == first_sequence + 2u &&
              block.media_time_ns == (first_sequence + 2u) * 1000000000ull / runtime->sample_rate &&
              samples[0] == queued[2] && samples[1] == queued[3] && samples[2] == queued[4] &&
              std::all_of(samples.begin() + 3, samples.end(), [](int16_t value) { return value == 777; }),
          "partial drain reports only copied samples and leaves unused output untouched");
    block = make_block();
    samples.fill(777);
    check(fly_runtime_pull_pcm(runtime, samples.data(), 8u, &block) == FLY_RESULT_OK,
          "empty queue underrun succeeds");
    check_silence(samples, block);
    check(runtime->pcm_count == 0u && runtime->pcm_read_sequence == first_sequence + queued.size() &&
              runtime->pcm_producer_sequence == first_sequence + queued.size(),
          "drains and underrun retain the canonical producer cursor");

    check(fly_runtime_pull_pcm(nullptr, samples.data(), 8u, &block) == FLY_RESULT_INVALID_ARGUMENT,
          "null runtime remains invalid");
    check(fly_runtime_pull_pcm(runtime, samples.data(), 8u, nullptr) == FLY_RESULT_INVALID_ARGUMENT,
          "null metadata remains invalid");
    check(fly_runtime_pull_pcm(runtime, nullptr, 8u, &block) == FLY_RESULT_INVALID_ARGUMENT,
          "null sample buffer remains invalid");
    check(fly_runtime_pull_pcm(runtime, samples.data(), 0u, &block) == FLY_RESULT_INVALID_ARGUMENT,
          "zero capacity remains invalid");
    block.struct_size = FLY_PCM_BLOCK_V1_SIZE - 1u;
    check(fly_runtime_pull_pcm(runtime, samples.data(), 8u, &block) == FLY_RESULT_STRUCT_TOO_SMALL,
          "short metadata remains invalid");
    block = make_block();
    block.version = FLY_PCM_BLOCK_VERSION_1 + 1u;
    check(fly_runtime_pull_pcm(runtime, samples.data(), 8u, &block) == FLY_RESULT_UNSUPPORTED_VERSION,
          "unsupported metadata version remains invalid");

    fly_runtime_destroy(runtime);
    return failures == 0 ? 0 : 1;
}
