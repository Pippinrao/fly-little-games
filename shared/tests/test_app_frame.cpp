// C2b — application-channel framing (shared layer).
//
// Unit tests for the framing/bounds/frame-type-tag/per-channel-allow-list layer.
// The schema and golden-corpus agreement guards (kind table == schema kinds,
// message tags == schema messages order, every golden round-trips) live in
// test_app_frame_consistency.cpp.
//
// Framing under test (owner-approved C2b addendum, corrected 2026-09-11):
//   u32be(frame_length) || frame_type_tag u16be || exact_object_bytes
//   frame_length = 2 + len(exact_object_bytes)
// The type field is a 16-bit frame type tag with two ranges:
//   0x0001..0xEFFF  ObjectKind namespace (the schema's ObjectKind value)
//   0xFF00..0xFFFF  message namespace (0xFF00 + index in schema `messages`)
// A reliable stream carries zero or more records back to back; a datagram
// carries exactly one.

#include "app_frame.hpp"
#include "sha256.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using flynes::session::wire::AppFrame;
using flynes::session::wire::AppFrameCursor;
using flynes::session::wire::FrameTagInfo;
using flynes::session::wire::FrameTypeNamespace;
using flynes::session::wire::QuicChannel;
using flynes::session::wire::Status;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string status_name(Status status)
{
    switch (status)
    {
    case Status::Ok:
        return "ok";
    case Status::Truncated:
        return "truncated";
    case Status::Trailing:
        return "trailing";
    case Status::BadLength:
        return "bad_length";
    case Status::UnknownKind:
        return "unknown_tag";
    case Status::InvalidField:
        return "invalid_field";
    default:
        return "other";
    }
}

void expect_status(Status got, Status wanted, const std::string& what)
{
    expect(got == wanted, what + ": expected " + status_name(wanted) + " got " + status_name(got));
}

bool read_all(const fs::path& path, std::vector<std::uint8_t>* out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (end < 0)
        return false;
    in.seekg(0, std::ios::beg);
    out->assign(static_cast<std::size_t>(end), 0);
    if (end != 0 && !in.read(reinterpret_cast<char*>(out->data()), end))
        return false;
    return true;
}

std::string read_text(const fs::path& path)
{
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();
    return line;
}

std::string hex_of(const std::uint8_t* bytes, std::size_t size)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(size * 2u, '0');
    for (std::size_t i = 0; i < size; ++i)
    {
        out[i * 2u] = digits[bytes[i] >> 4u];
        out[i * 2u + 1u] = digits[bytes[i] & 0x0Fu];
    }
    return out;
}

fs::path golden_dir(const char* name)
{
    return fs::path(FLYNES_SESSION_GOLDEN_DIR) / name;
}

std::vector<std::uint8_t> golden_body(const char* name)
{
    std::vector<std::uint8_t> bytes;
    expect(read_all(golden_dir(name) / "legal.bin", &bytes),
           std::string("golden ") + name + " legal.bin readable");
    return bytes;
}

// Independent re-implementation of the documented classification preimage:
//   SHA256("flynes-app-frame-v1" || u8 channel || u16be tag || u32be body_len || body)
std::array<std::uint8_t, 32> expected_app_frame_hash(std::uint8_t channel,
                                                     std::uint16_t tag,
                                                     const std::uint8_t* body,
                                                     std::size_t size)
{
    static const char domain[] = "flynes-app-frame-v1";
    std::vector<std::uint8_t> preimage;
    preimage.insert(preimage.end(), domain, domain + (sizeof(domain) - 1u));
    preimage.push_back(channel);
    preimage.push_back(static_cast<std::uint8_t>(tag >> 8u));
    preimage.push_back(static_cast<std::uint8_t>(tag & 0xFFu));
    preimage.push_back(static_cast<std::uint8_t>((size >> 24u) & 0xFFu));
    preimage.push_back(static_cast<std::uint8_t>((size >> 16u) & 0xFFu));
    preimage.push_back(static_cast<std::uint8_t>((size >> 8u) & 0xFFu));
    preimage.push_back(static_cast<std::uint8_t>(size & 0xFFu));
    preimage.insert(preimage.end(), body, body + size);
    return flynes::session::wire::sha256(preimage.data(), preimage.size());
}

std::vector<std::uint8_t> frame_of(std::uint16_t tag, const std::vector<std::uint8_t>& body)
{
    std::vector<std::uint8_t> out(6u + body.size(), 0u);
    std::size_t written = 0u;
    const Status status = flynes::session::wire::encode_app_frame(
        tag, body.empty() ? nullptr : body.data(), body.size(), out.data(), out.size(), &written);
    expect_status(status, Status::Ok, "encode_app_frame");
    expect(written == out.size(), "encode_app_frame wrote the exact frame size");
    return out;
}

// Hand-built frame for tags the encoder refuses (framing is tag-agnostic; only
// the encoder gates on the tag table).
std::vector<std::uint8_t> raw_frame_of(std::uint16_t tag, const std::vector<std::uint8_t>& body)
{
    std::vector<std::uint8_t> out(6u + body.size(), 0u);
    const std::uint32_t declared = static_cast<std::uint32_t>(2u + body.size());
    out[0] = static_cast<std::uint8_t>((declared >> 24u) & 0xFFu);
    out[1] = static_cast<std::uint8_t>((declared >> 16u) & 0xFFu);
    out[2] = static_cast<std::uint8_t>((declared >> 8u) & 0xFFu);
    out[3] = static_cast<std::uint8_t>(declared & 0xFFu);
    out[4] = static_cast<std::uint8_t>(tag >> 8u);
    out[5] = static_cast<std::uint8_t>(tag & 0xFFu);
    std::memcpy(out.data() + 6u, body.data(), body.size());
    return out;
}

std::uint32_t read_be32(const std::uint8_t* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24u) | (static_cast<std::uint32_t>(p[1]) << 16u) |
           (static_cast<std::uint32_t>(p[2]) << 8u) | static_cast<std::uint32_t>(p[3]);
}

void write_be32(std::uint8_t* p, std::uint32_t value)
{
    p[0] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
    p[1] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    p[3] = static_cast<std::uint8_t>(value & 0xFFu);
}

const char* channel_name(QuicChannel channel)
{
    switch (channel)
    {
    case QuicChannel::Control:
        return "Control";
    case QuicChannel::Input:
        return "Input";
    case QuicChannel::StateCommit:
        return "StateCommit";
    case QuicChannel::Bulk:
        return "Bulk";
    case QuicChannel::Rom:
        return "Rom";
    case QuicChannel::Video:
        return "Video";
    case QuicChannel::Audio:
        return "Audio";
    }
    return "?";
}

constexpr QuicChannel kAllChannels[] = {
    QuicChannel::Control, QuicChannel::Input, QuicChannel::StateCommit, QuicChannel::Bulk,
    QuicChannel::Rom,     QuicChannel::Video, QuicChannel::Audio};

// ---------------------------------------------------------------------------
// 1. Framing layout, round trip, and the position of the validated window.
// ---------------------------------------------------------------------------
void frame_layout_and_round_trip()
{
    const std::uint16_t tag = 0x0210u; // SUSPEND_INTENT_V1, 240 bytes
    const std::vector<std::uint8_t> body = golden_body("suspend_intent_v1");
    expect(body.size() == 240u, "suspend_intent_v1 is 240 bytes");
    const std::vector<std::uint8_t> frame = frame_of(tag, body);

    expect(frame.size() == 246u, "framed record is 6 + body bytes");
    expect(read_be32(frame.data()) == 242u, "frame_length is 2 + body length (counts the tag)");
    expect(frame[4] == 0x02u && frame[5] == 0x10u, "tag is u16be at offset 4");
    expect(std::memcmp(frame.data() + 6u, body.data(), body.size()) == 0,
           "exact object bytes start at offset 6");

    AppFrame parsed{};
    expect_status(flynes::session::wire::parse_app_frame(QuicChannel::Control, frame.data(),
                                                         frame.size(), &parsed),
                  Status::Ok, "parse on Control");
    expect(parsed.frame_type_tag == tag, "parsed tag");
    expect(parsed.type_namespace == FrameTypeNamespace::ObjectKind, "parsed namespace");
    expect(std::strcmp(parsed.type_name, "0x0210") == 0, "parsed codec type name");
    expect(parsed.object_bytes == frame.data() + 6u, "body pointer is inside the caller buffer");
    expect(parsed.object_size == body.size(), "body size");
    expect(parsed.frame_length == 242u, "declared frame length");
    expect(parsed.has_app_frame_hash, "channel-bound parse fills the classification hash");

    // The validated-and-hashed window is exactly the object bytes: the frame
    // prefix is outside it, so check() is called with body at offset 0.
    std::uint8_t direct[32]{};
    expect_status(flynes::session::wire::check("0x0210", body.data(), body.size(), direct),
                  Status::Ok, "check on the unwrapped body");
    expect(std::memcmp(parsed.object_hash, direct, 32u) == 0,
           "object hash equals the hash of the naked body");
    expect(hex_of(parsed.object_hash, 32u) == read_text(golden_dir("suspend_intent_v1") / "legal.hash"),
           "object hash equals the published golden hash");

    // Classification tamper-evidence is a separate, additional hash.
    const auto expected = expected_app_frame_hash(1u, tag, body.data(), body.size());
    expect(std::memcmp(parsed.app_frame_hash, expected.data(), 32u) == 0,
           "classification hash matches the documented preimage");
    expect(std::memcmp(parsed.app_frame_hash, parsed.object_hash, 32u) != 0,
           "classification hash is not the object hash");
}

// ---------------------------------------------------------------------------
// 2. Truncated frames and partial records.
// ---------------------------------------------------------------------------
void rejects_truncated_frames()
{
    const std::uint16_t tag = 0x0210u;
    const std::vector<std::uint8_t> body = golden_body("suspend_intent_v1");
    const std::vector<std::uint8_t> frame = frame_of(tag, body);

    for (std::size_t size = 0; size < 6u; ++size)
    {
        AppFrame parsed{};
        expect_status(flynes::session::wire::identify_app_frame(frame.data(), size, &parsed),
                      Status::Truncated, "identify header short by " + std::to_string(6u - size));
        expect_status(flynes::session::wire::parse_app_frame(QuicChannel::Control, frame.data(), size,
                                                             &parsed),
                      Status::Truncated, "parse header short by " + std::to_string(6u - size));
    }

    // One body byte missing: the declared length no longer agrees with the bytes.
    AppFrame parsed{};
    expect_status(flynes::session::wire::identify_app_frame(frame.data(), frame.size() - 1u, &parsed),
                  Status::Truncated, "identify with one body byte missing");

    // Declared length larger than the bytes that follow.
    std::vector<std::uint8_t> over = frame;
    write_be32(over.data(), read_be32(over.data()) + 1u);
    expect_status(flynes::session::wire::identify_app_frame(over.data(), over.size(), &parsed),
                  Status::Truncated, "identify with over-declared frame_length");
}

// ---------------------------------------------------------------------------
// 3. frame_length disagreement: too small (trailing bytes, datagram only),
//    below 2 (BadLength).
// ---------------------------------------------------------------------------
void rejects_length_disagreement()
{
    const std::uint16_t tag = 0x0210u;
    const std::vector<std::uint8_t> body = golden_body("suspend_intent_v1");
    const std::vector<std::uint8_t> frame = frame_of(tag, body);

    // A datagram carries exactly one record: anything after it is an error.
    std::vector<std::uint8_t> extra = frame;
    extra.push_back(0u);
    AppFrame parsed{};
    expect_status(flynes::session::wire::parse_app_frame(QuicChannel::Control, extra.data(),
                                                         extra.size(), &parsed),
                  Status::Trailing, "one byte after the datagram record");

    std::vector<std::uint8_t> extra4 = frame;
    extra4.insert(extra4.end(), 4u, 0u);
    expect_status(flynes::session::wire::parse_app_frame(QuicChannel::Control, extra4.data(),
                                                         extra4.size(), &parsed),
                  Status::Trailing, "a whole second record after the datagram record");

    // frame_length must cover the 2-byte tag, so 0 and 1 are impossible.
    for (std::uint32_t declared : {0u, 1u})
    {
        std::vector<std::uint8_t> bad = frame;
        write_be32(bad.data(), declared);
        expect_status(flynes::session::wire::identify_app_frame(bad.data(), bad.size(), &parsed),
                      Status::BadLength, "frame_length below 2");
    }

    // frame_length = 2 is the empty-body boundary and is structurally legal.
    std::vector<std::uint8_t> empty(6u, 0u);
    write_be32(empty.data(), 2u);
    empty[4] = 0x02u;
    empty[5] = 0x10u;
    expect_status(flynes::session::wire::identify_app_frame(empty.data(), empty.size(), &parsed),
                  Status::Truncated, "empty body fails the object validator, not the framer");
}

// ---------------------------------------------------------------------------
// 4. Per-channel maximum object size.
// ---------------------------------------------------------------------------
void rejects_above_channel_maximum()
{
    expect(flynes::session::wire::max_object_bytes(QuicChannel::Control) == 65536u,
           "Control maximum is 64 KiB (design spec:490)");
    expect(flynes::session::wire::max_object_bytes(QuicChannel::StateCommit) == 65536u,
           "State Commit maximum");
    expect(flynes::session::wire::max_object_bytes(QuicChannel::Bulk) == 245760u,
           "Bulk maximum is the canonical transition frame size");
    expect(flynes::session::wire::max_object_bytes(QuicChannel::Rom) == 65536u, "Rom maximum");
    expect(flynes::session::wire::max_object_bytes(QuicChannel::Input) == 1200u, "Input maximum");
    expect(flynes::session::wire::max_object_bytes(QuicChannel::Video) == 1200u, "Video maximum");
    expect(flynes::session::wire::max_object_bytes(QuicChannel::Audio) == 1200u, "Audio maximum");
    expect(flynes::session::wire::absolute_max_object_bytes() == 245760u,
           "channel-independent maximum is the largest per-channel maximum");

    // One byte over the Control bound is a framing error, not a validator error.
    std::vector<std::uint8_t> oversized(6u + 65537u, 0u);
    write_be32(oversized.data(), 2u + 65537u);
    oversized[4] = 0x02u;
    oversized[5] = 0x12u;
    AppFrame parsed{};
    expect_status(flynes::session::wire::parse_app_frame(QuicChannel::Control, oversized.data(),
                                                         oversized.size(), &parsed),
                  Status::BadLength, "Control body one byte over 64 KiB");
    // The same bytes are within Bulk's bound and fail later, on the allow-list.
    expect_status(flynes::session::wire::parse_app_frame(QuicChannel::Bulk, oversized.data(),
                                                         oversized.size(), &parsed),
                  Status::InvalidField, "same record is not a Bulk object");

    // Exactly at the Control bound the frame is accepted by the framer and only
    // the object validator rejects it (0x0212 is 312 bytes, so 65536 is long).
    std::vector<std::uint8_t> at_max(6u + 65536u, 0u);
    write_be32(at_max.data(), 2u + 65536u);
    at_max[4] = 0x02u;
    at_max[5] = 0x12u;
    expect_status(flynes::session::wire::parse_app_frame(QuicChannel::Control, at_max.data(),
                                                         at_max.size(), &parsed),
                  Status::Trailing, "exactly at the Control bound reaches the validator");
}

// ---------------------------------------------------------------------------
// 5. Identification is by the explicit tag only — never trial validation.
// ---------------------------------------------------------------------------
void identifies_by_tag_only()
{
    AppFrame parsed{};

    // 0x0203 (INPUT_SEQUENCE_RESERVATION_V1) and 0x0205
    // (RECONNECT_DEADLINE_RECORD_V1) are both 144-byte fixed records with the
    // same 8-byte header, so a 0x0203 body also satisfies the 0x0205 checks.
    // Tagged 0x0205 it must be identified as 0x0205 and hashed in that domain.
    const std::vector<std::uint8_t> reservation = golden_body("input_sequence_reservation_v1");
    expect(reservation.size() == 144u, "input_sequence_reservation_v1 is 144 bytes");
    const std::vector<std::uint8_t> frame = frame_of(0x0205u, reservation);
    expect_status(flynes::session::wire::identify_app_frame(frame.data(), frame.size(), &parsed),
                  Status::Ok, "identify the re-tagged body");
    expect(parsed.frame_type_tag == 0x0205u, "tag is preserved");
    expect(std::strcmp(parsed.type_name, "0x0205") == 0,
           "type name comes from the tag, not from the body's origin");

    std::uint8_t as_0205[32]{};
    std::uint8_t as_0203[32]{};
    expect_status(flynes::session::wire::check("0x0205", reservation.data(), reservation.size(), as_0205),
                  Status::Ok, "the body also validates as 0x0205");
    expect_status(flynes::session::wire::check("0x0203", reservation.data(), reservation.size(), as_0203),
                  Status::Ok, "the body validates as 0x0203");
    expect(std::memcmp(parsed.object_hash, as_0205, 32u) == 0,
           "the object hash uses the identified type's domain");
    expect(std::memcmp(parsed.object_hash, as_0203, 32u) != 0,
           "and therefore differs from the origin type's hash");

    // Unknown tags in either namespace fail closed.
    const std::uint16_t unknown[] = {0x0000u, 0x0400u, 0x1234u, 0xEFFFu, 0xF000u, 0xF0FFu, 0xFF06u, 0xFFFFu};
    for (std::uint16_t tag : unknown)
    {
        const std::vector<std::uint8_t> bad = raw_frame_of(tag, reservation);
        expect_status(flynes::session::wire::identify_app_frame(bad.data(), bad.size(), &parsed),
                      Status::UnknownKind, "unknown tag");
    }

    // Message namespace entries identify by index into the schema's `messages`.
    const std::vector<std::uint8_t> cursor = golden_body("frame_cursor_genesis");
    const std::vector<std::uint8_t> cursor_frame = frame_of(0xFF00u, cursor);
    expect_status(flynes::session::wire::identify_app_frame(cursor_frame.data(), cursor_frame.size(),
                                                            &parsed),
                  Status::Ok, "message tag 0xFF00 identifies");
    expect(parsed.type_namespace == FrameTypeNamespace::Message, "message namespace");
    expect(std::strcmp(parsed.type_name, "FrameCursorV1") == 0, "message tag 0xFF00 is FrameCursorV1");

    const std::vector<std::uint8_t> summary = golden_body("channel_resume_summary_v1");
    const std::vector<std::uint8_t> summary_frame = frame_of(0xFF05u, summary);
    expect_status(flynes::session::wire::identify_app_frame(summary_frame.data(), summary_frame.size(),
                                                            &parsed),
                  Status::Ok, "message tag 0xFF05 identifies");
    expect(std::strcmp(parsed.type_name, "ChannelResumeSummaryV1") == 0,
           "message tag 0xFF05 is ChannelResumeSummaryV1");

    // Table introspection used by the consistency guard.
    expect(flynes::session::wire::frame_tag_count() == 70u,
           "62 schema kinds plus 8 schema messages");
    FrameTagInfo info{};
    expect(flynes::session::wire::frame_tag_info(0x0201u, &info), "0x0201 is in the table");
    expect(info.type_namespace == FrameTypeNamespace::ObjectKind, "0x0201 namespace");
    expect(std::strcmp(info.type_name, "0x0201") == 0, "0x0201 type name");
    expect(!flynes::session::wire::frame_tag_info(0xF000u, &info), "0xF000 is not in the table");
}

// ---------------------------------------------------------------------------
// 6. Per-channel allow-list.
// ---------------------------------------------------------------------------
void enforces_channel_allow_list()
{
    AppFrame parsed{};
    const std::vector<std::uint8_t> suspend = golden_body("suspend_intent_v1");
    const std::vector<std::uint8_t> key_binding = golden_body("session_signing_key_binding_v1");
    const std::vector<std::uint8_t> reservation = golden_body("input_sequence_reservation_v1");
    const std::vector<std::uint8_t> transition = golden_body("transition_rgba8888_frame_v1");
    const std::vector<std::uint8_t> bundle = golden_body("canonical_input_bundle_v1");
    const std::vector<std::uint8_t> bind = golden_body("channel_bind_v1_initial");

    const std::vector<std::uint8_t> key_frame = frame_of(0x0212u, key_binding);
    expect_status(flynes::session::wire::parse_app_frame(QuicChannel::Control, key_frame.data(),
                                                         key_frame.size(), &parsed),
                  Status::Ok, "SessionSigningKeyBindingV1 is a Control object");

    const std::vector<std::uint8_t> suspend_frame = frame_of(0x0210u, suspend);
    expect_status(flynes::session::wire::parse_app_frame(QuicChannel::Control, suspend_frame.data(),
                                                         suspend_frame.size(), &parsed),
                  Status::Ok, "SuspendIntentV1 is a Control object");

    std::vector<std::uint8_t> confirm(44u, 0u);
    confirm[1] = 1u;
    confirm[4] = 0xA1u;
    confirm[43] = 1u;
    const std::vector<std::uint8_t> confirm_frame = raw_frame_of(0x0218u, confirm);
    expect_status(flynes::session::wire::parse_app_frame(QuicChannel::Control, confirm_frame.data(),
                                                         confirm_frame.size(), &parsed),
                  Status::Ok, "PendingConfigConfirmV1 is a Control object");
    for (QuicChannel channel : kAllChannels)
    {
        if (channel == QuicChannel::Control)
            continue;
        expect_status(flynes::session::wire::parse_app_frame(channel, confirm_frame.data(),
                                                             confirm_frame.size(), &parsed),
                      Status::InvalidField,
                      std::string("PendingConfigConfirmV1 rejected on ") + channel_name(channel));
    }

    const std::vector<std::uint8_t> reservation_frame = frame_of(0x0203u, reservation);
    expect_status(flynes::session::wire::parse_app_frame(QuicChannel::StateCommit,
                                                         reservation_frame.data(),
                                                         reservation_frame.size(), &parsed),
                  Status::Ok, "InputSequenceReservationV1 is a State Commit object");

    const std::vector<std::uint8_t> transition_frame = frame_of(0x000bu, transition);
    expect_status(flynes::session::wire::parse_app_frame(QuicChannel::Bulk, transition_frame.data(),
                                                         transition_frame.size(), &parsed),
                  Status::Ok, "the canonical transition frame is a Bulk object");

    // CanonicalInputBundleV1 (message tag) is legal on State Commit only.
    const std::vector<std::uint8_t> bundle_frame = frame_of(0xFF02u, bundle);
    expect_status(flynes::session::wire::parse_app_frame(QuicChannel::StateCommit,
                                                         bundle_frame.data(), bundle_frame.size(),
                                                         &parsed),
                  Status::Ok, "CanonicalInputBundleV1 is a State Commit object");
    for (QuicChannel channel : kAllChannels)
    {
        if (channel == QuicChannel::StateCommit)
            continue;
        expect_status(flynes::session::wire::parse_app_frame(channel, bundle_frame.data(),
                                                             bundle_frame.size(), &parsed),
                      Status::InvalidField,
                      std::string("CanonicalInputBundleV1 rejected on ") + channel_name(channel));
    }

    // A record that is legal somewhere is still rejected everywhere else. The
    // per-channel maximum is a framing bound and is checked before the
    // allow-list, so an over-sized Bulk object fails as BadLength on Control.
    expect_status(flynes::session::wire::parse_app_frame(QuicChannel::StateCommit, key_frame.data(),
                                                         key_frame.size(), &parsed),
                  Status::InvalidField, "Control object rejected on State Commit");
    expect_status(flynes::session::wire::parse_app_frame(QuicChannel::Control, transition_frame.data(),
                                                         transition_frame.size(), &parsed),
                  Status::BadLength, "over-sized Bulk object rejected by the Control bound");
    expect_status(flynes::session::wire::parse_app_frame(QuicChannel::Bulk, suspend_frame.data(),
                                                         suspend_frame.size(), &parsed),
                  Status::InvalidField, "Control object rejected on Bulk");

    // The pre-app bind messages belong to the bind streams, never to the seven
    // application channels (addendum item 1).
    const std::vector<std::uint8_t> bind_frame = frame_of(0xFF03u, bind);
    for (QuicChannel channel : kAllChannels)
    {
        expect_status(flynes::session::wire::parse_app_frame(channel, bind_frame.data(),
                                                             bind_frame.size(), &parsed),
                      Status::InvalidField,
                      std::string("ChannelBindV1 rejected on ") + channel_name(channel));
    }

    // A schema kind with no assigned channel fails closed everywhere.
    const std::vector<std::uint8_t> unassigned = frame_of(0x0306u, suspend);
    for (QuicChannel channel : {QuicChannel::Control, QuicChannel::StateCommit, QuicChannel::Bulk})
    {
        expect_status(flynes::session::wire::parse_app_frame(channel, unassigned.data(),
                                                             unassigned.size(), &parsed),
                      Status::InvalidField,
                      std::string("unassigned kind rejected on ") + channel_name(channel));
    }

    // Allow-list queries agree with the parser.
    expect(flynes::session::wire::tag_is_legal_on(QuicChannel::Control, 0x0212u), "query 0x0212/Control");
    expect(!flynes::session::wire::tag_is_legal_on(QuicChannel::Input, 0x0212u), "query 0x0212/Input");
    expect(flynes::session::wire::tag_is_legal_on(QuicChannel::StateCommit, 0xFF02u),
           "query CanonicalInputBundleV1/StateCommit");
    expect(!flynes::session::wire::tag_is_legal_on(QuicChannel::StateCommit, 0xFF03u),
           "query ChannelBindV1/StateCommit");
    expect(flynes::session::wire::type_name_is_legal_on(QuicChannel::StateCommit, "CanonicalInputBundleV1"),
           "type-name query CanonicalInputBundleV1/StateCommit");
    expect(!flynes::session::wire::type_name_is_legal_on(QuicChannel::Control, "CanonicalInputBundleV1"),
           "type-name query CanonicalInputBundleV1/Control");
}

// ---------------------------------------------------------------------------
// 7. The Input, Video, Audio and ROM allow-lists are explicitly empty.
// ---------------------------------------------------------------------------
void empty_channels_fail_closed()
{
    const QuicChannel empty[] = {QuicChannel::Input, QuicChannel::Video, QuicChannel::Audio,
                                 QuicChannel::Rom};
    for (QuicChannel channel : empty)
    {
        expect(flynes::session::wire::allowed_type_count(channel) == 0u,
               std::string("no schema-defined object on ") + channel_name(channel));
    }
    expect(flynes::session::wire::allowed_type_count(QuicChannel::Control) > 0u, "Control populated");
    expect(flynes::session::wire::allowed_type_count(QuicChannel::StateCommit) > 0u,
           "State Commit populated");
    expect(flynes::session::wire::allowed_type_count(QuicChannel::Bulk) > 0u, "Bulk populated");

    const std::vector<std::uint8_t> body = golden_body("suspend_intent_v1");
    const std::vector<std::uint8_t> frame = frame_of(0x0210u, body);
    for (QuicChannel channel : empty)
    {
        AppFrame parsed{};
        expect_status(flynes::session::wire::parse_app_frame(channel, frame.data(), frame.size(), &parsed),
                      Status::InvalidField,
                      std::string("well-formed record still rejected on ") + channel_name(channel));
    }
}

// ---------------------------------------------------------------------------
// 8. Reliable streams carry zero or more back-to-back records.
// ---------------------------------------------------------------------------
void multi_record_stream()
{
    const std::vector<std::uint8_t> suspend = golden_body("suspend_intent_v1");
    const std::vector<std::uint8_t> key_binding = golden_body("session_signing_key_binding_v1");
    const std::vector<std::uint8_t> suspend_frame = frame_of(0x0210u, suspend);
    const std::vector<std::uint8_t> key_frame = frame_of(0x0212u, key_binding);

    // Three records, as spec:441 mandates for the bind stream shape.
    std::vector<std::uint8_t> three = suspend_frame;
    three.insert(three.end(), key_frame.begin(), key_frame.end());
    three.insert(three.end(), suspend_frame.begin(), suspend_frame.end());

    AppFrameCursor cursor = flynes::session::wire::app_frame_cursor(
        QuicChannel::Control, three.data(), three.size());
    const std::uint16_t wanted[] = {0x0210u, 0x0212u, 0x0210u};
    for (std::size_t i = 0; i < 3u; ++i)
    {
        AppFrame parsed{};
        bool has_frame = false;
        expect_status(flynes::session::wire::next_app_frame(&cursor, &parsed, &has_frame),
                      Status::Ok, "stream record " + std::to_string(i));
        expect(has_frame, "stream record present");
        expect(parsed.frame_type_tag == wanted[i], "stream record tag order");
    }
    {
        AppFrame parsed{};
        bool has_frame = true;
        expect_status(flynes::session::wire::next_app_frame(&cursor, &parsed, &has_frame), Status::Ok,
                      "stream end");
        expect(!has_frame, "stream ends cleanly at zero remaining bytes");
        expect(cursor.offset == three.size(), "cursor consumed the whole stream");
    }

    // Six records, as spec:439 mandates.
    std::vector<std::uint8_t> six;
    for (int i = 0; i < 6; ++i)
    {
        const std::vector<std::uint8_t>& record = (i % 2) == 0 ? suspend_frame : key_frame;
        six.insert(six.end(), record.begin(), record.end());
    }
    AppFrameCursor six_cursor =
        flynes::session::wire::app_frame_cursor(QuicChannel::Control, six.data(), six.size());
    int seen = 0;
    for (;;)
    {
        AppFrame parsed{};
        bool has_frame = false;
        expect_status(flynes::session::wire::next_app_frame(&six_cursor, &parsed, &has_frame),
                      Status::Ok, "six-record stream");
        if (!has_frame)
            break;
        ++seen;
    }
    expect(seen == 6, "six back-to-back records are all accepted");

    // An empty stream is zero records, not an error.
    AppFrameCursor empty = flynes::session::wire::app_frame_cursor(QuicChannel::Control, nullptr, 0u);
    {
        AppFrame parsed{};
        bool has_frame = true;
        expect_status(flynes::session::wire::next_app_frame(&empty, &parsed, &has_frame), Status::Ok,
                      "empty stream");
        expect(!has_frame, "empty stream has zero records");
    }

    // A leftover of 1..3 bytes is a partial length prefix.
    for (std::size_t leftover = 1u; leftover <= 3u; ++leftover)
    {
        std::vector<std::uint8_t> partial = suspend_frame;
        partial.insert(partial.end(), leftover, 0u);
        AppFrameCursor partial_cursor =
            flynes::session::wire::app_frame_cursor(QuicChannel::Control, partial.data(), partial.size());
        AppFrame parsed{};
        bool has_frame = false;
        expect_status(flynes::session::wire::next_app_frame(&partial_cursor, &parsed, &has_frame),
                      Status::Ok, "first record of a partial stream");
        expect(has_frame, "first record present");
        expect_status(flynes::session::wire::next_app_frame(&partial_cursor, &parsed, &has_frame),
                      Status::Truncated, "leftover length prefix is an error");
    }

    // A length prefix promising more bytes than the stream holds.
    std::vector<std::uint8_t> promise = suspend_frame;
    promise.insert(promise.end(), 4u, 0u);
    write_be32(promise.data() + suspend_frame.size(), 4u + 2u + 240u);
    AppFrameCursor promise_cursor =
        flynes::session::wire::app_frame_cursor(QuicChannel::Control, promise.data(), promise.size());
    {
        AppFrame parsed{};
        bool has_frame = false;
        expect_status(flynes::session::wire::next_app_frame(&promise_cursor, &parsed, &has_frame),
                      Status::Ok, "first record before the short promise");
        expect_status(flynes::session::wire::next_app_frame(&promise_cursor, &parsed, &has_frame),
                      Status::Truncated, "frame_length larger than the remaining bytes");
    }

    // A record that is illegal on the stream's channel fails without advancing.
    const std::size_t before = cursor.offset;
    AppFrameCursor bulk_cursor = flynes::session::wire::app_frame_cursor(
        QuicChannel::Bulk, suspend_frame.data(), suspend_frame.size());
    AppFrame parsed{};
    bool has_frame = false;
    expect_status(flynes::session::wire::next_app_frame(&bulk_cursor, &parsed, &has_frame),
                  Status::InvalidField, "stream record illegal on the stream channel");
    expect(bulk_cursor.offset == 0u, "a rejected record does not advance the cursor");
    expect(before == three.size(), "the earlier cursor is untouched");
}

// ---------------------------------------------------------------------------
// 9. Encoder bounds.
// ---------------------------------------------------------------------------
void encode_bounds()
{
    const std::vector<std::uint8_t> body = golden_body("suspend_intent_v1");
    std::uint8_t out[512]{};
    std::size_t written = 0u;

    expect_status(flynes::session::wire::encode_app_frame(0x0210u, body.data(), body.size(), nullptr,
                                                          sizeof(out), &written),
                  Status::InvalidField, "null output buffer");
    expect_status(flynes::session::wire::encode_app_frame(0x0210u, body.data(), body.size(), out,
                                                          sizeof(out), nullptr),
                  Status::InvalidField, "null written pointer");
    expect_status(flynes::session::wire::encode_app_frame(0x0210u, nullptr, body.size(), out,
                                                          sizeof(out), &written),
                  Status::InvalidField, "null body with a non-zero size");
    expect_status(flynes::session::wire::encode_app_frame(0x0210u, body.data(), body.size(), out,
                                                          body.size(), &written),
                  Status::Truncated, "output buffer too small");
    expect_status(flynes::session::wire::encode_app_frame(0x0210u, body.data(), body.size(), out,
                                                          body.size() + 6u, &written),
                  Status::Ok, "output buffer exactly large enough");
    expect(written == body.size() + 6u, "written size");

    // The encoder refuses a tag the parser could never identify, so every
    // encodable record is identifiable.
    expect_status(flynes::session::wire::encode_app_frame(0x1234u, body.data(), body.size(), out,
                                                          sizeof(out), &written),
                  Status::UnknownKind, "unknown tag is not encodable");

    // Empty bodies frame, then fail the object validator.
    expect_status(flynes::session::wire::encode_app_frame(0x0210u, nullptr, 0u, out, sizeof(out),
                                                          &written),
                  Status::Ok, "empty body frames");
    expect(written == 6u, "empty body frame size");
    expect(out[3] == 2u, "empty body frame_length is 2");

    // Above the channel-independent maximum the length cannot be expressed.
    expect_status(flynes::session::wire::encode_app_frame(0x0210u, body.data(),
                                                          flynes::session::wire::absolute_max_object_bytes() + 1u,
                                                          out, sizeof(out), &written),
                  Status::BadLength, "body above the absolute maximum");
}

// ---------------------------------------------------------------------------
// 10. Classification tamper-evidence.
// ---------------------------------------------------------------------------
void classification_hash_binds_channel_and_tag()
{
    const std::vector<std::uint8_t> body = golden_body("suspend_intent_v1");

    std::uint8_t on_control[32]{};
    std::uint8_t on_state[32]{};
    std::uint8_t other_tag[32]{};
    flynes::session::wire::compute_app_frame_hash(QuicChannel::Control, 0x0210u, body.data(),
                                                  body.size(), on_control);
    flynes::session::wire::compute_app_frame_hash(QuicChannel::StateCommit, 0x0210u, body.data(),
                                                  body.size(), on_state);
    flynes::session::wire::compute_app_frame_hash(QuicChannel::Control, 0x0211u, body.data(),
                                                  body.size(), other_tag);
    expect(std::memcmp(on_control, on_state, 32u) != 0, "classification hash binds the channel");
    expect(std::memcmp(on_control, other_tag, 32u) != 0, "classification hash binds the tag");

    // Re-tagging a valid record is therefore visible in the classification hash
    // even though the object bytes and the object hash are untouched. The
    // 0x0203 body also satisfies the 0x0205 validator, so this is exactly the
    // case an attacker would use: the same bytes, two type names, two different
    // classification hashes.
    const std::vector<std::uint8_t> reservation = golden_body("input_sequence_reservation_v1");
    std::uint8_t as_0203[32]{};
    std::uint8_t as_0205[32]{};
    flynes::session::wire::compute_app_frame_hash(QuicChannel::StateCommit, 0x0203u,
                                                  reservation.data(), reservation.size(), as_0203);
    flynes::session::wire::compute_app_frame_hash(QuicChannel::StateCommit, 0x0205u,
                                                  reservation.data(), reservation.size(), as_0205);
    expect(std::memcmp(as_0203, as_0205, 32u) != 0,
           "two type names for the same valid body have different classification hashes");

    // A flipped tag that no longer satisfies the new tag's validator fails
    // closed rather than being re-classified.
    std::vector<std::uint8_t> retagged = frame_of(0x0210u, body);
    retagged[5] = 0x12u; // 0x0212 is a 312-byte record, this body is 240 bytes
    AppFrame parsed{};
    expect(flynes::session::wire::identify_app_frame(retagged.data(), retagged.size(), &parsed) !=
               Status::Ok,
           "a flipped tag that no longer validates fails closed");
}

} // namespace

int main()
{
    frame_layout_and_round_trip();
    rejects_truncated_frames();
    rejects_length_disagreement();
    rejects_above_channel_maximum();
    identifies_by_tag_only();
    enforces_channel_allow_list();
    empty_channels_fail_closed();
    multi_record_stream();
    encode_bounds();
    classification_hash_binds_channel_and_tag();

    if (failures != 0)
    {
        std::cerr << "flynes_session_app_frame: FAIL (" << failures << ")\n";
        return 1;
    }
    std::cout << "flynes_session_app_frame: PASS\n";
    return 0;
}
