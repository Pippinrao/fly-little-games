#pragma once
#include <flynes/flynes_app.h>
#include <flynes/flynes_session.h>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace flynes::harmony::nearby {
// Metadata only. A policy revision is a local invalidation fence, not OS permission.
class ContentPortV2 final {
public:
    using Ref = std::array<std::uint8_t, 16>;
    using Random = std::function<bool(Ref&)>;
    struct Selection {
        Ref ref{}, source_uuid{};
        std::array<std::uint8_t, 32> physical_hash{}, payload_hash{};
        std::array<std::uint8_t, 20> payload_sha1{};
        std::array<std::uint8_t, 4> payload_crc{};
        std::uint64_t catalog_generation = 0, policy_revision = 0;
        std::uint64_t physical_size = 0, payload_size = 0;
        std::uint32_t scope = 0, package_format = 0;
        std::string canonical_id, variant_id, relative_path, display_name;
        std::vector<std::uint8_t> zip_raw_name;
        std::int32_t zip_offset = -1;
    };
    struct QueryStatus { bool attempted = false; fly_session_result_v2 result = FLY_SESSION_V2_OK; };
    explicit ContentPortV2(Random random);
    ~ContentPortV2();
    ContentPortV2(const ContentPortV2&) = delete;
    ContentPortV2& operator=(const ContentPortV2&) = delete;
    // Copies one immutable snapshot synchronously; caller keeps it alive until return.
    fly_session_result_v2 publish(const fly_catalog_snapshot_t*, std::uint64_t policy_revision);
    void invalidate_policy();
    bool resolve(const Ref&, Selection*) const;
    fly_session_result_v2 query_record(std::uint32_t index, std::vector<std::uint8_t>*);
    QueryStatus query_status() const;
    fly_session_content_port_v2 port() const noexcept;
    void close();
private:
    struct State;
    State* state_;
};
}
