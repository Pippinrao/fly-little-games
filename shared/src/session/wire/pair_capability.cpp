#include "pair_capability.hpp"

#include "sha256.hpp"

#include <algorithm>
#include <cstring>

namespace flynes::session::wire {
namespace {

constexpr std::size_t kSummarySize = 512;
constexpr std::size_t kPlansOffset = 96;
constexpr std::size_t kPlanSize = 48;
constexpr std::size_t kMaxPlans = 8;

bool zeros(const std::uint8_t* bytes, std::size_t size) noexcept
{
    for (std::size_t i = 0; i < size; ++i)
    {
        if (bytes[i] != 0)
            return false;
    }
    return true;
}

Status validate_plan(const std::uint8_t* p, std::uint8_t platform) noexcept
{
    if (p[7] != 0 || !zeros(p + 44, 4))
        return Status::NonzeroReserved;
    if (p[0] < 2 || p[0] > 4 || p[1] < 1 || p[1] > 2 ||
        p[2] < 1 || p[2] > 2 || p[3] < 1 || p[3] > 2 ||
        p[4] < 1 || p[4] > 3 || p[5] > 2)
        return Status::UnknownEnum;

    const std::uint8_t expected_rank = p[0] == 2 ? 10 : (p[0] == 3 ? 20 : 30);
    const std::uint8_t expected_codec = p[0] == 4 ? 1 : 2;
    if (p[6] != expected_rank || p[3] != expected_codec ||
        (p[4] == 3 && (platform != 3 || p[0] != 3 || p[3] != 2)) ||
        zeros(p + 8, 4) || zeros(p + 12, 32))
        return Status::InvalidField;
    return Status::Ok;
}

bool preferred(const std::uint8_t* candidate, const std::uint8_t* current) noexcept
{
    // Certification ID is u32be, so byte order is also unsigned numeric order.
    constexpr std::size_t fields[]{6, 5, 0, 1, 2, 3, 4};
    for (const auto offset : fields)
    {
        if (candidate[offset] != current[offset])
            return candidate[offset] < current[offset];
    }
    return std::memcmp(candidate + 8, current + 8, 36) < 0;
}

} // namespace

Status validate_bearer_plan_v1(const BearerPlanBytes& plan,
                               std::uint8_t platform) noexcept
{
    return platform >= 1 && platform <= 3
        ? validate_plan(plan.data(), platform) : Status::UnknownEnum;
}

std::array<std::uint8_t, 32> selected_bearer_plan_hash_v1(
    const BearerPlanBytes& plan) noexcept
{
    static constexpr char domain[] = "flynes-selected-bearer-plan-v1";
    std::array<std::uint8_t, sizeof(domain) - 1 + 4 + 48> preimage{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(domain),
                sizeof(domain) - 1, preimage.begin());
    preimage[sizeof(domain) - 1 + 3] = 48;
    std::copy(plan.begin(), plan.end(),
              preimage.begin() + sizeof(domain) - 1 + 4);
    return sha256(preimage.data(), preimage.size());
}

Status validate_pair_capability(const std::uint8_t* bytes, std::size_t size) noexcept
{
    if (bytes == nullptr)
        return Status::InvalidField;
    if (size < kSummarySize)
        return Status::Truncated;
    if (size > kSummarySize)
        return Status::Trailing;
    if (bytes[0] != 0 || bytes[1] != 1)
        return Status::InvalidField;
    if (!zeros(bytes + 2, 6) || !zeros(bytes + 10, 2) ||
        !zeros(bytes + 16, 16) || !zeros(bytes + 480, 32))
        return Status::NonzeroReserved;
    if (bytes[8] < 1 || bytes[8] > 3)
        return Status::UnknownEnum;
    const std::size_t count = bytes[9];
    if (count > kMaxPlans || zeros(bytes + 32, 32) || zeros(bytes + 64, 32))
        return Status::InvalidField;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto* p = bytes + kPlansOffset + i * kPlanSize;
        const auto status = validate_plan(p, bytes[8]);
        if (status != Status::Ok)
            return status;
        if (i != 0 && std::memcmp(p - kPlanSize, p, kPlanSize) >= 0)
            return Status::InvalidField;
    }
    if (!zeros(bytes + kPlansOffset + count * kPlanSize, (kMaxPlans - count) * kPlanSize))
        return Status::NonzeroReserved;
    return Status::Ok;
}

PairSelectionStatus select_pair_plan(const std::uint8_t* initiator,
                                     std::size_t initiator_size,
                                     const std::uint8_t* responder,
                                     std::size_t responder_size,
                                     BearerPlanBytes& selected) noexcept
{
    const auto initiator_status = validate_pair_capability(initiator, initiator_size);
    const auto responder_status = validate_pair_capability(responder, responder_size);
    if (initiator_status != Status::Ok || responder_status != Status::Ok)
    {
        selected.fill(0);
        return initiator_status != Status::Ok ? PairSelectionStatus::InvalidInitiator
                                             : PairSelectionStatus::InvalidResponder;
    }

    const std::uint8_t* best = nullptr;
    for (std::size_t i = 0; i < initiator[9]; ++i)
    {
        const auto* candidate = initiator + kPlansOffset + i * kPlanSize;
        for (std::size_t j = 0; j < responder[9]; ++j)
        {
            const auto* remote = responder + kPlansOffset + j * kPlanSize;
            if (std::memcmp(candidate, remote, kPlanSize) == 0 &&
                (best == nullptr || preferred(candidate, best)))
                best = candidate;
        }
    }
    if (best == nullptr)
    {
        selected.fill(0);
        return PairSelectionStatus::NoCommonPlan;
    }
    // Stage the result so an output buffer overlapping an input is harmless.
    BearerPlanBytes result{};
    std::copy_n(best, kPlanSize, result.begin());
    selected = result;
    return PairSelectionStatus::Ok;
}

} // namespace flynes::session::wire
