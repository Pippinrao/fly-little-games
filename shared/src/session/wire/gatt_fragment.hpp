#ifndef FLYNES_SESSION_WIRE_GATT_FRAGMENT_HPP
#define FLYNES_SESSION_WIRE_GATT_FRAGMENT_HPP

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

namespace flynes::session::wire {

inline constexpr std::size_t kGattPhysicalHeaderSize = 14;
inline constexpr std::size_t kGattLogicalMinSize = 40;
inline constexpr std::size_t kGattLogicalMaxSize = 4096;
inline constexpr std::size_t kGattMaxIncompleteMessages = 2;
inline constexpr std::size_t kGattMaxBufferedBytes = 8192;
inline constexpr std::uint64_t kGattNoProgressTimeoutNs = UINT64_C(5000000000);

enum class GattPhysicalDirection : std::uint8_t
{
    CentralToPeripheral = 1,
    PeripheralToCentral = 2
};

enum class GattFragmentResult : std::int32_t
{
    Accepted = 1,
    Duplicate = 2,
    Complete = 3,
    Stale = -1,
    InvalidArgument = -2,
    NotSupported = -3,
    Backpressure = -4,
    ProtocolViolation = -5
};

enum class GattLogicalType : std::uint8_t
{
    QrRouteProbe = 1,
    QrRouteChallenge = 2,
    QrRouteResponse = 3,
    PairCommit = 4,
    PairReveal = 5,
    PairSignature = 6,
    KeyConfirm = 7,
    InitialBearerCredential = 8,
    ReconnectBootstrapRequest = 9,
    ReconnectBootstrapResponse = 10,
    ReconnectBootstrapPrepare = 11,
    ReconnectBootstrapAccept = 12,
    ReconnectContribution = 13,
    ReconnectProof = 14,
    ReconnectBearerReveal = 15,
    PhysicalAck = 16,
    PairKnownBranch = 17,
    Abort = 18,
    PairContext = 19,
    ReconnectContributionAck = 20,
    PairKnownStatus = 21,
    BearerEndpointOffer = 22,
    PairCapabilityReveal = 23,
    InitialBearerPlan = 24,
    InitialBearerPlanAck = 25,
    InitialBearerPlanFinal = 26,
    InviteCodeLookupRequest = 27,
    InviteCodeLookupResponse = 28
};

struct GattLogicalMessageView final
{
    std::uint8_t version = 0;
    std::uint8_t logical_type = 0;
    const std::uint8_t* body = nullptr;
    std::size_t body_size = 0;
    const std::uint8_t* logical_hash = nullptr;
};

inline constexpr std::size_t kGattLogicalAckBodySize = 36;

struct GattLogicalAckV1 final
{
    GattPhysicalDirection original_sender_physical_role{};
    std::uint8_t acked_logical_type = 0;
    std::uint16_t message_id = 0;
    std::array<std::uint8_t, 32> logical_hash{};
};

GattFragmentResult encode_gatt_logical_ack_v1(
    const GattLogicalAckV1& ack,
    std::array<std::uint8_t, kGattLogicalAckBodySize>* out) noexcept;

GattFragmentResult decode_gatt_logical_ack_v1(
    const std::uint8_t* bytes, std::size_t size,
    GattLogicalAckV1* out) noexcept;

struct GattCompletedMetadata final
{
    std::uint8_t logical_type = 0;
    std::uint16_t message_id = 0;
    std::array<std::uint8_t, 32> logical_hash{};
};

// Parses one complete logical message without copying. The returned windows are
// borrowed from `bytes` and remain valid only while that storage is alive.
GattFragmentResult decode_gatt_logical_message(
    const std::uint8_t* bytes, std::size_t size,
    std::uint8_t expected_type, GattLogicalMessageView* out) noexcept;

GattFragmentResult encode_gatt_logical_message(
    std::uint8_t logical_type, const std::uint8_t* body,
    std::size_t body_size, std::vector<std::uint8_t>* out) noexcept;

GattFragmentResult fragment_gatt_logical(
    const std::uint8_t* logical,
    std::size_t logical_size,
    std::uint8_t logical_type,
    std::uint16_t message_id,
    std::size_t att_value_cap,
    std::vector<std::vector<std::uint8_t>>* out) noexcept;

class GattReassembler
{
public:
    GattReassembler(std::uint64_t connection_generation,
                    GattPhysicalDirection direction);

    GattFragmentResult accept(std::uint64_t connection_generation,
                              std::uint64_t now_ns,
                              const std::uint8_t* physical_value,
                              std::size_t physical_size,
                              std::vector<std::uint8_t>* completed_logical,
                              GattCompletedMetadata* completed_metadata = nullptr);

    std::size_t expire(std::uint64_t now_ns) noexcept;
    [[nodiscard]] std::size_t incomplete_count() const noexcept;
    [[nodiscard]] std::size_t buffered_bytes() const noexcept;

private:
    struct Piece
    {
        std::uint16_t index = 0;
        std::uint16_t offset = 0;
        std::vector<std::uint8_t> bytes;
    };

    struct Assembly
    {
        std::uint8_t logical_type = 0;
        std::uint16_t message_id = 0;
        std::uint16_t fragment_count = 0;
        std::uint16_t total_length = 0;
        std::uint64_t last_progress_ns = 0;
        std::vector<Piece> pieces;
    };

    struct Completed
    {
        std::uint8_t logical_type = 0;
        std::uint16_t message_id = 0;
        std::uint16_t fragment_count = 0;
        std::uint16_t total_length = 0;
        std::vector<std::uint8_t> logical;
    };

    void erase(std::size_t index) noexcept;

    std::uint64_t generation_ = 0;
    GattPhysicalDirection direction_{};
    std::size_t buffered_bytes_ = 0;
    std::vector<Assembly> assemblies_;
    std::vector<Completed> completed_;
};

[[nodiscard]] constexpr bool gatt_ack_produces_authentication_evidence() noexcept
{
    return false;
}

} // namespace flynes::session::wire

#endif
