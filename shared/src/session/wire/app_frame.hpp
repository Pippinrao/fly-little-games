#ifndef FLYNES_SESSION_WIRE_APP_FRAME_HPP
#define FLYNES_SESSION_WIRE_APP_FRAME_HPP

#include "session_codec.hpp"

#include <cstddef>
#include <cstdint>

// C2b — application-channel framing and type identification.
//
// WIRE SHAPE (owner-approved C2b design addendum, corrected 2026-09-11):
//
//   u32be(frame_length) || frame_type_tag u16be || exact_object_bytes
//   frame_length = 2 + len(exact_object_bytes)
//
// `frame_length` COUNTS the 2-byte tag, so the framing is the same shape as the
// repository's pre-bind record idiom (design spec:386
// `u32be(1 + body_length) || logical_type u8 || exact_body`) with that idiom's
// one security-relevant property preserved: a truncated or rewritten tag is a
// length error, never a silently re-classified object.
//
// `exact_object_bytes` is the canonical object encoding that
// session_codec::check() validates and hashes today, at offset 0. The frame
// prefix is OUTSIDE that validated-and-hashed window, so check() keeps its
// existing signature and every existing golden and validator stays valid.
//
// STREAMS vs DATAGRAMS:
//   * A reliable application stream carries ZERO OR MORE records back to back
//     until end of stream (design spec:439 mandates six records and spec:441
//     three on the bind streams). Use app_frame_cursor()/next_app_frame() and
//     loop until has_frame is false. A leftover of 1..3 bytes, or a
//     frame_length larger than the bytes remaining, is an error.
//   * A datagram carries EXACTLY ONE record; use parse_app_frame(). Any byte
//     after that record is an error (Status::Trailing).
//
// FRAME TYPE TAG (two namespaces, both derived from shared/schema):
//   * 0x0001..0xEFFF  ObjectKind namespace. The tag IS the schema's ObjectKind
//     value from the schema's `kinds` array; the codec type name is the
//     schema's hex spelling ("0x0210"), which is exactly what check()
//     dispatches on. The table carries every schema kind, so the 30 kinds that
//     have no body validator in this slice are identified by tag and then fail
//     closed with Status::UnknownKind from check().
//   * 0xFF00..0xFFFF  message namespace, tag = message_tag_base + index of the
//     entry in the schema's `messages` array. The six `messages` are explicitly
//     "Not an ObjectKind" (ChannelBindV1, ChannelBindProofV1,
//     CanonicalInputBundleV1, ChannelResumeSummaryV1, FrameCursorV1,
//     EvidenceCursorV1), so an ObjectKind-only type field could not carry the
//     pairing/bind objects at all. A u16 type field is required either way: the
//     schema declares 59 kinds in 0x0001..0x0308 and 45 of them exceed 255, so a
//     u8 field cannot express them, and the schema itself already encodes
//     `object_kind` as u16be (flynes_session_v1.schema:1201 and :1884).
//   * Any other tag value is rejected (fail closed).
//
// `message_tag_base` (0xFF00) is a C2b DESIGN ADDITION: the approved design
// does not assign the 0xFF00..0xFFFF range, and the range overlaps no schema
// value today. It is kept in one named constant so the design section 30 review
// can move it before any cross-platform integration depends on it.
//
// NOT A REPLACEMENT FOR spec:468's stream-kind prefix. spec:468's per-direction
// reliable-stream-kind prefix identifies a STREAM's family at open; this tag
// identifies each RECORD's object type. The two are complementary. Neither the
// prefix nor this framing is wired to a platform adapter yet, and the pre-app
// bind preambles (spec:439 "FNR1", spec:441 "FNB1") are untouched.
//
// SECURITY POSTURE:
//   * Identification is by the explicit tag only. Never guess a type by trying
//     validators until one accepts: a body can validate under more than one type
//     name (see the ChannelBindV1 / ChannelResumeSummaryV1 windows, both with an
//     empty hash domain), and trial validation would make the classification
//     attacker-chosen.
//   * `app_frame_hash` is a NEW, additional classification hash computed at this
//     layer on top of the object hash. It makes the classification (channel and
//     tag) tamper-evident when bound into a later transcript. It is NOT
//     authentication, and NOTHING in this file may reach the trusted evidence
//     seam: a validated-and-hashed envelope is not an authenticated one, and no
//     authenticated decoder exists in this slice.
//   * Per-channel allow-lists are the design section 11.4 channel table made
//     explicit. A tag that is not legal on the channel is rejected. The Input,
//     Video and Audio channels have no schema-defined objects yet and their
//     allow-lists are explicitly EMPTY, so they fail closed (addendum item 7).

namespace flynes::session::wire {

// ObjectKind namespace upper bound; 0x0000 is a schema `illegal_kind`.
constexpr std::uint16_t object_kind_tag_max = 0xEFFFu;

// Message namespace base (design addition; see the header note above).
constexpr std::uint16_t message_tag_base = 0xFF00u;

enum class FrameTypeNamespace : std::uint8_t
{
    ObjectKind = 0,
    Message = 1
};

struct FrameTagInfo
{
    FrameTypeNamespace type_namespace = FrameTypeNamespace::ObjectKind;
    const char* type_name = nullptr;
};

struct AppFrame
{
    // The u16 type field exactly as it appeared on the wire.
    std::uint16_t frame_type_tag = 0u;
    FrameTypeNamespace type_namespace = FrameTypeNamespace::ObjectKind;
    // The codec type name to pass to check(); never null on a successful parse.
    const char* type_name = nullptr;
    // Points into the caller's buffer: the exact object bytes, at offset 0 of
    // the validated-and-hashed window.
    const std::uint8_t* object_bytes = nullptr;
    std::size_t object_size = 0u;
    // The declared u32be frame length (2 + object_size).
    std::uint32_t frame_length = 0u;
    // Digest produced by check() over exactly [object_bytes, object_size).
    std::uint8_t object_hash[32] = {};
    // SHA256("flynes-app-frame-v1" || u8 channel || u16be tag || u32be body_len
    // || body). Filled by parse_app_frame()/next_app_frame(), which know the
    // channel; identify_app_frame() is channel-independent and therefore leaves
    // has_app_frame_hash false. See compute_app_frame_hash().
    std::uint8_t app_frame_hash[32] = {};
    bool has_app_frame_hash = false;
};

// Channel-independent framing plus identification plus check(). No channel
// allow-list and no block-derived classification hash are applied, because no
// channel is known. Used to keep the type tables honest against the golden
// corpus. A receiver MUST use parse_app_frame()/next_app_frame() instead.
Status identify_app_frame(const std::uint8_t* bytes, std::size_t size, AppFrame* out) noexcept;

// One complete record plus the channel allow-list. Extra bytes after the record
// are Status::Trailing (datagram semantics).
Status parse_app_frame(QuicChannel channel, const std::uint8_t* bytes, std::size_t size,
                       AppFrame* out) noexcept;

// Reliable-stream cursor over zero or more back-to-back records.
struct AppFrameCursor
{
    QuicChannel channel = QuicChannel::Control;
    const std::uint8_t* bytes = nullptr;
    std::size_t size = 0u;
    std::size_t offset = 0u;
};

AppFrameCursor app_frame_cursor(QuicChannel channel, const std::uint8_t* bytes,
                                std::size_t size) noexcept;

// Status::Ok with *has_frame false means the stream ended cleanly at a record
// boundary. On any other status the cursor does not advance, so the caller can
// keep the unparsed bytes.
Status next_app_frame(AppFrameCursor* cursor, AppFrame* out, bool* has_frame) noexcept;

// Build u32be(frame_length) || tag u16be || body into out. The tag must be in
// the type table, so every frame this function produces is identifiable.
Status encode_app_frame(std::uint16_t frame_type_tag, const std::uint8_t* object_bytes,
                        std::size_t object_size, std::uint8_t* out, std::size_t out_capacity,
                        std::size_t* written) noexcept;

// Type table lookup. Returns false for a tag in neither namespace.
bool frame_tag_info(std::uint16_t frame_type_tag, FrameTagInfo* out) noexcept;
// nullptr for a tag not in the type table.
const char* type_name_for_tag(std::uint16_t frame_type_tag) noexcept;

// Type table introspection, for the consistency guard.
std::size_t frame_tag_count() noexcept;
bool frame_tag_entry(std::size_t index, std::uint16_t* frame_type_tag,
                     FrameTypeNamespace* type_namespace, const char** type_name) noexcept;

// Per-channel allow-list (design section 11.4, hand-authored in app_frame.cpp).
bool tag_is_legal_on(QuicChannel channel, std::uint16_t frame_type_tag) noexcept;
bool type_name_is_legal_on(QuicChannel channel, const char* type_name) noexcept;
std::size_t allowed_type_count(QuicChannel channel) noexcept;

// Per-channel maximum object body size in bytes. Sources are documented in
// app_frame.cpp next to the table.
std::size_t max_object_bytes(QuicChannel channel) noexcept;
// Largest per-channel maximum; used when no channel is known yet.
constexpr std::size_t absolute_max_object_bytes() noexcept
{
    return 245760u;
}

// Classification tamper-evidence (C2b design addition, computed at this layer on
// top of — never instead of — the object hash produced by check()):
//
//   SHA256("flynes-app-frame-v1" || u8 channel || u16be tag || u32be body_len
//          || body)
//
// The domain string is concatenated literally and the body length is the
// explicit u32be field, so this is NOT domain_hash(): there is no additional
// domain-length prefix. A later transcript binding MUST use exactly this
// preimage; changing it remaps every classification hash.
//
// Why it exists: the object hash covers only the object bytes, and one body can
// validate under more than one type name (ChannelBindV1 and
// ChannelResumeSummaryV1 both hash a bare sha256 over the same window, so a
// 176-byte summary that also satisfies the bind checks hashes identically under
// both names). Binding channel and tag here makes the classification
// tamper-evident once a transcript covers this digest.
//
// This is NOT authentication: a validated frame is not an authenticated one, and
// nothing in this file may reach the trusted evidence seam.
void compute_app_frame_hash(QuicChannel channel, std::uint16_t frame_type_tag,
                            const std::uint8_t* object_bytes, std::size_t object_size,
                            std::uint8_t out[32]) noexcept;

} // namespace flynes::session::wire

#endif
