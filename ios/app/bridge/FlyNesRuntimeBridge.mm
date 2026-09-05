#import "FlyNesRuntimeBridge.h"

#include <flynes/flynes_runtime.h>

#include <vector>

@implementation FlyNesRuntimeBridge {
    fly_runtime_t *runtime_;
    uint64_t frame_index_;
}

- (instancetype)init
{
    self = [super init];
    if (self != nil)
    {
        runtime_ = nullptr;
        frame_index_ = 0;
    }
    return self;
}

- (void)dealloc
{
    [self destroyRuntime];
}

- (BOOL)createRuntime:(NSError **)error
{
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
    return YES;
}

- (BOOL)stepFrameWithButtons:(uint32_t)buttons error:(NSError **)error
{
    if (runtime_ == nullptr)
        return NO;
    fly_frame_input_v1 input{};
    input.struct_size = FLY_FRAME_INPUT_V1_SIZE;
    input.version = FLY_FRAME_INPUT_VERSION_1;
    input.timeline_epoch = 1;
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
    return YES;
}

- (NSData *)copyLatestRgb565Frame
{
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
    fly_runtime_destroy(runtime_);
    runtime_ = nullptr;
    frame_index_ = 0;
}

@end
