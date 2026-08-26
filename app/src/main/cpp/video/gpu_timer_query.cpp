#include "gpu_timer_query.h"

#include <EGL/egl.h>

#include <cstring>

namespace flynes::video {
namespace {
constexpr GLenum kTimeElapsed = 0x88BF;
constexpr GLenum kQueryResultAvailable = 0x8867;
constexpr GLenum kQueryResult = 0x8866;
constexpr GLenum kGpuDisjoint = 0x8FBB;

template <typename T>
T proc(const char* name) {
    return reinterpret_cast<T>(eglGetProcAddress(name));
}
}  // namespace

bool GpuTimerQuery::initialize() {
    destroy();
    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!extensions || !std::strstr(extensions, "GL_EXT_disjoint_timer_query")) return false;
    gen_queries_ = proc<PFNGLGENQUERIESEXTPROC>("glGenQueriesEXT");
    delete_queries_ = proc<PFNGLDELETEQUERIESEXTPROC>("glDeleteQueriesEXT");
    begin_query_ = proc<PFNGLBEGINQUERYEXTPROC>("glBeginQueryEXT");
    end_query_ = proc<PFNGLENDQUERYEXTPROC>("glEndQueryEXT");
    get_query_uiv_ = proc<PFNGLGETQUERYOBJECTUIVEXTPROC>("glGetQueryObjectuivEXT");
    get_query_ui64v_ = proc<PFNGLGETQUERYOBJECTUI64VEXTPROC>("glGetQueryObjectui64vEXT");
    if (!gen_queries_ || !delete_queries_ || !begin_query_ || !end_query_
            || !get_query_uiv_ || !get_query_ui64v_) {
        destroy();
        return false;
    }
    gen_queries_(1, &query_);
    if (!query_ || glGetError() != GL_NO_ERROR) {
        destroy();
        return false;
    }
    status_ = Status::PENDING;
    return true;
}

void GpuTimerQuery::destroy() {
    if (query_ && delete_queries_) delete_queries_(1, &query_);
    query_ = 0;
    begun_ = false;
    in_flight_ = false;
    status_ = Status::UNAVAILABLE;
    last_duration_ns_ = -1;
    gen_queries_ = nullptr;
    delete_queries_ = nullptr;
    begin_query_ = nullptr;
    end_query_ = nullptr;
    get_query_uiv_ = nullptr;
    get_query_ui64v_ = nullptr;
}

bool GpuTimerQuery::begin() {
    poll();
    if (!query_ || in_flight_ || begun_) return false;
    begin_query_(kTimeElapsed, query_);
    if (glGetError() != GL_NO_ERROR) return false;
    begun_ = true;
    return true;
}

void GpuTimerQuery::end() {
    if (!begun_) return;
    end_query_(kTimeElapsed);
    begun_ = false;
    in_flight_ = glGetError() == GL_NO_ERROR;
    if (!in_flight_) {
        status_ = Status::UNAVAILABLE;
        last_duration_ns_ = -1;
    }
}

void GpuTimerQuery::poll() {
    if (!query_ || !in_flight_) return;
    GLboolean disjoint = GL_FALSE;
    glGetBooleanv(kGpuDisjoint, &disjoint);
    if (disjoint == GL_TRUE) {
        in_flight_ = false;
        status_ = Status::PENDING;
        last_duration_ns_ = -1;
        return;
    }
    GLuint available = GL_FALSE;
    get_query_uiv_(query_, kQueryResultAvailable, &available);
    if (available != GL_TRUE) return;
    GLuint64EXT elapsed = 0;
    get_query_ui64v_(query_, kQueryResult, &elapsed);
    in_flight_ = false;
    if (glGetError() == GL_NO_ERROR && elapsed > 0) {
        status_ = Status::VALID;
        last_duration_ns_ = static_cast<std::int64_t>(elapsed);
    } else {
        status_ = Status::PENDING;
        last_duration_ns_ = -1;
    }
}

}  // namespace flynes::video
