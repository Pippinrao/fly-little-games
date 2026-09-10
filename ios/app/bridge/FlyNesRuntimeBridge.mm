#import "FlyNesRuntimeBridge.h"

#include <flynes/flynes_runtime.h>

#include <vector>
#include <mutex>
#include <algorithm>

@implementation FlyNesRuntimeBridge {
    fly_runtime_t *runtime_;
    uint64_t frame_index_;
    uint64_t timeline_epoch_;
    uint32_t last_frame_samples_;
    uint32_t pending_samples_;
    std::recursive_mutex mutex_;
}

- (instancetype)init
{
    self = [super init];
    if (self != nil)
    {
        runtime_ = nullptr;
        frame_index_ = 0;
        timeline_epoch_ = 1;
    }
    return self;
}

- (void)dealloc
{
    [self destroyRuntime];
}

- (BOOL)createRuntime:(NSError **)error
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    [self destroyRuntime];
    fly_runtime_config config{};
    config.struct_size = FLY_RUNTIME_CONFIG_V1_SIZE;
    config.version = FLY_RUNTIME_CONFIG_VERSION_1;
    config.sample_rate = 0;
    config.reserved = 0;
    const fly_result result = fly_runtime_create(&config, &runtime_);
    if (result != FLY_RESULT_OK || runtime_ == nullptr)
    {
        runtime_ = nullptr;
        if (error != nullptr)
        {
            *error = [NSError errorWithDomain:@"com.flynes.runtime"
                                         code:result
                                     userInfo:@{NSLocalizedDescriptionKey : @"fly_runtime_create failed"}];
        }
        return NO;
    }
    frame_index_ = 0;
    return YES;
}

- (BOOL)loadRom:(NSData *)rom error:(NSError **)error
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (runtime_ == nullptr || rom.length == 0)
        return NO;
    const fly_result result = fly_runtime_load_rom(
        runtime_, static_cast<const uint8_t *>(rom.bytes), static_cast<size_t>(rom.length), nullptr);
    if (result != FLY_RESULT_OK)
    {
        if (error != nullptr)
        {
            *error = [NSError errorWithDomain:@"com.flynes.runtime"
                                         code:result
                                     userInfo:@{NSLocalizedDescriptionKey : @"fly_runtime_load_rom failed"}];
        }
        return NO;
    }
    frame_index_ = 0;
    timeline_epoch_ = 1;
    pending_samples_ = last_frame_samples_ = 0;
    return YES;
}

- (BOOL)stepFrameWithButtons:(uint32_t)buttons error:(NSError **)error
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (runtime_ == nullptr)
        return NO;
    // The bridge exposes at most one frame of pending audio. Discard an unconsumed
    // previous frame instead of letting a muted/slow caller build stale PCM.
    [self discardAudio];
    fly_frame_input_v1 input{};
    input.struct_size = FLY_FRAME_INPUT_V1_SIZE;
    input.version = FLY_FRAME_INPUT_VERSION_1;
    input.timeline_epoch = timeline_epoch_;
    input.frame_index = frame_index_;
    input.buttons[0] = buttons;
    fly_frame_result_v1 result{};
    result.struct_size = FLY_FRAME_RESULT_V1_SIZE;
    result.version = FLY_FRAME_RESULT_VERSION_1;
    const fly_result status = fly_runtime_step_frame(runtime_, &input, &result);
    if (status != FLY_RESULT_OK)
    {
        if (error != nullptr)
        {
            *error = [NSError errorWithDomain:@"com.flynes.runtime"
                                         code:status
                                     userInfo:@{NSLocalizedDescriptionKey : @"fly_runtime_step_frame failed"}];
        }
        return NO;
    }
    frame_index_ += 1;
    last_frame_samples_ = result.pcm_published
        ? static_cast<uint32_t>(result.audio_last_sample_sequence - result.audio_first_sample_sequence + 1) : 0;
    pending_samples_ += last_frame_samples_;
    return YES;
}

- (NSData *)saveCheckpoint:(NSError **)error
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (runtime_ == nullptr)
    {
        if (error != nullptr)
        {
            *error = [NSError errorWithDomain:@"com.flynes.runtime"
                                         code:FLY_RESULT_INVALID_STATE
                                     userInfo:@{NSLocalizedDescriptionKey : @"fly_runtime_save_checkpoint failed"}];
        }
        return nil;
    }
    size_t written = 0;
    size_t needed = 0;
    const fly_result query =
        fly_runtime_save_checkpoint(runtime_, nullptr, 0, &written, &needed);
    if (query != FLY_RESULT_BUFFER_TOO_SMALL || needed == 0)
    {
        const fly_result failure =
            query == FLY_RESULT_OK ? FLY_RESULT_INTERNAL_ERROR : query;
        if (error != nullptr)
        {
            *error = [NSError errorWithDomain:@"com.flynes.runtime"
                                         code:failure
                                     userInfo:@{NSLocalizedDescriptionKey : @"fly_runtime_save_checkpoint failed"}];
        }
        return nil;
    }
    std::vector<uint8_t> bytes(needed);
    written = 0;
    const fly_result status = fly_runtime_save_checkpoint(
        runtime_, bytes.data(), bytes.size(), &written, &needed);
    if (status != FLY_RESULT_OK || written == 0)
    {
        if (error != nullptr)
        {
            *error = [NSError errorWithDomain:@"com.flynes.runtime"
                                         code:status != FLY_RESULT_OK ? status
                                                                      : FLY_RESULT_INTERNAL_ERROR
                                     userInfo:@{NSLocalizedDescriptionKey : @"fly_runtime_save_checkpoint failed"}];
        }
        return nil;
    }
    return [NSData dataWithBytes:bytes.data() length:written];
}

- (BOOL)loadCheckpoint:(NSData *)blob error:(NSError **)error
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (runtime_ == nullptr || blob.length == 0)
    {
        if (error != nullptr)
        {
            *error = [NSError errorWithDomain:@"com.flynes.runtime"
                                         code:FLY_RESULT_INVALID_ARGUMENT
                                     userInfo:@{NSLocalizedDescriptionKey : @"fly_runtime_load_checkpoint failed"}];
        }
        return NO;
    }
    const fly_result status = fly_runtime_load_checkpoint(
        runtime_, static_cast<const uint8_t *>(blob.bytes), static_cast<size_t>(blob.length));
    if (status != FLY_RESULT_OK)
    {
        if (error != nullptr)
        {
            *error = [NSError errorWithDomain:@"com.flynes.runtime"
                                         code:status
                                     userInfo:@{NSLocalizedDescriptionKey : @"fly_runtime_load_checkpoint failed"}];
        }
        return NO;
    }
    std::vector<uint8_t> pixels(FLY_RUNTIME_RGB565_BYTES);
    fly_latest_frame_v1 meta{};
    meta.struct_size = FLY_LATEST_FRAME_V1_SIZE;
    meta.version = FLY_LATEST_FRAME_VERSION_1;
    const fly_result copied = fly_runtime_copy_latest_frame(
        runtime_, pixels.data(), pixels.size(), &meta);
    // A valid checkpoint can precede the very first published frame.
    if (copied == FLY_RESULT_INVALID_STATE)
    {
        frame_index_ = 0;
        timeline_epoch_ = 1;
    }
    else if (copied != FLY_RESULT_OK)
    {
        if (error != nullptr)
        {
            *error = [NSError errorWithDomain:@"com.flynes.runtime"
                                         code:copied
                                     userInfo:@{NSLocalizedDescriptionKey : @"fly_runtime_copy_latest_frame failed"}];
        }
        return NO;
    }
    else
    {
        frame_index_ = meta.frame_index + 1;
        timeline_epoch_ = meta.timeline_epoch;
    }
    pending_samples_ = last_frame_samples_ = 0;
    [self clearInput];
    return YES;
}

- (NSData *)copyLatestRgb565Frame
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (runtime_ == nullptr)
        return nil;
    std::vector<uint8_t> pixels(FLY_RUNTIME_RGB565_BYTES);
    fly_latest_frame_v1 meta{};
    meta.struct_size = FLY_LATEST_FRAME_V1_SIZE;
    meta.version = FLY_LATEST_FRAME_VERSION_1;
    const fly_result status = fly_runtime_copy_latest_frame(
        runtime_, pixels.data(), pixels.size(), &meta);
    if (status != FLY_RESULT_OK || meta.bytes_written != FLY_RUNTIME_RGB565_BYTES)
        return nil;
    return [NSData dataWithBytes:pixels.data() length:pixels.size()];
}

- (void)destroyRuntime
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    fly_runtime_destroy(runtime_);
    runtime_ = nullptr;
    frame_index_ = 0;
    timeline_epoch_ = 1;
    pending_samples_ = last_frame_samples_ = 0;
}

- (uint32_t)lastFrameSampleCount
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return last_frame_samples_;
}

- (NSData *)pullPCM
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!runtime_ || pending_samples_ == 0) return [NSData data];
    std::vector<int16_t> samples(std::min(pending_samples_, uint32_t(4096)));
    fly_pcm_block_v1 block{};
    block.struct_size = FLY_PCM_BLOCK_V1_SIZE;
    block.version = FLY_PCM_BLOCK_VERSION_1;
    if (fly_runtime_pull_pcm(runtime_, samples.data(), static_cast<uint32_t>(samples.size()), &block) != FLY_RESULT_OK)
        return [NSData data];
    pending_samples_ -= std::min(pending_samples_, block.sample_count);
    return [NSData dataWithBytes:samples.data() length:block.sample_count * sizeof(int16_t)];
}

- (void)discardAudio
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    while (pending_samples_ > 0) {
        if ([self pullPCM].length == 0) { pending_samples_ = 0; break; }
    }
}

- (void)clearInput
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (runtime_) fly_runtime_clear_input_ports(runtime_, (1u << FLY_RUNTIME_PORT_COUNT) - 1,
                                                FLY_RUNTIME_CLEAR_PAUSE);
}

@end
