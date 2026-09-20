#pragma once
#include "content_port.hpp"
#include <flynes/product/dual_runtime_port.hpp>
#include <memory>

namespace flynes::android::nearby {
// One selected ROM and one single-use START authorization, never a ROM library.
class PreparedContent final {
public:
    explicit PreparedContent(fly_session_content_port_v2 content);
    ~PreparedContent();
    fly_session_result_v2 begin(const fly_session_snapshot_v2&, const fly_session_game_choice_v2&, std::uint64_t*);
    fly_session_result_v2 stage(std::uint64_t, const std::uint8_t*, const std::uint8_t*,
        std::shared_ptr<const std::vector<std::uint8_t>>, const fly_session_snapshot_v2&);
    fly_session_result_v2 bind_config(const fly_session_snapshot_v2&);
    fly_session_result_v2 finish_selection(fly_session_result_v2, std::uint64_t,
        const fly_session_notice_v2&, const fly_session_snapshot_v2&);
    bool can_confirm(const fly_session_snapshot_v2&);
    fly_session_result_v2 authorize_start(fly_session_v2_t*, const fly_session_approval_token_v2_t*, std::uint64_t*);
    flynes::product::AuthorizedRom resolve(const fly_session_dual_content_ref_v2&);
    void reconcile(const fly_session_snapshot_v2&);
    void cancel(std::uint64_t ticket = 0);
    void close();
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
