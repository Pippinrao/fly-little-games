#pragma once

#include <GLES3/gl3.h>

#include <array>
#include <cstdint>
#include <string>

namespace flynes::harmony {

class GlProgram final
{
public:
    GlProgram() = default;
    ~GlProgram();
    GlProgram(const GlProgram&) = delete;
    GlProgram& operator=(const GlProgram&) = delete;
    bool build(const char* vertex, const char* fragment, std::string* error);
    void destroy();
    GLuint id() const { return id_; }

private:
    GLuint id_ = 0;
};

class GlTarget final
{
public:
    GlTarget() = default;
    ~GlTarget();
    GlTarget(const GlTarget&) = delete;
    GlTarget& operator=(const GlTarget&) = delete;
    bool create(int width, int height, GLenum component_type);
    bool create_rgba8(int width, int height) { return create(width, height, GL_UNSIGNED_BYTE); }
    void bind() const;
    void destroy();
    GLuint texture() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    GLuint framebuffer_ = 0;
    GLuint texture_ = 0;
    int width_ = 0;
    int height_ = 0;
    GLenum component_type_ = 0;
};

class HarmonySpatialPipeline final
{
public:
    enum class Failure
    {
        NONE,
        SHADER,
        FRAMEBUFFER,
        GL,
    };

    ~HarmonySpatialPipeline();
    bool render(std::int32_t spatial_mode, GLuint source, int width, int height);
    GLuint output_texture() const { return output_.texture(); }
    int output_width() const { return output_.width(); }
    int output_height() const { return output_.height(); }
    Failure failure() const { return failure_; }
    const std::string& error() const { return error_; }
    void destroy();

private:
    bool ensure_mmpx();
    bool ensure_scalefx();
    bool render_mmpx(GLuint source, int width, int height);
    bool render_scalefx(GLuint source, int width, int height);
    bool draw_scale_pass(int pass, GLuint primary, GLuint secondary,
                         int width, int height, const GlTarget& destination);

    GlProgram mmpx_;
    std::array<GlProgram, 5> scalefx_;
    GlTarget pass0_;
    GlTarget pass1_;
    GlTarget pass2_;
    GlTarget pass3_;
    GlTarget output_;
    Failure failure_ = Failure::NONE;
    std::string error_;
};

} // namespace flynes::harmony
