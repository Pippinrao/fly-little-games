#include "flynes/product/multiplayer_eligibility.hpp"

namespace flynes::product {

MultiplayerCapabilityRegistry::MultiplayerCapabilityRegistry(std::uint32_t profile_version) noexcept
    : profile_version_(profile_version)
{
}

MultiplayerEligibility MultiplayerCapabilityRegistry::eligibility_for(
    std::string_view canonical_id) const
{
    const auto found = entries_.find(std::string{canonical_id});
    if (found == entries_.end() || found->second.profile_version != profile_version_)
    {
        return MultiplayerEligibility::Unknown;
    }
    return found->second.eligibility;
}

} // namespace flynes::product
