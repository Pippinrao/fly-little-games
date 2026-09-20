#pragma once

// Host link seam for OHAudio and the platform GPU only. NativePlayRuntime,
// PlaySession, PCM support and the NES core are compiled without substitution.
#include <cstdint>
#include <vector>

struct OH_AudioRendererStruct;
using OH_AudioRenderer = OH_AudioRendererStruct;
struct OH_AudioStreamBuilder;
using OH_AudioStream_Result = int;
using OH_AudioRenderer_OnWriteDataCallback = int (*)(OH_AudioRenderer*, void*, void*, std::int32_t);
constexpr int AUDIOSTREAM_SUCCESS = 0;
constexpr int AUDIOSTREAM_TYPE_RENDERER = 1;
constexpr int AUDIOSTREAM_SAMPLE_S16LE = 1;
constexpr int AUDIOSTREAM_ENCODING_TYPE_RAW = 1;
constexpr int AUDIOSTREAM_LATENCY_MODE_FAST = 1;
constexpr int AUDIOSTREAM_LATENCY_MODE_NORMAL = 0;
constexpr int AUDIOSTREAM_USAGE_GAME = 1;
constexpr int AUDIO_DATA_CALLBACK_RESULT_INVALID = -1;
constexpr int AUDIO_DATA_CALLBACK_RESULT_VALID = 0;
int OH_AudioStreamBuilder_Create(OH_AudioStreamBuilder**, int);
int OH_AudioStreamBuilder_Destroy(OH_AudioStreamBuilder*);
int OH_AudioStreamBuilder_SetSamplingRate(OH_AudioStreamBuilder*, int);
int OH_AudioStreamBuilder_SetChannelCount(OH_AudioStreamBuilder*, int);
int OH_AudioStreamBuilder_SetSampleFormat(OH_AudioStreamBuilder*, int);
int OH_AudioStreamBuilder_SetEncodingType(OH_AudioStreamBuilder*, int);
int OH_AudioStreamBuilder_SetLatencyMode(OH_AudioStreamBuilder*, int);
int OH_AudioStreamBuilder_SetRendererInfo(OH_AudioStreamBuilder*, int);
int OH_AudioStreamBuilder_SetFrameSizeInCallback(OH_AudioStreamBuilder*, int);
int OH_AudioStreamBuilder_SetRendererWriteDataCallback(OH_AudioStreamBuilder*, OH_AudioRenderer_OnWriteDataCallback, void*);
int OH_AudioStreamBuilder_GenerateRenderer(OH_AudioStreamBuilder*, OH_AudioRenderer**);
int OH_AudioRenderer_Start(OH_AudioRenderer*);
int OH_AudioRenderer_Pause(OH_AudioRenderer*);
int OH_AudioRenderer_Flush(OH_AudioRenderer*);
int OH_AudioRenderer_Stop(OH_AudioRenderer*);
int OH_AudioRenderer_Release(OH_AudioRenderer*);
int OH_AudioRenderer_GetFrameSizeInCallback(OH_AudioRenderer*, std::int32_t*);
int OH_AudioRenderer_GetSamplingRate(OH_AudioRenderer*, std::int32_t*);
int OH_AudioRenderer_GetChannelCount(OH_AudioRenderer*, std::int32_t*);
int OH_AudioRenderer_GetAudioTimestampInfo(OH_AudioRenderer*, std::int64_t*, std::int64_t*);

namespace flynes::harmony {
struct TestRenderStatus { struct { bool motion_qualified = false; } display; };
struct TestRenderer {
    bool submit_frame(std::uint64_t, std::uint32_t, std::uint32_t, const std::vector<std::uint8_t>&) { return true; }
    TestRenderStatus status() const { return {}; }
};
inline TestRenderer& harmony_renderer() { static TestRenderer renderer; return renderer; }
}
