#pragma once

#include <cstdint>
#include <vector>

namespace flynes::video {

class MotionComputePipeline {
public:
    enum class Failure : int {
        NONE = 0,
        INVALID_INPUT = 1,
        EGL_CONTEXT_UNAVAILABLE = 2,
        ES31_UNAVAILABLE = 3,
        COMPUTE_SHADER_FAILURE = 4,
        GL_OPERATION_FAILED = 5
    };
    struct Output {
        Failure failure = Failure::INVALID_INPUT;
        bool generated = false;
        double unsafe_ratio = 1.0;
        std::vector<std::uint16_t> pixels;
    };

    MotionComputePipeline() = default;
    ~MotionComputePipeline();
    MotionComputePipeline(const MotionComputePipeline&) = delete;
    MotionComputePipeline& operator=(const MotionComputePipeline&) = delete;

    bool initialize();
    Output interpolate(const std::uint16_t* a, const std::uint16_t* b,
                       int width, int height);
    void destroy();

private:
    unsigned int vector_program_ = 0;
    unsigned int synth_program_ = 0;
};

}  // namespace flynes::video
