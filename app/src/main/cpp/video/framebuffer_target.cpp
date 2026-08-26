#include "framebuffer_target.h"

#include <cstring>

namespace flynes::video {

bool FramebufferTarget::create_rgba8(int width, int height) {
    return create_rgba(width, height, GL_UNSIGNED_BYTE);
}

bool FramebufferTarget::create_rgba(int width, int height, GLenum component_type) {
    if (texture_ && framebuffer_ && width_ == width && height_ == height
            && component_type_ == component_type) return true;
    destroy();
    if (width <= 0 || height <= 0) return false;
    while (glGetError() != GL_NO_ERROR) {}
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    constexpr GLenum kHalfFloat = 0x140B;
    constexpr GLenum kHalfFloatOes = 0x8D61;
    constexpr GLint kRgba16f = 0x881A;
    constexpr GLint kRgba32f = 0x8814;
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    bool es3 = version && std::strstr(version, "OpenGL ES 3") != nullptr;
    GLint internal_format = GL_RGBA;
    GLenum upload_type = component_type;
    if (es3 && component_type == GL_FLOAT) internal_format = kRgba32f;
    if (es3 && component_type == kHalfFloatOes) {
        internal_format = kRgba16f;
        upload_type = kHalfFloat;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0,
                 GL_RGBA, upload_type, nullptr);
    glGenFramebuffers(1, &framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texture_, 0);
    if (!texture_ || !framebuffer_
            || glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        destroy();
        return false;
    }
    width_ = width;
    height_ = height;
    component_type_ = component_type;
    return glGetError() == GL_NO_ERROR;
}

void FramebufferTarget::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width_, height_);
}

void FramebufferTarget::destroy() {
    if (framebuffer_) glDeleteFramebuffers(1, &framebuffer_);
    if (texture_) glDeleteTextures(1, &texture_);
    framebuffer_ = texture_ = 0;
    width_ = height_ = 0;
    component_type_ = 0;
}

}  // namespace flynes::video
