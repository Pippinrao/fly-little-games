#pragma once

#include <GLES2/gl2.h>

#include "presenter_types.h"

namespace flynes::video {

/** Pixel-identical native port of the legacy Java GLES2 filters. */
class BaselinePipeline {
public:
    bool initialize();
    void destroy();
    void resize(int width, int height);
    void set_filter(FilterMode mode) { requested_filter_ = mode; }
    bool render(const StagedFrame& frame);

private:
    bool ensure_program();
    bool upload(const StagedFrame& frame);
    static GLuint compile(GLenum type, const char* source);
    static GLuint link(const char* vertex, const char* fragment);
    void uniform2f(const char* name, float first, float second);

    GLuint program_ = 0;
    GLuint texture_ = 0;
    int texture_width_ = 0;
    int texture_height_ = 0;
    PixelFormat texture_format_ = PixelFormat::RGB565;
    int output_width_ = 0;
    int output_height_ = 0;
    FilterMode requested_filter_ = FilterMode::EDGE_ENHANCED;
    int active_filter_ = -1;
};

}  // namespace flynes::video
