#pragma once

#include <android/asset_manager.h>

#include <cstdint>
#include <vector>

namespace flynes::video {

class SpatialPipeline {
public:
    enum class Algorithm : int { NEAREST_2X = 0, MMPX_2X = 1, SCALEFX_3X = 2 };
    enum class Failure : int {
        NONE = 0,
        INVALID_INPUT = 1,
        EGL_CONTEXT_UNAVAILABLE = 2,
        SHADER_ASSET_MISSING = 3,
        SHADER_COMPILE_OR_LINK_FAILED = 4,
        FRAMEBUFFER_UNSUPPORTED = 5,
        GL_OPERATION_FAILED = 6,
        ALGORITHM_NOT_IMPLEMENTED = 7
    };

    struct Output {
        Failure failure = Failure::NONE;
        int width = 0;
        int height = 0;
        std::vector<std::uint32_t> pixels;
    };

    Output render(AAssetManager* assets, Algorithm algorithm,
                  const std::uint32_t* source, int width, int height);
};

}  // namespace flynes::video
