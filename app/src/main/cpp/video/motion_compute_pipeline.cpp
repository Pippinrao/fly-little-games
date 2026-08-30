#include "motion_compute_pipeline.h"

#include <GLES3/gl31.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace flynes::video {
namespace {

int luma565(std::uint16_t pixel) {
    const int r = ((pixel >> 11) & 31) * 255 / 31;
    const int g = ((pixel >> 5) & 63) * 255 / 63;
    const int b = (pixel & 31) * 255 / 31;
    return (54 * r + 183 * g + 19 * b) >> 8;
}

bool global_hold(const std::uint16_t* a, const std::uint16_t* b, std::size_t count) {
    std::uint64_t changed = 0, large = 0, same_sign = 0;
    int brightness_sign = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (a[i] == b[i]) continue;
        ++changed;
        const int delta = luma565(b[i]) - luma565(a[i]);
        if (std::abs(delta) >= 96) ++large;
        const int sign = delta > 0 ? 1 : (delta < 0 ? -1 : 0);
        if (brightness_sign == 0 && sign != 0) brightness_sign = sign;
        if (sign != 0 && sign == brightness_sign) ++same_sign;
    }
    const double ratio = count == 0 ? 1.0
            : static_cast<double>(changed) / static_cast<double>(count);
    const bool flash = ratio >= 0.80 && large * 10 >= changed * 9
            && same_sign * 10 >= changed * 9;
    return flash || ratio >= 0.45;
}

constexpr char kVectorShader[] = R"GLSL(#version 310 es
layout(local_size_x=1, local_size_y=1) in;
layout(std430, binding=0) readonly buffer FrameA { uint a[]; };
layout(std430, binding=1) readonly buffer FrameB { uint b[]; };
layout(std430, binding=2) writeonly buffer Vectors { ivec4 vectors[]; };
layout(std430, binding=5) buffer CoarseVectors { ivec4 coarseVectors[]; };
uniform int width;
uniform int height;
uniform int blocksX;
uniform int coarseBlocksX;
uniform int stage;
const int BLOCK = 4;
const int COARSE = 8;
const int COARSE_RADIUS = 8;
const int REFINE_RADIUS = 2;

int distance565(uint lhs, uint rhs) {
    int lr = int((lhs >> 11u) & 31u), lg = int((lhs >> 5u) & 63u), lb = int(lhs & 31u);
    int rr = int((rhs >> 11u) & 31u), rg = int((rhs >> 5u) & 63u), rb = int(rhs & 31u);
    return abs(lr - rr) * 2 + abs(lg - rg) + abs(lb - rb) * 2;
}

uint fineCost(bool reverse, int fromX, int fromY, int toX, int toY) {
    uint total = 0u;
    for (int yy = 0; yy < BLOCK; ++yy) for (int xx = 0; xx < BLOCK; ++xx) {
        int fi = (fromY + yy) * width + fromX + xx;
        int ti = (toY + yy) * width + toX + xx;
        total += uint(reverse ? distance565(b[fi], a[ti]) : distance565(a[fi], b[ti]));
    }
    return total;
}

ivec4 refineSearch(bool reverse, int x, int y, int centerDx, int centerDy) {
    uint best = 0xffffffffu, second = 0xffffffffu;
    int bestDx = 0, bestDy = 0, ties = 0;
    for (int oy = -REFINE_RADIUS; oy <= REFINE_RADIUS; ++oy)
    for (int ox = -REFINE_RADIUS; ox <= REFINE_RADIUS; ++ox) {
        int dx = centerDx + ox, dy = centerDy + oy;
        int tx = x + dx, ty = y + dy;
        if (tx < 0 || ty < 0 || tx + BLOCK > width || ty + BLOCK > height) continue;
        uint candidate = fineCost(reverse, x, y, tx, ty);
        if (candidate < best) {
            second = best; best = candidate; bestDx = dx; bestDy = dy; ties = 1;
        } else if (candidate == best) {
            ++ties;
        } else if (candidate < second) {
            second = candidate;
        }
    }
    return ivec4(bestDx, bestDy, ties, int(min(second, 0x7fffffffu)));
}

uint coarseCost(bool reverse, int anchorX, int anchorY, int dxQuarter, int dyQuarter) {
    uint total = 0u;
    int dx = dxQuarter * 4, dy = dyQuarter * 4;
    for (int qy = 0; qy < COARSE; ++qy) for (int qx = 0; qx < COARSE; ++qx) {
        int fi = (anchorY + qy * 4) * width + anchorX + qx * 4;
        int ti = (anchorY + qy * 4 + dy) * width + anchorX + qx * 4 + dx;
        total += uint(reverse ? distance565(b[fi], a[ti]) : distance565(a[fi], b[ti]));
    }
    return total;
}

ivec2 coarseSearch(bool reverse, int anchorX, int anchorY) {
    int coarseDx = 0, coarseDy = 0;
    uint best = 0xffffffffu;
    for (int dyq = -COARSE_RADIUS; dyq <= COARSE_RADIUS; ++dyq)
    for (int dxq = -COARSE_RADIUS; dxq <= COARSE_RADIUS; ++dxq) {
        int tx = anchorX + dxq * 4, ty = anchorY + dyq * 4;
        int coarseFull = COARSE * 4;
        if (tx < 0 || ty < 0 || tx + coarseFull > width || ty + coarseFull > height)
            continue;
        uint candidate = coarseCost(reverse, anchorX, anchorY, dxq, dyq);
        if (candidate < best) {
            best = candidate;
            coarseDx = dxq * 4;
            coarseDy = dyq * 4;
        }
    }
    return ivec2(coarseDx, coarseDy);
}

ivec4 hierarchicalSearch(bool reverse, int x, int y) {
    const int coarseFull = COARSE * 4;
    int anchorX = (x / coarseFull) * coarseFull;
    int anchorY = (y / coarseFull) * coarseFull;
    int coarseDx = 0, coarseDy = 0;
    if (anchorX + coarseFull <= width && anchorY + coarseFull <= height) {
        ivec4 coarse = coarseVectors[(anchorY / coarseFull) * coarseBlocksX
                                     + anchorX / coarseFull];
        ivec2 selected = reverse ? coarse.zw : coarse.xy;
        coarseDx = selected.x;
        coarseDy = selected.y;
    }
    return refineSearch(reverse, x, y, coarseDx, coarseDy);
}

void main() {
    int bx = int(gl_GlobalInvocationID.x), by = int(gl_GlobalInvocationID.y);
    if (stage == 0) {
        int anchorX = bx * COARSE * 4, anchorY = by * COARSE * 4;
        coarseVectors[by * coarseBlocksX + bx] = ivec4(
                coarseSearch(false, anchorX, anchorY),
                coarseSearch(true, anchorX, anchorY));
        return;
    }
    int x = bx * BLOCK, y = by * BLOCK;
    if (x + BLOCK > width || y + BLOCK > height) return;
    int outIndex = by * blocksX + bx;
    uint zero = fineCost(false, x, y, x, y);
    if (zero == 0u) { vectors[outIndex] = ivec4(0); return; }
    ivec4 forward = hierarchicalSearch(false, x, y);
    int destinationX = x + forward.x, destinationY = y + forward.y;
    ivec4 backward = hierarchicalSearch(true, destinationX, destinationY);
    uint best = fineCost(false, x, y, destinationX, destinationY);
    bool unique = forward.z == 1 && backward.z == 1;
    bool reciprocal = abs(forward.x + backward.x) <= 1 && abs(forward.y + backward.y) <= 1;
    bool useful = best * 4u < zero * 3u;
    bool separated = forward.w == 0x7fffffff || best + uint(BLOCK * BLOCK * 2) < uint(forward.w);
    vectors[outIndex] = unique && reciprocal && useful && separated
            ? ivec4(forward.xy, 1, 0) : ivec4(0, 0, -1, 0);
}
)GLSL";

constexpr char kSynthesisShader[] = R"GLSL(#version 310 es
layout(local_size_x=8, local_size_y=8) in;
layout(std430, binding=0) readonly buffer FrameA { uint a[]; };
layout(std430, binding=1) readonly buffer FrameB { uint b[]; };
layout(std430, binding=2) readonly buffer Vectors { ivec4 vectors[]; };
layout(std430, binding=3) buffer Output { uint outputPixels[]; };
layout(std430, binding=4) buffer Owners { uint owners[]; };
uniform int width;
uniform int height;
uniform int blocksX;
uniform int phase;
const int BLOCK = 4;

int halfNearest(int value) { return value >= 0 ? (value + 1) / 2 : (value - 1) / 2; }

void main() {
    int x = int(gl_GlobalInvocationID.x), y = int(gl_GlobalInvocationID.y);
    if (x >= width || y >= height) return;
    int index = y * width + x;
    int bx = x / BLOCK, by = y / BLOCK;
    bool fullBlock = (bx + 1) * BLOCK <= width && (by + 1) * BLOCK <= height;
    ivec4 motion = fullBlock ? vectors[by * blocksX + bx] : ivec4(0);
    if (phase == 0) {
        outputPixels[index] = motion.z == 1 && (motion.x != 0 || motion.y != 0) ? b[index] : a[index];
        owners[index] = 0xffffffffu;
    } else if (phase == 1) {
        if (motion.z == 1 && (motion.x != 0 || motion.y != 0)) {
            int tx = clamp(x + halfNearest(motion.x), 0, width - 1);
            int ty = clamp(y + halfNearest(motion.y), 0, height - 1);
            atomicMin(owners[ty * width + tx], uint(index));
        }
    } else if (phase == 2) {
        uint owner = owners[index];
        if (owner != 0xffffffffu) outputPixels[index] = a[owner];
    }
}
)GLSL";

GLuint compile_compute(const char* source) {
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        glDeleteShader(shader);
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

void bind_buffer(GLuint buffer, GLuint binding, const void* data, std::size_t bytes,
                 GLenum usage) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(bytes), data, usage);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buffer);
}

}  // namespace

MotionComputePipeline::~MotionComputePipeline() { destroy(); }

bool MotionComputePipeline::initialize() {
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 3 || (major == 3 && minor < 1)) return false;
    vector_program_ = compile_compute(kVectorShader);
    synth_program_ = compile_compute(kSynthesisShader);
    if (!vector_program_ || !synth_program_) {
        destroy();
        return false;
    }
    return glGetError() == GL_NO_ERROR;
}

MotionComputePipeline::Output MotionComputePipeline::interpolate(
        const std::uint16_t* a, const std::uint16_t* b, int width, int height) {
    Output output;
    if (!a || !b || width <= 0 || height <= 0 || !vector_program_ || !synth_program_) {
        return output;
    }
    const std::size_t count = static_cast<std::size_t>(width) * height;
    output.pixels.assign(a, a + count);
    if (global_hold(a, b, count)) {
        output.failure = Failure::NONE;
        output.generated = false;
        output.unsafe_ratio = 1.0;
        return output;
    }

    std::vector<std::uint32_t> expanded_a(count), expanded_b(count), expanded_output(count);
    std::vector<std::uint32_t> owners(count, 0xffffffffu);
    for (std::size_t i = 0; i < count; ++i) {
        expanded_a[i] = a[i];
        expanded_b[i] = b[i];
    }
    const int blocks_x = width / 4;
    const int blocks_y = height / 4;
    const int coarse_blocks_x = width / 32;
    const int coarse_blocks_y = height / 32;
    std::vector<std::int32_t> vectors(
            static_cast<std::size_t>(blocks_x) * blocks_y * 4u);
    std::vector<std::int32_t> coarse_vectors(std::max<std::size_t>(4u,
            static_cast<std::size_t>(coarse_blocks_x) * coarse_blocks_y * 4u));
    GLuint buffers[6] = {};
    glGenBuffers(6, buffers);
    bind_buffer(buffers[0], 0, expanded_a.data(), expanded_a.size() * sizeof(std::uint32_t),
                GL_STATIC_DRAW);
    bind_buffer(buffers[1], 1, expanded_b.data(), expanded_b.size() * sizeof(std::uint32_t),
                GL_STATIC_DRAW);
    bind_buffer(buffers[2], 2, vectors.data(), vectors.size() * sizeof(std::int32_t),
                GL_DYNAMIC_COPY);
    bind_buffer(buffers[3], 3, expanded_output.data(),
                expanded_output.size() * sizeof(std::uint32_t), GL_DYNAMIC_READ);
    bind_buffer(buffers[4], 4, owners.data(), owners.size() * sizeof(std::uint32_t),
                GL_DYNAMIC_COPY);
    bind_buffer(buffers[5], 5, coarse_vectors.data(),
                coarse_vectors.size() * sizeof(std::int32_t), GL_DYNAMIC_COPY);

    glUseProgram(vector_program_);
    glUniform1i(glGetUniformLocation(vector_program_, "width"), width);
    glUniform1i(glGetUniformLocation(vector_program_, "height"), height);
    glUniform1i(glGetUniformLocation(vector_program_, "blocksX"), blocks_x);
    glUniform1i(glGetUniformLocation(vector_program_, "coarseBlocksX"), coarse_blocks_x);
    if (coarse_blocks_x > 0 && coarse_blocks_y > 0) {
        glUniform1i(glGetUniformLocation(vector_program_, "stage"), 0);
        glDispatchCompute(static_cast<GLuint>(coarse_blocks_x),
                          static_cast<GLuint>(coarse_blocks_y), 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
    glUniform1i(glGetUniformLocation(vector_program_, "stage"), 1);
    glDispatchCompute(static_cast<GLuint>(blocks_x), static_cast<GLuint>(blocks_y), 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[2]);
    const GLsizeiptr vector_bytes = static_cast<GLsizeiptr>(
            vectors.size() * sizeof(std::int32_t));
    void* mapped_vectors = glMapBufferRange(
            GL_SHADER_STORAGE_BUFFER, 0, vector_bytes, GL_MAP_READ_BIT);
    if (mapped_vectors) {
        std::memcpy(vectors.data(), mapped_vectors, static_cast<std::size_t>(vector_bytes));
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }
    if (!mapped_vectors || glGetError() != GL_NO_ERROR) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glDeleteBuffers(6, buffers);
        output.pixels.clear();
        output.failure = Failure::GL_OPERATION_FAILED;
        return output;
    }
    std::uint64_t unsafe_pixels = 0;
    for (std::size_t i = 2; i < vectors.size(); i += 4) {
        if (vectors[i] < 0) unsafe_pixels += 16;
    }
    output.unsafe_ratio = std::min(1.0, static_cast<double>(unsafe_pixels)
            / static_cast<double>(count));
    if (output.unsafe_ratio > 0.10) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glDeleteBuffers(6, buffers);
        output.failure = Failure::NONE;
        output.generated = false;
        return output;
    }

    glUseProgram(synth_program_);
    glUniform1i(glGetUniformLocation(synth_program_, "width"), width);
    glUniform1i(glGetUniformLocation(synth_program_, "height"), height);
    glUniform1i(glGetUniformLocation(synth_program_, "blocksX"), blocks_x);
    glUniform1i(glGetUniformLocation(synth_program_, "phase"), 0);
    const GLuint groups_x = static_cast<GLuint>((width + 7) / 8);
    const GLuint groups_y = static_cast<GLuint>((height + 7) / 8);
    glDispatchCompute(groups_x, groups_y, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUniform1i(glGetUniformLocation(synth_program_, "phase"), 1);
    glDispatchCompute(groups_x, groups_y, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUniform1i(glGetUniformLocation(synth_program_, "phase"), 2);
    glDispatchCompute(groups_x, groups_y, 1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[3]);
    const GLsizeiptr output_bytes = static_cast<GLsizeiptr>(
            expanded_output.size() * sizeof(std::uint32_t));
    void* mapped = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, output_bytes, GL_MAP_READ_BIT);
    if (mapped) {
        std::memcpy(expanded_output.data(), mapped, static_cast<std::size_t>(output_bytes));
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }
    const GLenum error = glGetError();
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glDeleteBuffers(6, buffers);
    if (!mapped || error != GL_NO_ERROR) {
        output.pixels.clear();
        output.failure = Failure::GL_OPERATION_FAILED;
        return output;
    }
    for (std::size_t i = 0; i < count; ++i)
        output.pixels[i] = static_cast<std::uint16_t>(expanded_output[i]);
    output.failure = Failure::NONE;
    output.generated = true;
    return output;
}

void MotionComputePipeline::destroy() {
    if (vector_program_) glDeleteProgram(vector_program_);
    if (synth_program_) glDeleteProgram(synth_program_);
    vector_program_ = synth_program_ = 0;
}

}  // namespace flynes::video
