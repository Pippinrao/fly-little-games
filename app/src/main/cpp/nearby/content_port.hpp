#pragma once
#include <flynes/flynes_session.h>
#include <cstdint>
#include <functional>
#include <vector>

namespace flynes::android::nearby {
// Metadata-only callbacks. No callback may perform ROM IO or capture an owner.
// The retained context serializes calls and owns callbacks independently of owner.
class ContentPort final {
public:
    struct Callbacks final {
        std::function<fly_session_result_v2(std::uint32_t, std::vector<std::uint8_t>*)> query;
        std::function<bool(const std::uint8_t*)> validate;
        std::function<void()> close;
    };
    explicit ContentPort(Callbacks callbacks);
    ~ContentPort();
    ContentPort(const ContentPort&) = delete;
    ContentPort& operator=(const ContentPort&) = delete;
    fly_session_content_port_v2 port() const noexcept;
    struct QueryStatus final {
        bool attempted = false;
        fly_session_result_v2 result = FLY_SESSION_V2_OK;
    };
    QueryStatus query_status() const;
    static bool validate(const fly_session_content_port_v2&, const std::uint8_t ref[16]);
    static fly_session_result_v2 encode_record(const std::vector<std::uint8_t>&,
                                               std::vector<std::uint8_t>*);
private:
    struct Context;
    Context* context_;
};
}
