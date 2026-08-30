#pragma once

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <cstdint>

namespace flynes::video {

/** EXT_disjoint_timer_query wrapper. CPU wall time is never accepted as a substitute. */
class GpuTimerQuery {
public:
    enum class Status : int { UNAVAILABLE = 0, PENDING = 1, VALID = 2 };

    bool initialize();
    void destroy();
    bool begin();
    void end();
    void poll();
    Status status() const { return status_; }
    std::int64_t last_duration_ns() const { return last_duration_ns_; }

private:
    PFNGLGENQUERIESEXTPROC gen_queries_ = nullptr;
    PFNGLDELETEQUERIESEXTPROC delete_queries_ = nullptr;
    PFNGLBEGINQUERYEXTPROC begin_query_ = nullptr;
    PFNGLENDQUERYEXTPROC end_query_ = nullptr;
    PFNGLGETQUERYOBJECTUIVEXTPROC get_query_uiv_ = nullptr;
    PFNGLGETQUERYOBJECTUI64VEXTPROC get_query_ui64v_ = nullptr;
    GLuint query_ = 0;
    bool begun_ = false;
    bool in_flight_ = false;
    Status status_ = Status::UNAVAILABLE;
    std::int64_t last_duration_ns_ = -1;
};

}  // namespace flynes::video
