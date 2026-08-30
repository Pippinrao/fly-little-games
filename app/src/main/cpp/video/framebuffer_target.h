#pragma once

#include <GLES2/gl2.h>

namespace flynes::video {

class FramebufferTarget {
public:
    FramebufferTarget() = default;
    ~FramebufferTarget() { destroy(); }
    FramebufferTarget(const FramebufferTarget&) = delete;
    FramebufferTarget& operator=(const FramebufferTarget&) = delete;

    bool create_rgba8(int width, int height);
    bool create_rgba(int width, int height, GLenum component_type);
    void destroy();
    void bind() const;
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

}  // namespace flynes::video
