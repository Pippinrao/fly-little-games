#include "link_ready.hpp"

#include "p256_point.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <cstring>

namespace flynes::session::wire {
namespace {

using link::kLinkReadyDigestDomainV1;
using link::kLinkReadyObjectHashDomainV1;
using link::kLinkReadyPretagSizeV1;
using link::kLinkReadySizeV1;

bool nonzero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return bytes != nullptr &&
           std::any_of(bytes, bytes + size,
                       [](std::uint8_t value) { return value != 0; });
}

bool zeros(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return !nonzero(bytes, size);
}

bool valid_role(PairRoleV1 role) noexcept
{
    return role == PairRoleV1::Initiator || role == PairRoleV1::Responder;
}

bool valid_ready_phase(link::LinkReadyPhaseV1 phase) noexcept
{
    return phase == link::LinkReadyPhaseV1::Ready ||
           phase == link::LinkReadyPhaseV1::Ack;
}

void put_be64(std::uint8_t* out, std::uint64_t value) noexcept
{
    for (int index = 0; index < 8; ++index)
        out[index] = static_cast<std::uint8_t>(value >> ((7 - index) * 8));
}

std::uint16_t be16(const std::uint8_t* in) noexcept
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(in[0]) << 8) | in[1]);
}

std::uint64_t be64(const std::uint8_t* in) noexcept
{
    std::uint64_t value = 0;
    for (int index = 0; index < 8; ++index)
        value = (value << 8) | in[index];
    return value;
}

void copy_out(const std::uint8_t* source, std::size_t size,
              std::uint8_t* destination) noexcept
{
    std::memcpy(destination, source, size);
}

bool same(const std::uint8_t* left, const std::uint8_t* right,
          std::size_t size) noexcept
{
    return std::memcmp(left, right, size) == 0;
}

Status fail(LinkControlDecodeReportV1* report, LinkControlIssueV1 issue,
            Status status) noexcept
{
    if (report != nullptr) {
        report->status = status;
        report->issue = issue;
    }
    return status;
}

void parse_pretag(const std::uint8_t* bytes, link::LinkReadyV1* out) noexcept
{
    out->version = be16(bytes);
    out->sender_role = static_cast<PairRoleV1>(bytes[8]);
    out->receiver_role = static_cast<PairRoleV1>(bytes[9]);
    out->phase = static_cast<link::LinkPhaseV1>(bytes[10]);
    out->ready_phase = static_cast<link::LinkReadyPhaseV1>(bytes[11]);
    copy_out(bytes + 16, 16, out->session_id.data());
    copy_out(bytes + 32, 16, out->link_id.data());
    out->channel_id = be64(bytes + 48);
    out->channel_bind_id = be64(bytes + 56);
    out->connection_generation = be64(bytes + 64);
    out->reconnect_attempt = be64(bytes + 72);
    out->link_generation = be64(bytes + 80);
    copy_out(bytes + 88, 32, out->channel_bind_hash.data());
    copy_out(bytes + 120, 32, out->local_hello_object_hash.data());
    copy_out(bytes + 152, 32, out->peer_hello_object_hash.data());
    copy_out(bytes + 184, 32, out->negotiated_result_hash.data());
    copy_out(bytes + 216, 32, out->local_summary_hash.data());
    copy_out(bytes + 248, 32, out->peer_summary_hash.data());
    copy_out(bytes + 280, 32, out->merge_result_hash.data());
}

} // namespace

Status build_link_ready_pretag_v1(
    const link::LinkReadyV1& value, P256PointValidatorV1 validate_point,
    void* validator_context,
    std::array<std::uint8_t, kLinkReadyPretagSizeV1>* out_pretag,
    std::array<std::uint8_t, 32>* out_digest) noexcept
{
    if (out_pretag == nullptr || out_digest == nullptr)
        return Status::InvalidField;
    (void)validate_point;
    (void)validator_context;
    if (value.version != 1) return Status::UnknownEnum;
    if (!valid_role(value.sender_role) || !valid_role(value.receiver_role))
        return Status::UnknownEnum;
    if (!link::link_roles_are_mirrored_v1(value.sender_role,
                                          value.receiver_role))
        return Status::InvalidField;
    /* READY is only legal in phase RECONCILE. */
    if (value.phase != link::LinkPhaseV1::Reconcile) return Status::InvalidField;
    if (!valid_ready_phase(value.ready_phase)) return Status::UnknownEnum;

    if (!nonzero(value.session_id.data(), value.session_id.size()) ||
        !nonzero(value.link_id.data(), value.link_id.size()))
        return Status::InvalidField;
    if (value.channel_id == 0 || value.channel_bind_id == 0 ||
        value.connection_generation == 0 || value.link_generation == 0)
        return Status::InvalidField;
    /* Every bound hash must be a real persisted/verified value. */
    if (!nonzero(value.channel_bind_hash.data(), value.channel_bind_hash.size()) ||
        !nonzero(value.local_hello_object_hash.data(),
                 value.local_hello_object_hash.size()) ||
        !nonzero(value.peer_hello_object_hash.data(),
                 value.peer_hello_object_hash.size()) ||
        !nonzero(value.negotiated_result_hash.data(),
                 value.negotiated_result_hash.size()) ||
        !nonzero(value.local_summary_hash.data(),
                 value.local_summary_hash.size()) ||
        !nonzero(value.peer_summary_hash.data(),
                 value.peer_summary_hash.size()) ||
        !nonzero(value.merge_result_hash.data(),
                 value.merge_result_hash.size()))
        return Status::InvalidField;
    if (same(value.local_hello_object_hash.data(),
             value.peer_hello_object_hash.data(), 32))
        return Status::InvalidField;

    out_pretag->fill(0);
    auto* out = out_pretag->data();
    out[1] = 1;
    out[8] = static_cast<std::uint8_t>(value.sender_role);
    out[9] = static_cast<std::uint8_t>(value.receiver_role);
    out[10] = static_cast<std::uint8_t>(value.phase);
    out[11] = static_cast<std::uint8_t>(value.ready_phase);
    copy_out(value.session_id.data(), 16, out + 16);
    copy_out(value.link_id.data(), 16, out + 32);
    put_be64(out + 48, value.channel_id);
    put_be64(out + 56, value.channel_bind_id);
    put_be64(out + 64, value.connection_generation);
    put_be64(out + 72, value.reconnect_attempt);
    put_be64(out + 80, value.link_generation);
    copy_out(value.channel_bind_hash.data(), 32, out + 88);
    copy_out(value.local_hello_object_hash.data(), 32, out + 120);
    copy_out(value.peer_hello_object_hash.data(), 32, out + 152);
    copy_out(value.negotiated_result_hash.data(), 32, out + 184);
    copy_out(value.local_summary_hash.data(), 32, out + 216);
    copy_out(value.peer_summary_hash.data(), 32, out + 248);
    copy_out(value.merge_result_hash.data(), 32, out + 280);
    *out_digest = domain_hash(kLinkReadyDigestDomainV1, out, out_pretag->size());
    return Status::Ok;
}

Status finish_link_ready_v1(
    const std::array<std::uint8_t, kLinkReadyPretagSizeV1>& pretag,
    const std::array<std::uint8_t, 64>& signature,
    std::array<std::uint8_t, kLinkReadySizeV1>* out_bytes,
    link::LinkReadyV1* out_value) noexcept
{
    if (out_bytes == nullptr || out_value == nullptr ||
        !link_control_signature_is_canonical_v1(signature.data()))
        return Status::InvalidField;
    std::copy(pretag.begin(), pretag.end(), out_bytes->begin());
    std::copy(signature.begin(), signature.end(),
              out_bytes->begin() +
                  static_cast<std::ptrdiff_t>(kLinkReadyPretagSizeV1));
    link::LinkReadyV1 parsed{};
    parse_pretag(out_bytes->data(), &parsed);
    parsed.signature = signature;
    parsed.digest = domain_hash(kLinkReadyDigestDomainV1, out_bytes->data(),
                                kLinkReadyPretagSizeV1);
    parsed.object_hash = domain_hash(kLinkReadyObjectHashDomainV1,
                                     out_bytes->data(), out_bytes->size());
    *out_value = parsed;
    return Status::Ok;
}

/*
 * Stage 1: every non-cryptographic READY/ACK check, including the canonical
 * low-S shape of the signature encoding. A rejected signature is decided only in
 * stage 2, once the asynchronous verification result is available.
 */
Status parse_link_ready_v1(
    const std::uint8_t* bytes, std::size_t size,
    const LinkReadyExpectationsV1& expected, P256PointValidatorV1 validate_point,
    void* validator_context, LinkControlDecodeReportV1* out_report,
    LinkControlParsedV1* out_parsed, link::LinkReadyV1* out) noexcept
{
    if (out_report != nullptr) {
        out_report->status = Status::Ok;
        out_report->issue = LinkControlIssueV1::None;
        out_report->proposal = link::LinkProposalSupportV1::Supported;
    }
    if (out_parsed != nullptr) *out_parsed = LinkControlParsedV1{};
    if (size < kLinkReadySizeV1)
        return fail(out_report, LinkControlIssueV1::Size, Status::Truncated);
    if (size > kLinkReadySizeV1)
        return fail(out_report, LinkControlIssueV1::Size, Status::Trailing);
    if (bytes == nullptr || out == nullptr || out_report == nullptr ||
        out_parsed == nullptr || validate_point == nullptr)
        return fail(out_report, LinkControlIssueV1::None, Status::InvalidField);
    if (!valid_role(expected.local_role) ||
        !valid_ready_phase(expected.expected_phase))
        return fail(out_report, LinkControlIssueV1::Discriminant,
                    Status::InvalidField);
    if (!nonzero(expected.channel_bind_hash.data(),
                 expected.channel_bind_hash.size()) ||
        !nonzero(expected.local_hello_object_hash.data(),
                 expected.local_hello_object_hash.size()) ||
        !nonzero(expected.peer_hello_object_hash.data(),
                 expected.peer_hello_object_hash.size()) ||
        !nonzero(expected.negotiated_result_hash.data(),
                 expected.negotiated_result_hash.size()) ||
        !nonzero(expected.local_summary_hash.data(),
                 expected.local_summary_hash.size()) ||
        !nonzero(expected.peer_summary_hash.data(),
                 expected.peer_summary_hash.size()) ||
        !nonzero(expected.merge_result_hash.data(),
                 expected.merge_result_hash.size()))
        return fail(out_report, LinkControlIssueV1::ExpectedField,
                    Status::InvalidField);

    if (bytes[0] != 0 || bytes[1] != 1)
        return fail(out_report, LinkControlIssueV1::Version, Status::UnknownEnum);
    if (!zeros(bytes + 2, 6))
        return fail(out_report, LinkControlIssueV1::Reserved,
                    Status::NonzeroReserved);

    const auto sender = static_cast<PairRoleV1>(bytes[8]);
    const auto receiver = static_cast<PairRoleV1>(bytes[9]);
    if (!valid_role(sender) || !valid_role(receiver))
        return fail(out_report, LinkControlIssueV1::Discriminant,
                    Status::UnknownEnum);
    if (!link::link_roles_are_mirrored_v1(sender, receiver) ||
        sender == expected.local_role || receiver != expected.local_role)
        return fail(out_report, LinkControlIssueV1::Discriminant,
                    Status::InvalidField);

    if (bytes[10] != static_cast<std::uint8_t>(link::LinkPhaseV1::Reconcile))
        return fail(out_report, LinkControlIssueV1::Discriminant,
                    bytes[10] ==
                            static_cast<std::uint8_t>(link::LinkPhaseV1::Initial)
                        ? Status::InvalidField
                        : Status::UnknownEnum);
    const auto ready_phase = static_cast<link::LinkReadyPhaseV1>(bytes[11]);
    if (!valid_ready_phase(ready_phase))
        return fail(out_report, LinkControlIssueV1::Discriminant,
                    Status::UnknownEnum);
    if (ready_phase != expected.expected_phase)
        return fail(out_report, LinkControlIssueV1::Discriminant,
                    Status::InvalidField);
    if (!zeros(bytes + 12, 4))
        return fail(out_report, LinkControlIssueV1::Reserved,
                    Status::NonzeroReserved);

    if (!same(bytes + 16, expected.session_id.data(), 16) ||
        !same(bytes + 32, expected.link_id.data(), 16))
        return fail(out_report, LinkControlIssueV1::ExpectedField,
                    Status::InvalidField);
    if (be64(bytes + 48) != expected.channel_id)
        return fail(out_report, LinkControlIssueV1::ExpectedField,
                    Status::InvalidField);
    if (be64(bytes + 56) != expected.channel_bind_id)
        return fail(out_report, LinkControlIssueV1::BindingRef,
                    Status::InvalidField);
    if (be64(bytes + 64) != expected.connection_generation ||
        be64(bytes + 72) != expected.reconnect_attempt ||
        be64(bytes + 80) != expected.link_generation)
        return fail(out_report, LinkControlIssueV1::Generation,
                    Status::InvalidField);

    if (!same(bytes + 88, expected.channel_bind_hash.data(), 32))
        return fail(out_report, LinkControlIssueV1::BindingRef,
                    Status::InvalidField);
    /* Mirrored HELLO binding: the sender's own HELLO is the peer HELLO we
     * verified, and the sender's peer HELLO is our own persisted HELLO. */
    if (!same(bytes + 120, expected.peer_hello_object_hash.data(), 32) ||
        !same(bytes + 152, expected.local_hello_object_hash.data(), 32))
        return fail(out_report, LinkControlIssueV1::ExpectedField,
                    Status::InvalidField);
    if (!same(bytes + 184, expected.negotiated_result_hash.data(), 32) ||
        !same(bytes + 216, expected.peer_summary_hash.data(), 32) ||
        !same(bytes + 248, expected.local_summary_hash.data(), 32) ||
        !same(bytes + 280, expected.merge_result_hash.data(), 32))
        return fail(out_report, LinkControlIssueV1::ExpectedField,
                    Status::InvalidField);
    if (!zeros(bytes + 312, 8))
        return fail(out_report, LinkControlIssueV1::Reserved,
                    Status::NonzeroReserved);

    if (!link_control_signature_is_canonical_v1(bytes + 320) ||
        !validate_point(validator_context,
                        expected.peer_session_signing_public_key.data()))
        return fail(out_report, LinkControlIssueV1::Signature,
                    Status::InvalidField);

    copy_out(expected.peer_session_signing_public_key.data(), 65,
             out_parsed->signer_public_key.data());
    out_parsed->digest =
        domain_hash(kLinkReadyDigestDomainV1, bytes, kLinkReadyPretagSizeV1);
    copy_out(bytes + 320, 64, out_parsed->signature.data());

    link::LinkReadyV1 parsed{};
    parse_pretag(bytes, &parsed);
    parsed.signature = out_parsed->signature;
    parsed.digest = out_parsed->digest;
    parsed.object_hash =
        domain_hash(kLinkReadyObjectHashDomainV1, bytes, kLinkReadySizeV1);
    *out = parsed;
    return Status::Ok;
}

Status accept_link_ready_v1(const LinkControlParsedV1& parsed,
                            const link::LinkReadyV1& parsed_value,
                            LinkControlVerificationOutcomeV1 outcome,
                            LinkControlDecodeReportV1* out_report,
                            link::LinkReadyV1* out) noexcept
{
    if (outcome != LinkControlVerificationOutcomeV1::Accepted)
        return fail(out_report, LinkControlIssueV1::Signature,
                    Status::InvalidField);
    if (out == nullptr ||
        !link_control_signature_is_canonical_v1(parsed.signature.data()))
        return fail(out_report, LinkControlIssueV1::Signature,
                    Status::InvalidField);
    *out = parsed_value;
    if (out_report != nullptr) {
        out_report->status = Status::Ok;
        out_report->issue = LinkControlIssueV1::None;
        out_report->proposal = link::LinkProposalSupportV1::Supported;
    }
    return Status::Ok;
}

Status decode_link_ready_v1(
    const std::uint8_t* bytes, std::size_t size,
    const LinkReadyExpectationsV1& expected, P256PointValidatorV1 validate_point,
    void* validator_context, LinkControlSignatureVerifierV1 verify_signature,
    void* verifier_context, LinkControlDecodeReportV1* out_report,
    link::LinkReadyV1* out) noexcept
{
    if (verify_signature == nullptr)
        return fail(out_report, LinkControlIssueV1::None, Status::InvalidField);
    LinkControlParsedV1 parsed{};
    link::LinkReadyV1 value{};
    const auto parsed_status = parse_link_ready_v1(
        bytes, size, expected, validate_point, validator_context, out_report,
        &parsed, &value);
    if (parsed_status != Status::Ok) return parsed_status;
    const auto outcome =
        verify_signature(verifier_context, parsed.signer_public_key.data(),
                         parsed.digest.data(), parsed.signature.data())
            ? LinkControlVerificationOutcomeV1::Accepted
            : LinkControlVerificationOutcomeV1::Rejected;
    return accept_link_ready_v1(parsed, value, outcome, out_report, out);
}

Status hash_link_negotiated_result_v1(const std::uint8_t* bytes,
                                      std::size_t size,
                                      std::array<std::uint8_t, 32>* out_hash) noexcept
{
    if (bytes == nullptr || out_hash == nullptr || size == 0)
        return Status::InvalidField;
    *out_hash = domain_hash(link::kLinkNegotiatedResultHashDomainV1, bytes, size);
    return Status::Ok;
}

} // namespace flynes::session::wire
