#include <flynes/product/dual_start_identity.hpp>
#include "../session/dual/dual_runtime_contract.hpp"
#include "../session/wire/sha256.hpp"

#include <cstring>

namespace flynes::product {
namespace {

// Same domain_hash encoding with bounded stack storage: the required-start
// identities have fixed preimages, so this noexcept helper need not allocate.
template<std::size_t DomainSize, std::size_t PayloadSize>
std::array<std::uint8_t, 32> identity_hash(
    const char (&domain)[DomainSize], const std::uint8_t (&payload)[PayloadSize]) noexcept
{
    std::array<std::uint8_t, DomainSize - 1u + 4u + PayloadSize> bytes{};
    std::memcpy(bytes.data(), domain, DomainSize - 1u);
    auto* length = bytes.data() + DomainSize - 1u;
    length[0] = static_cast<std::uint8_t>(PayloadSize >> 24u);
    length[1] = static_cast<std::uint8_t>(PayloadSize >> 16u);
    length[2] = static_cast<std::uint8_t>(PayloadSize >> 8u);
    length[3] = static_cast<std::uint8_t>(PayloadSize);
    std::memcpy(length + 4u, payload, PayloadSize);
    return session::wire::sha256(bytes.data(), bytes.size());
}

} // namespace

DualStartIdentity canonical_dual_start_identity_v1() noexcept
{
    constexpr std::uint8_t core[] = {'1', '.', '5', '3', '.', '2'};
    std::uint8_t profile[session::dual::kDualCanonicalProfileBytesV1]{};
    std::uint8_t options[session::dual::kDualCanonicalOptionsBytesV1]{};
    session::dual::write_canonical_dual_profile_bytes_v1(profile);
    session::dual::write_canonical_dual_options_bytes_v1(options);
    return {identity_hash("flynes-dual-core-id-v1", core),
            identity_hash("flynes-dual-profile-id-v1", profile),
            identity_hash("flynes-dual-options-id-v1", options)};
}

} // namespace flynes::product
