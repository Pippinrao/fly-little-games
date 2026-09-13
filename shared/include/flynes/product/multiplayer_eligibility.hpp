#pragma once

// Shared, versioned two-player capability projection (design 2026-09-13 §3.2).
// This is the ONLY source of two-player eligibility for the Game Center:
// platforms must never infer it from a filename, a "P2" in the title, or a
// controller count. The registry is filled from the shared, versioned game
// profile/catalog data; anything unread or version-mismatched projects as
// UNKNOWN, which is not UNSUPPORTED.

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace flynes::product {

enum class MultiplayerEligibility : std::uint8_t
{
    Unsupported = 0,
    Supported = 1,
    // Unread entry or profile version mismatch; excluded while the two-player
    // filter is on ("支持情况待确认"), visible when the filter is off.
    Unknown = 2,
};

class MultiplayerCapabilityRegistry
{
public:
    explicit MultiplayerCapabilityRegistry(std::uint32_t profile_version) noexcept;

    [[nodiscard]] std::uint32_t profile_version() const noexcept { return profile_version_; }

    // Records one game's eligibility together with the profile version that
    // produced it. Re-registering the same id replaces the earlier entry.
    void put(std::string canonical_id, MultiplayerEligibility eligibility,
             std::uint32_t profile_version)
    {
        entries_[std::move(canonical_id)] = Entry{eligibility, profile_version};
    }

    // Unread ids and entries whose profile version does not match the
    // registry's version project UNKNOWN, never UNSUPPORTED.
    [[nodiscard]] MultiplayerEligibility eligibility_for(std::string_view canonical_id) const;

private:
    struct Entry
    {
        MultiplayerEligibility eligibility;
        std::uint32_t profile_version;
    };

    std::uint32_t profile_version_;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace flynes::product
