#include "app_frame.hpp"
#include "sha256.hpp"

#include <cstring>
#include <vector>

namespace flynes::session::wire {
namespace {

std::uint32_t be32(const std::uint8_t* p) noexcept
{
    return (static_cast<std::uint32_t>(p[0]) << 24u) | (static_cast<std::uint32_t>(p[1]) << 16u) |
           (static_cast<std::uint32_t>(p[2]) << 8u) | static_cast<std::uint32_t>(p[3]);
}

std::uint16_t be16(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8u) | p[1]);
}

void put_be32(std::uint8_t* p, std::uint32_t value) noexcept
{
    p[0] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
    p[1] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    p[3] = static_cast<std::uint8_t>(value & 0xFFu);
}

// ---------------------------------------------------------------------------
// Frame type table — hand-authored, one row per schema type. The consistency
// guard (tests/test_app_frame_consistency.cpp) asserts this table against
// shared/schema/flynes_session_v1.schema: the ObjectKind rows must equal the
// schema `kinds` array exactly, and the Message rows must equal the schema
// `messages` array order exactly.
//
// ObjectKind rows carry every one of the 61 schema kinds. The 30 kinds that have
// no body validator in this slice are identified here and then fail closed in
// check() with Status::UnknownKind; they are deliberately NOT dropped, so a
// future validator only has to be added in session_codec.cpp.
//
// The codec type name is the schema's hex spelling because that is exactly what
// session_codec::check() dispatches on for ObjectKind-tagged objects. Message
// rows carry the message's schema name.
// ---------------------------------------------------------------------------
struct FrameTagEntry
{
    std::uint16_t tag;
    FrameTypeNamespace type_namespace;
    const char* type_name;
};

constexpr FrameTagEntry kFrameTags[] = {
    {0x0001u, FrameTypeNamespace::ObjectKind, "0x0001"},
    {0x0002u, FrameTypeNamespace::ObjectKind, "0x0002"},
    {0x0003u, FrameTypeNamespace::ObjectKind, "0x0003"},
    {0x0004u, FrameTypeNamespace::ObjectKind, "0x0004"},
    {0x0005u, FrameTypeNamespace::ObjectKind, "0x0005"},
    {0x0006u, FrameTypeNamespace::ObjectKind, "0x0006"},
    {0x0007u, FrameTypeNamespace::ObjectKind, "0x0007"},
    {0x0008u, FrameTypeNamespace::ObjectKind, "0x0008"},
    {0x0009u, FrameTypeNamespace::ObjectKind, "0x0009"},
    {0x000Au, FrameTypeNamespace::ObjectKind, "0x000a"},
    {0x000Bu, FrameTypeNamespace::ObjectKind, "0x000b"},
    {0x000Cu, FrameTypeNamespace::ObjectKind, "0x000c"},
    {0x000Du, FrameTypeNamespace::ObjectKind, "0x000d"},
    {0x000Eu, FrameTypeNamespace::ObjectKind, "0x000e"},
    {0x0101u, FrameTypeNamespace::ObjectKind, "0x0101"},
    {0x0102u, FrameTypeNamespace::ObjectKind, "0x0102"},
    {0x0103u, FrameTypeNamespace::ObjectKind, "0x0103"},
    {0x0104u, FrameTypeNamespace::ObjectKind, "0x0104"},
    {0x0105u, FrameTypeNamespace::ObjectKind, "0x0105"},
    {0x0106u, FrameTypeNamespace::ObjectKind, "0x0106"},
    {0x0107u, FrameTypeNamespace::ObjectKind, "0x0107"},
    {0x0108u, FrameTypeNamespace::ObjectKind, "0x0108"},
    {0x0109u, FrameTypeNamespace::ObjectKind, "0x0109"},
    {0x010Au, FrameTypeNamespace::ObjectKind, "0x010a"},
    {0x010Bu, FrameTypeNamespace::ObjectKind, "0x010b"},
    {0x010Cu, FrameTypeNamespace::ObjectKind, "0x010c"},
    {0x010Du, FrameTypeNamespace::ObjectKind, "0x010d"},
    {0x010Eu, FrameTypeNamespace::ObjectKind, "0x010e"},
    {0x010Fu, FrameTypeNamespace::ObjectKind, "0x010f"},
    {0x0110u, FrameTypeNamespace::ObjectKind, "0x0110"},
    {0x0111u, FrameTypeNamespace::ObjectKind, "0x0111"},
    {0x0112u, FrameTypeNamespace::ObjectKind, "0x0112"},
    {0x0201u, FrameTypeNamespace::ObjectKind, "0x0201"},
    {0x0202u, FrameTypeNamespace::ObjectKind, "0x0202"},
    {0x0203u, FrameTypeNamespace::ObjectKind, "0x0203"},
    {0x0204u, FrameTypeNamespace::ObjectKind, "0x0204"},
    {0x0205u, FrameTypeNamespace::ObjectKind, "0x0205"},
    {0x0206u, FrameTypeNamespace::ObjectKind, "0x0206"},
    {0x0207u, FrameTypeNamespace::ObjectKind, "0x0207"},
    {0x0208u, FrameTypeNamespace::ObjectKind, "0x0208"},
    {0x0209u, FrameTypeNamespace::ObjectKind, "0x0209"},
    {0x020Au, FrameTypeNamespace::ObjectKind, "0x020a"},
    {0x020Bu, FrameTypeNamespace::ObjectKind, "0x020b"},
    {0x020Cu, FrameTypeNamespace::ObjectKind, "0x020c"},
    {0x020Du, FrameTypeNamespace::ObjectKind, "0x020d"},
    {0x020Eu, FrameTypeNamespace::ObjectKind, "0x020e"},
    {0x020Fu, FrameTypeNamespace::ObjectKind, "0x020f"},
    {0x0210u, FrameTypeNamespace::ObjectKind, "0x0210"},
    {0x0211u, FrameTypeNamespace::ObjectKind, "0x0211"},
    {0x0212u, FrameTypeNamespace::ObjectKind, "0x0212"},
    {0x0213u, FrameTypeNamespace::ObjectKind, "0x0213"},
    {0x0216u, FrameTypeNamespace::ObjectKind, "0x0216"},
    {0x0217u, FrameTypeNamespace::ObjectKind, "0x0217"},
    {0x0301u, FrameTypeNamespace::ObjectKind, "0x0301"},
    {0x0302u, FrameTypeNamespace::ObjectKind, "0x0302"},
    {0x0303u, FrameTypeNamespace::ObjectKind, "0x0303"},
    {0x0304u, FrameTypeNamespace::ObjectKind, "0x0304"},
    {0x0305u, FrameTypeNamespace::ObjectKind, "0x0305"},
    {0x0306u, FrameTypeNamespace::ObjectKind, "0x0306"},
    {0x0307u, FrameTypeNamespace::ObjectKind, "0x0307"},
    {0x0308u, FrameTypeNamespace::ObjectKind, "0x0308"},
    // Message namespace: tag = message_tag_base + index in the schema `messages`
    // array. The order below must stay in schema order.
    {0xFF00u, FrameTypeNamespace::Message, "FrameCursorV1"},
    {0xFF01u, FrameTypeNamespace::Message, "EvidenceCursorV1"},
    {0xFF02u, FrameTypeNamespace::Message, "CanonicalInputBundleV1"},
    {0xFF03u, FrameTypeNamespace::Message, "ChannelBindV1"},
    {0xFF04u, FrameTypeNamespace::Message, "ChannelBindProofV1"},
    {0xFF05u, FrameTypeNamespace::Message, "ChannelResumeSummaryV1"},
    {0xFF06u, FrameTypeNamespace::Message, "LinkHelloV1"},
    {0xFF07u, FrameTypeNamespace::Message, "LinkReadyV1"},
};

constexpr std::size_t kFrameTagCount = sizeof(kFrameTags) / sizeof(kFrameTags[0]);

// ---------------------------------------------------------------------------
// Per-channel allow-list — hand-authored from design section 11.4. There is no
// schema source for per-kind channel membership (the schema's `quic_channels` is
// only {id, name, form} and no kind carries a channel field), so this table IS
// the design's channel table made explicit; a schema addition to carry channel
// membership would replace it.
//
// The empty lists are deliberate and explicit: a channel whose object families
// do not exist in the schema yet must fail closed for EVERY tag (addendum
// item 7), not fall through to a default.
//
// Entries are restricted to tags with a frozen encoding (validator plus golden),
// so "legal on this channel" always implies "decodable by this build". The
// design's other section 11.4 families (checkpoint envelope, digest,
// INPUT_REPAIR/INPUT_SEAL, watermarks, ROM offer/chunks, and every Input, Video
// and Audio object) have no frozen wire object yet and therefore no entry.
// ---------------------------------------------------------------------------
const char* const kControlTypes[] = {
    "0x0210", // SuspendIntentV1 — design:458 lists pause/resume on Control.
    "0x0212", // SessionSigningKeyBindingV1 — spec:468 makes it mandatory in the
              // first app-level reliable message (HELLO) on Control.
    "0x0216", // LinkHelloV1 — the link control plane's own object; the spec makes
              // HELLO the first reliable Control message.
    "0x0217", // LinkReadyV1 — READY/ACK closure on the same Control channel.
};

const char* const kStateCommitTypes[] = {
    "0x0112", // OpenInputReservationSetV1 — input reservation bookkeeping (spec:460).
    "0x0202", // InputSequenceLedgerV1 — design:460 continuous watermark/ledger.
    "0x0203", // InputSequenceReservationV1 — design:460 INPUT_SEQUENCE_RESERVE.
    "0x020A", // InputReservationGrantV1 — design:460 INPUT_SEQUENCE_GRANT.
    "0x020B", // InputReservationAckV1 — design:460 INPUT_SEQUENCE_ACK.
    "0x020C", // InputReservationFinalizedV1 — reservation closure.
    "0x020D", // InputReservationRequestV1 — design:460 INPUT_SEQUENCE_RESERVE.
    "0x020E", // PrimeAuthorizationV1 — input priming on the input log.
    "0x020F", // EpochInputCloseCertificateV1 — design:460 INPUT_SEAL/watermark closure.
    "0x0211", // InputRangeAuthorizationV1 — input sequence authorization.
    "CanonicalInputBundleV1", // Addendum item 6: legal on State Commit only.
};

const char* const kBulkTypes[] = {
    "0x000b", // TransitionRgba8888FrameV1 — design:461 Bulk State carries a
              // transition package component; spec:492 fixes 256x240x4 bytes.
};

struct ChannelAllowList
{
    QuicChannel channel;
    const char* const* type_names;
    std::size_t count;
};

constexpr ChannelAllowList kChannelAllowLists[] = {
    {QuicChannel::Control, kControlTypes, sizeof(kControlTypes) / sizeof(kControlTypes[0])},
    // Input, Video and Audio carry only prose families (INPUT_SAMPLE,
    // CANONICAL_HINT, FRAME_BEACON, Video access units, AUDIO_DATA/AUDIO_XOR_FEC)
    // with no schema object and no golden: EMPTY, fails closed.
    {QuicChannel::Input, nullptr, 0u},
    {QuicChannel::StateCommit, kStateCommitTypes, sizeof(kStateCommitTypes) / sizeof(kStateCommitTypes[0])},
    {QuicChannel::Bulk, kBulkTypes, sizeof(kBulkTypes) / sizeof(kBulkTypes[0])},
    // ROM offer/chunk objects exist only in spec prose: EMPTY, fails closed.
    {QuicChannel::Rom, nullptr, 0u},
    {QuicChannel::Video, nullptr, 0u},
    {QuicChannel::Audio, nullptr, 0u},
};

constexpr std::size_t kChannelAllowListCount =
    sizeof(kChannelAllowLists) / sizeof(kChannelAllowLists[0]);

// ---------------------------------------------------------------------------
// Per-channel maximum object body size. Every number is either a frozen design
// bound or a documented conservative constant; none of them is new protocol
// semantics.
//
//   Control     65536  design spec:490 "Control single message <= 64 KiB".
//   Input        1200  design:495 binds the datagram objects to the negotiated
//                      QUIC max_datagram_payload, which is not frozen. 1200 is
//                      the smallest datagram every conforming QUIC peer must
//                      accept (RFC 9000 section 14), so it is the conservative
//                      bound until HOST_STREAM negotiates a larger value. The
//                      Input allow-list is empty today, so this bound can only
//                      reject.
//   StateCommit 65536  the design gives no explicit State Commit bound; the
//                      largest max_decode_length among the State Commit tags
//                      that have a frozen encoding is 65536 (END_PACKAGE_V1,
//                      schema kind 0x0306), and design spec:492 caps a
//                      TransitionPackage manifest at 64 KiB.
//   Bulk       245760  schema max_decode_length of TRANSITION_RGBA8888_FRAME_V1
//                      (0x000b), the only Bulk tag with a frozen encoding;
//                      equals 256*240*4 from spec:492. Design spec:492's 12 MiB
//                      bound is on all uncompressed components together, not on
//                      one framed record.
//   Rom         65536  the design gives no framed-record bound for ROM (spec:493
//                      bounds the decompressed payload at 8 MiB); conservative
//                      constant. The ROM allow-list is empty, so this can only
//                      reject.
//   Video        1200  as Input.
//   Audio        1200  design spec:495 requires the AUDIO_DATA and AUDIO_XOR_FEC
//                      datagrams to fit the negotiated max_datagram_payload
//                      (960-byte PCM block plus header); 1200 is the conservative
//                      RFC 9000 floor. The Audio allow-list is empty.
// ---------------------------------------------------------------------------
constexpr std::size_t kMaxObjectBytes[] = {
    0u,     // index 0: no channel
    65536u, // Control = 1
    1200u,  // Input = 2
    65536u, // StateCommit = 3
    245760u,// Bulk = 4
    65536u, // Rom = 5
    1200u,  // Video = 6
    1200u,  // Audio = 7
};

constexpr std::size_t kMaxObjectBytesCount = sizeof(kMaxObjectBytes) / sizeof(kMaxObjectBytes[0]);

static_assert(kMaxObjectBytesCount == kChannelAllowListCount + 1u,
              "one maximum per QUIC application channel");
static_assert(kFrameTags[0].tag == 0x0001u, "the type table starts at 0x0001");
static_assert(kFrameTags[61].tag == message_tag_base, "message rows start at message_tag_base");
static_assert(kFrameTagCount == 69u, "61 schema kinds plus 8 schema messages");

const ChannelAllowList* allow_list_for(QuicChannel channel) noexcept
{
    for (const ChannelAllowList& entry : kChannelAllowLists)
    {
        if (entry.channel == channel)
            return &entry;
    }
    return nullptr;
}

bool allow_list_has_type(const ChannelAllowList* list, const char* type_name) noexcept
{
    if (list == nullptr || type_name == nullptr)
        return false;
    for (std::size_t i = 0; i < list->count; ++i)
    {
        if (list->type_names[i] != nullptr && std::strcmp(list->type_names[i], type_name) == 0)
            return true;
    }
    return false;
}

// One record at the front of [bytes, available). `context` is null when no
// channel is known: then the channel-independent maximum applies, no allow-list
// is enforced, and no channel-bound classification hash can be produced.
struct ChannelContext
{
    QuicChannel channel;
    std::size_t max_body;
    const ChannelAllowList* allow;
};

Status decode_record(const std::uint8_t* bytes, std::size_t available,
                     const ChannelContext* context, std::size_t* consumed, AppFrame* out) noexcept
{
    const std::size_t max_body =
        context == nullptr ? absolute_max_object_bytes() : context->max_body;
    const ChannelAllowList* allow = context == nullptr ? nullptr : context->allow;
    if (available < 4u)
        return Status::Truncated;
    const std::uint32_t declared = be32(bytes);
    // frame_length counts the 2-byte tag, so anything below 2 is impossible.
    if (declared < 2u)
        return Status::BadLength;
    const std::uint64_t body_size = static_cast<std::uint64_t>(declared) - 2u;
    if (body_size > static_cast<std::uint64_t>(max_body))
        return Status::BadLength;
    // Checked arithmetic: 4 + declared cannot overflow a 64-bit comparison.
    const std::uint64_t total = 4u + static_cast<std::uint64_t>(declared);
    if (total > static_cast<std::uint64_t>(available))
        return Status::Truncated;

    const std::uint16_t tag = be16(bytes + 4u);
    FrameTagInfo info{};
    if (!frame_tag_info(tag, &info))
        return Status::UnknownKind;
    if (allow != nullptr && !allow_list_has_type(allow, info.type_name))
        return Status::InvalidField;

    std::uint8_t hash[32] = {};
    const Status status = check(info.type_name, bytes + 6u, static_cast<std::size_t>(body_size), hash);
    if (status != Status::Ok)
        return status;

    if (out != nullptr)
    {
        *out = AppFrame{};
        out->frame_type_tag = tag;
        out->type_namespace = info.type_namespace;
        out->type_name = info.type_name;
        out->object_bytes = bytes + 6u;
        out->object_size = static_cast<std::size_t>(body_size);
        out->frame_length = declared;
        std::memcpy(out->object_hash, hash, 32u);
        if (context != nullptr)
        {
            compute_app_frame_hash(context->channel, tag, bytes + 6u,
                                   static_cast<std::size_t>(body_size), out->app_frame_hash);
            out->has_app_frame_hash = true;
        }
    }
    if (consumed != nullptr)
        *consumed = static_cast<std::size_t>(total);
    return Status::Ok;
}

} // namespace

bool frame_tag_info(std::uint16_t frame_type_tag, FrameTagInfo* out) noexcept
{
    for (const FrameTagEntry& entry : kFrameTags)
    {
        if (entry.tag == frame_type_tag)
        {
            if (out != nullptr)
            {
                out->type_namespace = entry.type_namespace;
                out->type_name = entry.type_name;
            }
            return true;
        }
    }
    return false;
}

const char* type_name_for_tag(std::uint16_t frame_type_tag) noexcept
{
    FrameTagInfo info{};
    return frame_tag_info(frame_type_tag, &info) ? info.type_name : nullptr;
}

std::size_t frame_tag_count() noexcept
{
    return kFrameTagCount;
}

bool frame_tag_entry(std::size_t index, std::uint16_t* frame_type_tag,
                     FrameTypeNamespace* type_namespace, const char** type_name) noexcept
{
    if (index >= kFrameTagCount)
        return false;
    if (frame_type_tag != nullptr)
        *frame_type_tag = kFrameTags[index].tag;
    if (type_namespace != nullptr)
        *type_namespace = kFrameTags[index].type_namespace;
    if (type_name != nullptr)
        *type_name = kFrameTags[index].type_name;
    return true;
}

bool tag_is_legal_on(QuicChannel channel, std::uint16_t frame_type_tag) noexcept
{
    return allow_list_has_type(allow_list_for(channel), type_name_for_tag(frame_type_tag));
}

bool type_name_is_legal_on(QuicChannel channel, const char* type_name) noexcept
{
    return allow_list_has_type(allow_list_for(channel), type_name);
}

std::size_t allowed_type_count(QuicChannel channel) noexcept
{
    const ChannelAllowList* list = allow_list_for(channel);
    return list == nullptr ? 0u : list->count;
}

std::size_t max_object_bytes(QuicChannel channel) noexcept
{
    const auto index = static_cast<std::size_t>(channel);
    if (index >= kMaxObjectBytesCount)
        return 0u;
    return kMaxObjectBytes[index];
}

Status identify_app_frame(const std::uint8_t* bytes, std::size_t size, AppFrame* out) noexcept
{
    if ((bytes == nullptr && size != 0u) || out == nullptr)
        return Status::InvalidField;
    std::size_t consumed = 0u;
    const Status status = decode_record(bytes, size, nullptr, &consumed, out);
    if (status != Status::Ok)
        return status;
    // Channel-independent identification still describes exactly one record.
    if (consumed != size)
        return Status::Trailing;
    return Status::Ok;
}

Status parse_app_frame(QuicChannel channel, const std::uint8_t* bytes, std::size_t size,
                       AppFrame* out) noexcept
{
    if ((bytes == nullptr && size != 0u) || out == nullptr)
        return Status::InvalidField;
    const ChannelContext context{channel, max_object_bytes(channel), allow_list_for(channel)};
    std::size_t consumed = 0u;
    const Status status = decode_record(bytes, size, &context, &consumed, out);
    if (status != Status::Ok)
        return status;
    // A datagram carries exactly one record.
    if (consumed != size)
        return Status::Trailing;
    return Status::Ok;
}

AppFrameCursor app_frame_cursor(QuicChannel channel, const std::uint8_t* bytes,
                                std::size_t size) noexcept
{
    AppFrameCursor cursor{};
    cursor.channel = channel;
    cursor.bytes = bytes;
    cursor.size = size;
    return cursor;
}

Status next_app_frame(AppFrameCursor* cursor, AppFrame* out, bool* has_frame) noexcept
{
    if (cursor == nullptr || out == nullptr || has_frame == nullptr)
        return Status::InvalidField;
    *has_frame = false;
    if (cursor->offset > cursor->size)
        return Status::Truncated;
    if (cursor->offset == cursor->size)
        return Status::Ok; // zero or more records: a clean end of stream
    const ChannelContext context{cursor->channel, max_object_bytes(cursor->channel),
                                 allow_list_for(cursor->channel)};
    std::size_t consumed = 0u;
    const Status status =
        decode_record(cursor->bytes + cursor->offset, cursor->size - cursor->offset, &context,
                      &consumed, out);
    if (status != Status::Ok)
        return status; // the cursor does not advance on failure
    cursor->offset += consumed;
    *has_frame = true;
    return Status::Ok;
}

Status encode_app_frame(std::uint16_t frame_type_tag, const std::uint8_t* object_bytes,
                        std::size_t object_size, std::uint8_t* out, std::size_t out_capacity,
                        std::size_t* written) noexcept
{
    if (out == nullptr || written == nullptr)
        return Status::InvalidField;
    if (object_bytes == nullptr && object_size != 0u)
        return Status::InvalidField;
    if (type_name_for_tag(frame_type_tag) == nullptr)
        return Status::UnknownKind; // never emit a record the parser cannot identify
    if (object_size > absolute_max_object_bytes())
        return Status::BadLength;
    const std::size_t total = 6u + object_size;
    if (out_capacity < total)
        return Status::Truncated;
    put_be32(out, static_cast<std::uint32_t>(2u + object_size));
    out[4] = static_cast<std::uint8_t>(frame_type_tag >> 8u);
    out[5] = static_cast<std::uint8_t>(frame_type_tag & 0xFFu);
    if (object_size != 0u)
        std::memcpy(out + 6u, object_bytes, object_size);
    *written = total;
    return Status::Ok;
}

void compute_app_frame_hash(QuicChannel channel, std::uint16_t frame_type_tag,
                            const std::uint8_t* object_bytes, std::size_t object_size,
                            std::uint8_t out[32]) noexcept
{
    if (out == nullptr)
        return;
    if (object_bytes == nullptr && object_size != 0u)
    {
        std::memset(out, 0, 32u);
        return;
    }
    // Preimage: domain || u8 channel || u16be tag || u32be body_len || body.
    // Deliberately not domain_hash(): its extra u32be(payload length) prefix is
    // redundant here and would change the documented classification preimage.
    static const char kDomain[] = "flynes-app-frame-v1";
    constexpr std::size_t kDomainSize = sizeof(kDomain) - 1u;
    std::vector<std::uint8_t> preimage;
    preimage.reserve(kDomainSize + 7u + object_size);
    preimage.insert(preimage.end(), kDomain, kDomain + kDomainSize);
    preimage.push_back(static_cast<std::uint8_t>(channel));
    preimage.push_back(static_cast<std::uint8_t>((frame_type_tag >> 8u) & 0xFFu));
    preimage.push_back(static_cast<std::uint8_t>(frame_type_tag & 0xFFu));
    preimage.push_back(static_cast<std::uint8_t>((object_size >> 24u) & 0xFFu));
    preimage.push_back(static_cast<std::uint8_t>((object_size >> 16u) & 0xFFu));
    preimage.push_back(static_cast<std::uint8_t>((object_size >> 8u) & 0xFFu));
    preimage.push_back(static_cast<std::uint8_t>(object_size & 0xFFu));
    if (object_size != 0u)
        preimage.insert(preimage.end(), object_bytes, object_bytes + object_size);
    const auto digest = sha256(preimage.data(), preimage.size());
    std::memcpy(out, digest.data(), 32u);
}

} // namespace flynes::session::wire
