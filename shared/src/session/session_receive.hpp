#ifndef FLYNES_SESSION_SESSION_RECEIVE_HPP
#define FLYNES_SESSION_SESSION_RECEIVE_HPP

#include <flynes/flynes_session.h>

#include "wire/app_frame.hpp"

#include <cstddef>
#include <cstdint>

namespace flynes::session {

// INTERNAL C++ seam, not a deployed authentication path and deliberately NOT
// part of the public C ABI (flynes_session.h is unchanged).
//
// fly_session_receive_stream / fly_session_receive_datagram must keep returning
// FLY_RESULT_INVALID_STATE: a validated-and-hashed envelope is NOT
// authentication (design spec:713) and no authenticated decoder exists in this
// slice (design section 12.6). What the C2b framing layer adds here is
// IDENTIFICATION only: which object type the peer claimed, and its
// classification hash. Nothing about a received byte reaches the trusted
// evidence seam (InitialPlanLock / VerifiedPairEvidence / VerifiedPlanEvidence),
// and nothing consumes this summary yet.
//
// The recorded summary is diagnostic; the receive entry points discard the
// return value of the record_* calls on purpose.
struct ReceivedFrameSummary
{
    bool identified = false;
    std::uint32_t channel = 0u;
    std::uint16_t frame_type_tag = 0u;
    // Static storage from the frame type table; valid for the program lifetime.
    const char* type_name = nullptr;
    std::size_t object_size = 0u;
    bool has_app_frame_hash = false;
    std::uint8_t app_frame_hash[32] = {};
};

// Replaces the stored summary with the identification result for this chunk.
// A reliable stream carries zero or more records; without stream reassembly in
// this slice only the first complete record at the front of the chunk is
// identified, and a partial trailing record is ignored rather than buffered.
// Returns true when a complete, channel-legal, validator-accepted record was
// identified.
bool record_received_stream(fly_session_t* session, std::uint32_t channel,
                            const std::uint8_t* bytes, std::size_t size) noexcept;

// Datagram semantics: exactly one record. Extra bytes after the record, a
// partial record, an unknown tag or a tag that is not legal on the channel are
// all recorded as "not identified".
bool record_received_datagram(fly_session_t* session, std::uint32_t channel,
                              const std::uint8_t* bytes, std::size_t size) noexcept;

// Never null-safe failure: a null handle reads as a fresh (unidentified)
// summary.
ReceivedFrameSummary last_received_frame(const fly_session_t* session) noexcept;

} // namespace flynes::session

#endif
