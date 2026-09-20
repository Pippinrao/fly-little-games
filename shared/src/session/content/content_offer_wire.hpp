#ifndef FLYNES_SESSION_CONTENT_CONTENT_OFFER_WIRE_HPP
#define FLYNES_SESSION_CONTENT_CONTENT_OFFER_WIRE_HPP

/*
 * Task 11: ROM bulk-stream records. These are not schema ObjectKinds (the Rom
 * channel allow-list is still empty for app-frames); they are length-prefixed
 * records exclusive to QuicChannel::Rom.
 *
 *   u32be(body_len) || type u8 || body
 *
 * type 1 = offer (exactly 88 body bytes)
 * type 2 = chunk (28-byte header + payload)
 */

#include "content_offer_scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace flynes::session::content {

inline constexpr std::uint8_t kContentOfferTypeV1 = 1;
inline constexpr std::uint8_t kContentChunkTypeV1 = 2;
inline constexpr std::size_t kContentOfferBodyBytesV1 = 88;
inline constexpr std::size_t kContentChunkHeaderBytesV1 = 28;
inline constexpr std::size_t kContentChunkMaxPayloadV1 = 4096;

bool encode_content_offer_v1(const ContentOfferDeclV1& decl,
                             std::vector<std::uint8_t>* out) noexcept;
bool decode_content_offer_v1(const std::uint8_t* body, std::size_t size,
                             ContentOfferDeclV1* out) noexcept;
bool encode_content_chunk_v1(const std::array<std::uint8_t, 16>& offer_id,
                             std::uint32_t offset, const std::uint8_t* data,
                             std::size_t size,
                             std::vector<std::uint8_t>* out) noexcept;
bool decode_content_chunk_v1(const std::uint8_t* body, std::size_t size,
                             std::array<std::uint8_t, 16>* offer_id,
                             std::uint32_t* offset,
                             const std::uint8_t** data,
                             std::size_t* payload_size) noexcept;

} // namespace flynes::session::content

#endif
