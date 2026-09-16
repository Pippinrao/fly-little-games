#include "link_hello.hpp"

#include "p256_point.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <cstring>

namespace flynes::session::wire {
namespace {

using link::kLinkHelloDigestDomainV1;
using link::kLinkHelloObjectHashDomainV1;
using link::kLinkHelloPretagSizeV1;
using link::kLinkHelloSizeV1;

constexpr char kIdentityKeyIdDomainV1[] = "flynes-identity-key-id-v1";

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

/* Canonical low-S, exactly as the 0x0212 binding codec enforces it. */
bool canonical_signature(const std::uint8_t signature[64]) noexcept
{
    static constexpr std::array<std::uint8_t, 32> order{{
        0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
        0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,
        0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51}};
    static constexpr std::array<std::uint8_t, 32> half_order{{
        0x7f,0xff,0xff,0xff,0x80,0x00,0x00,0x00,
        0x7f,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        0xde,0x73,0x7d,0x56,0xd3,0x8b,0xcf,0x42,
        0x79,0xdc,0xe5,0x61,0x7e,0x31,0x92,0xa8}};
    return nonzero(signature, 32) &&
           std::memcmp(signature, order.data(), 32) < 0 &&
           nonzero(signature + 32, 32) &&
           std::memcmp(signature + 32, half_order.data(), 32) <= 0;
}

void put_be16(std::uint8_t* out, std::uint16_t value) noexcept
{
    out[0] = static_cast<std::uint8_t>(value >> 8);
    out[1] = static_cast<std::uint8_t>(value & 0xff);
}

void put_be64(std::uint8_t* out, std::uint64_t value) noexcept
{
    for (int index = 0; index < 8; ++index)
        out[index] = static_cast<std::uint8_t>(
            value >> ((7 - index) * 8));
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

/* Fills every field of the value from a well-formed LINK_HELLO pretag. Performs
 * no legality checks: callers validate the bytes first. */
void parse_pretag(const std::uint8_t* bytes, link::LinkHelloV1* out) noexcept
{
    out->version = be16(bytes);
    out->sender_role = static_cast<PairRoleV1>(bytes[8]);
    out->receiver_role = static_cast<PairRoleV1>(bytes[9]);
    out->phase = static_cast<link::LinkPhaseV1>(bytes[10]);
    copy_out(bytes + 16, 16, out->session_id.data());
    copy_out(bytes + 32, 16, out->link_id.data());
    copy_out(bytes + link::kLinkHelloChannelIdOffsetV1, 16,
             out->channel_id.data());
    out->connection_generation =
        be64(bytes + link::kLinkHelloConnectionGenerationOffsetV1);
    out->link_generation = be64(bytes + link::kLinkHelloLinkGenerationOffsetV1);
    out->wire_major = be16(bytes + link::kLinkHelloWireMajorOffsetV1);
    out->wire_minor = be16(bytes + link::kLinkHelloWireMajorOffsetV1 + 2);
    out->capability_bits = be16(bytes + link::kLinkHelloCapabilityOffsetV1);
    out->critical_extension_mask =
        be16(bytes + link::kLinkHelloCriticalExtensionOffsetV1);
    out->determinism_profile = bytes[link::kLinkHelloDeterminismOffsetV1];
    out->core_state_format = bytes[link::kLinkHelloCoreStateFormatOffsetV1];
    copy_out(bytes + link::kLinkHelloSelectedPlanHashOffsetV1, 32,
             out->selected_plan_hash.data());
    copy_out(bytes + link::kLinkHelloEndpointOfferHashOffsetV1, 32,
             out->endpoint_offer_hash.data());
    copy_out(bytes + link::kLinkHelloPairTranscriptOffsetV1, 32,
             out->pair_transcript_object_hash.data());
    copy_out(bytes + link::kLinkHelloBindingHashOffsetV1, 32,
             out->session_signing_binding_hash.data());
    copy_out(bytes + link::kLinkHelloIdentityRefOffsetV1, 2,
             out->identity_verifier_ref.version.data());
    copy_out(bytes + link::kLinkHelloIdentityRefOffsetV1 + 8, 32,
             out->identity_verifier_ref.identity_key_id.data());
    copy_out(bytes + link::kLinkHelloIdentityRefOffsetV1 + 40, 65,
             out->identity_verifier_ref.identity_public_key.data());
    copy_out(bytes + link::kLinkHelloSessionSigningKeyOffsetV1, 65,
             out->session_signing_public_key.data());
}

} // namespace

bool link_control_signature_is_canonical_v1(
    const std::uint8_t signature[64]) noexcept
{
    return canonical_signature(signature);
}

std::array<std::uint8_t, 32> link_identity_key_id_v1(
    const std::uint8_t public_key_x963[65]) noexcept
{
    return domain_hash(kIdentityKeyIdDomainV1, public_key_x963, 65);
}

Status build_link_hello_pretag_v1(
    const link::LinkHelloV1& value, P256PointValidatorV1 validate_point,
    void* validator_context,
    std::array<std::uint8_t, kLinkHelloPretagSizeV1>* out_pretag,
    std::array<std::uint8_t, 32>* out_digest) noexcept
{
    if (out_pretag == nullptr || out_digest == nullptr ||
        validate_point == nullptr)
        return Status::InvalidField;
    if (value.version != 1) return Status::UnknownEnum;
    if (!valid_role(value.sender_role) || !valid_role(value.receiver_role))
        return Status::UnknownEnum;
    if (!link::link_roles_are_mirrored_v1(value.sender_role,
                                          value.receiver_role))
        return Status::InvalidField;
    if (value.phase != link::LinkPhaseV1::Initial) return Status::InvalidField;

    /* This release advertises DUAL only. A STREAM bit may never be emitted, and
     * an unknown critical bit is always fatal. */
    if (value.capability_bits == 0 ||
        (value.capability_bits &
         static_cast<std::uint16_t>(~link::kLinkSupportedCapabilityMaskV1)) != 0)
        return Status::InvalidField;
    const auto critical =
        link::check_critical_extension_mask_v1(value.critical_extension_mask);
    if (critical != Status::Ok) return critical;

    if (value.wire_major != 2 || value.wire_minor != 0)
        return Status::InvalidField;
    if (value.identity_verifier_ref.version[0] != 0 ||
        value.identity_verifier_ref.version[1] != 1)
        return Status::InvalidField;
    if (!zeros(value.identity_verifier_ref.reserved_zero0.data(),
               value.identity_verifier_ref.reserved_zero0.size()) ||
        !zeros(value.identity_verifier_ref.reserved_zero1.data(),
               value.identity_verifier_ref.reserved_zero1.size()))
        return Status::NonzeroReserved;

    if (!nonzero(value.session_id.data(), value.session_id.size()) ||
        !nonzero(value.link_id.data(), value.link_id.size()) ||
        !nonzero(value.channel_id.data(), value.channel_id.size()))
        return Status::InvalidField;
    if (value.connection_generation == 0 || value.link_generation == 0)
        return Status::InvalidField;
    if (!nonzero(value.selected_plan_hash.data(),
                 value.selected_plan_hash.size()) ||
        !nonzero(value.endpoint_offer_hash.data(),
                 value.endpoint_offer_hash.size()) ||
        !nonzero(value.pair_transcript_object_hash.data(),
                 value.pair_transcript_object_hash.size()) ||
        !nonzero(value.session_signing_binding_hash.data(),
                 value.session_signing_binding_hash.size()))
        return Status::InvalidField;

    const auto* identity_key = value.identity_verifier_ref.identity_public_key.data();
    if (!nonzero(value.identity_verifier_ref.identity_key_id.data(),
                 value.identity_verifier_ref.identity_key_id.size()))
        return Status::InvalidField;
    if (value.identity_verifier_ref.identity_public_key ==
        value.session_signing_public_key)
        return Status::InvalidField;
    if (!validate_point(validator_context, identity_key) ||
        !validate_point(validator_context,
                        value.session_signing_public_key.data()))
        return Status::InvalidField;
    const auto key_id =
        domain_hash(kIdentityKeyIdDomainV1, identity_key, 65);
    if (!same(key_id.data(),
              value.identity_verifier_ref.identity_key_id.data(), 32))
        return Status::InvalidField;

    out_pretag->fill(0);
    auto* out = out_pretag->data();
    put_be16(out, 1);
    out[8] = static_cast<std::uint8_t>(value.sender_role);
    out[9] = static_cast<std::uint8_t>(value.receiver_role);
    out[10] = static_cast<std::uint8_t>(value.phase);
    copy_out(value.session_id.data(), 16, out + 16);
    copy_out(value.link_id.data(), 16, out + 32);
    copy_out(value.channel_id.data(), 16,
             out + link::kLinkHelloChannelIdOffsetV1);
    put_be64(out + link::kLinkHelloConnectionGenerationOffsetV1,
             value.connection_generation);
    put_be64(out + link::kLinkHelloLinkGenerationOffsetV1,
             value.link_generation);
    put_be16(out + link::kLinkHelloWireMajorOffsetV1, value.wire_major);
    put_be16(out + link::kLinkHelloWireMajorOffsetV1 + 2, value.wire_minor);
    put_be16(out + link::kLinkHelloCapabilityOffsetV1, value.capability_bits);
    put_be16(out + link::kLinkHelloCriticalExtensionOffsetV1,
             value.critical_extension_mask);
    out[link::kLinkHelloDeterminismOffsetV1] = value.determinism_profile;
    out[link::kLinkHelloCoreStateFormatOffsetV1] = value.core_state_format;
    copy_out(value.selected_plan_hash.data(), 32,
             out + link::kLinkHelloSelectedPlanHashOffsetV1);
    copy_out(value.endpoint_offer_hash.data(), 32,
             out + link::kLinkHelloEndpointOfferHashOffsetV1);
    copy_out(value.pair_transcript_object_hash.data(), 32,
             out + link::kLinkHelloPairTranscriptOffsetV1);
    copy_out(value.session_signing_binding_hash.data(), 32,
             out + link::kLinkHelloBindingHashOffsetV1);
    out[link::kLinkHelloIdentityRefOffsetV1] = 0;
    out[link::kLinkHelloIdentityRefOffsetV1 + 1] = 1;
    copy_out(value.identity_verifier_ref.identity_key_id.data(), 32,
             out + link::kLinkHelloIdentityRefOffsetV1 + 8);
    copy_out(identity_key, 65, out + link::kLinkHelloIdentityRefOffsetV1 + 40);
    copy_out(value.session_signing_public_key.data(), 65,
             out + link::kLinkHelloSessionSigningKeyOffsetV1);
    *out_digest = domain_hash(kLinkHelloDigestDomainV1, out, out_pretag->size());
    return Status::Ok;
}

Status finish_link_hello_v1(
    const std::array<std::uint8_t, kLinkHelloPretagSizeV1>& pretag,
    const std::array<std::uint8_t, 64>& signature,
    std::array<std::uint8_t, kLinkHelloSizeV1>* out_bytes,
    link::LinkHelloV1* out_value) noexcept
{
    if (out_bytes == nullptr || out_value == nullptr ||
        !canonical_signature(signature.data()))
        return Status::InvalidField;
    std::copy(pretag.begin(), pretag.end(), out_bytes->begin());
    std::copy(signature.begin(), signature.end(),
              out_bytes->begin() + static_cast<std::ptrdiff_t>(kLinkHelloPretagSizeV1));
    link::LinkHelloV1 parsed{};
    parse_pretag(out_bytes->data(), &parsed);
    parsed.signature = signature;
    parsed.digest = domain_hash(kLinkHelloDigestDomainV1, out_bytes->data(),
                                kLinkHelloPretagSizeV1);
    parsed.object_hash = domain_hash(kLinkHelloObjectHashDomainV1,
                                     out_bytes->data(), out_bytes->size());
    *out_value = parsed;
    return Status::Ok;
}

/*
 * Stage 1. Every check that does not need the sender's signature lives here, so
 * a peer message can be parsed and staged while the asynchronous verification is
 * still outstanding. Nothing here touches a provider.
 */
Status parse_link_hello_v1(
    const std::uint8_t* bytes, std::size_t size,
    const LinkHelloExpectationsV1& expected, P256PointValidatorV1 validate_point,
    void* validator_context, LinkControlDecodeReportV1* out_report,
    LinkControlParsedV1* out_parsed, link::LinkHelloV1* out) noexcept
{
    if (out_report != nullptr) {
        out_report->status = Status::Ok;
        out_report->issue = LinkControlIssueV1::None;
        out_report->proposal = link::LinkProposalSupportV1::Supported;
    }
    if (out_parsed != nullptr) *out_parsed = LinkControlParsedV1{};
    if (size < kLinkHelloSizeV1)
        return fail(out_report, LinkControlIssueV1::Size, Status::Truncated);
    if (size > kLinkHelloSizeV1)
        return fail(out_report, LinkControlIssueV1::Size, Status::Trailing);
    if (bytes == nullptr || out == nullptr || out_report == nullptr ||
        out_parsed == nullptr || validate_point == nullptr)
        return fail(out_report, LinkControlIssueV1::None, Status::InvalidField);
    if (!nonzero(expected.selected_plan_hash.data(),
                 expected.selected_plan_hash.size()) ||
        !nonzero(expected.endpoint_offer_hash.data(),
                 expected.endpoint_offer_hash.size()) ||
        !nonzero(expected.pair_transcript_object_hash.data(),
                 expected.pair_transcript_object_hash.size()) ||
        !nonzero(expected.peer_binding_hash.data(),
                 expected.peer_binding_hash.size()) ||
        !nonzero(expected.peer_identity_key_id.data(),
                 expected.peer_identity_key_id.size()))
        return fail(out_report, LinkControlIssueV1::ExpectedField,
                    Status::InvalidField);

    if (!valid_role(expected.local_role))
        return fail(out_report, LinkControlIssueV1::Discriminant,
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

    if (bytes[10] != static_cast<std::uint8_t>(link::LinkPhaseV1::Initial))
        return fail(out_report, LinkControlIssueV1::Discriminant,
                    bytes[10] ==
                            static_cast<std::uint8_t>(link::LinkPhaseV1::Reconcile)
                        ? Status::InvalidField
                        : Status::UnknownEnum);
    if (!zeros(bytes + 11, 5))
        return fail(out_report, LinkControlIssueV1::Reserved,
                    Status::NonzeroReserved);

    if (!same(bytes + 16, expected.session_id.data(), 16) ||
        !same(bytes + 32, expected.link_id.data(), 16))
        return fail(out_report, LinkControlIssueV1::ExpectedField,
                    Status::InvalidField);
    if (!same(bytes + link::kLinkHelloChannelIdOffsetV1,
              expected.channel_id.data(), 16))
        return fail(out_report, LinkControlIssueV1::ExpectedField,
                    Status::InvalidField);
    if (be64(bytes + link::kLinkHelloConnectionGenerationOffsetV1) !=
            expected.connection_generation ||
        be64(bytes + link::kLinkHelloLinkGenerationOffsetV1) !=
            expected.link_generation)
        return fail(out_report, LinkControlIssueV1::Generation,
                    Status::InvalidField);

    if (be16(bytes + link::kLinkHelloWireMajorOffsetV1) != 2 ||
        be16(bytes + link::kLinkHelloWireMajorOffsetV1 + 2) != 0)
        return fail(out_report, LinkControlIssueV1::Version,
                    Status::InvalidField);

    const auto support =
        link::evaluate_link_proposal_v1(
            be16(bytes + link::kLinkHelloCapabilityOffsetV1));
    if (support != link::LinkProposalSupportV1::Supported) {
        if (out_report != nullptr) out_report->proposal = support;
        return fail(out_report,
                    LinkControlIssueV1::Capability,
                    support == link::LinkProposalSupportV1::UnknownCriticalCapability
                        ? Status::UnknownCriticalTag
                        : Status::InvalidField);
    }
    const auto critical = link::check_critical_extension_mask_v1(
        be16(bytes + link::kLinkHelloCriticalExtensionOffsetV1));
    if (critical != Status::Ok)
        return fail(out_report, LinkControlIssueV1::Capability, critical);
    if (!zeros(bytes + link::kLinkHelloReservedZero2OffsetV1, 2))
        return fail(out_report, LinkControlIssueV1::Reserved,
                    Status::NonzeroReserved);

    if (!same(bytes + link::kLinkHelloSelectedPlanHashOffsetV1,
              expected.selected_plan_hash.data(), 32) ||
        !same(bytes + link::kLinkHelloEndpointOfferHashOffsetV1,
              expected.endpoint_offer_hash.data(), 32))
        return fail(out_report, LinkControlIssueV1::ExpectedField,
                    Status::InvalidField);
    if (!same(bytes + link::kLinkHelloPairTranscriptOffsetV1,
              expected.pair_transcript_object_hash.data(), 32))
        return fail(out_report, LinkControlIssueV1::ExpectedField,
                    Status::InvalidField);
    if (!same(bytes + link::kLinkHelloBindingHashOffsetV1,
              expected.peer_binding_hash.data(), 32))
        return fail(out_report, LinkControlIssueV1::BindingRef,
                    Status::InvalidField);

    /* identity_verifier_ref: the long-term identity key that must authenticate
     * the session signing key carried below. */
    const auto* identity_ref = bytes + link::kLinkHelloIdentityRefOffsetV1;
    if (identity_ref[0] != 0 || identity_ref[1] != 1)
        return fail(out_report, LinkControlIssueV1::IdentityRef,
                    Status::InvalidField);
    if (!zeros(identity_ref + 2, 6) || !zeros(identity_ref + 105, 7))
        return fail(out_report, LinkControlIssueV1::Reserved,
                    Status::NonzeroReserved);
    if (!same(identity_ref + 8, expected.peer_identity_key_id.data(), 32))
        return fail(out_report, LinkControlIssueV1::IdentityRef,
                    Status::InvalidField);
    const auto* identity_key = identity_ref + 40;
    if (!validate_point(validator_context, identity_key))
        return fail(out_report, LinkControlIssueV1::IdentityRef,
                    Status::InvalidField);
    const auto derived_key_id =
        domain_hash(kIdentityKeyIdDomainV1, identity_key, 65);
    if (!same(identity_ref + 8, derived_key_id.data(), 32))
        return fail(out_report, LinkControlIssueV1::IdentityRef,
                    Status::InvalidField);
    if (!same(identity_key, expected.peer_identity_public_key.data(), 65))
        return fail(out_report, LinkControlIssueV1::IdentityRef,
                    Status::InvalidField);

    /* The session signing key may only be one the accepted binding already
     * authenticated, and it can never be the long-term identity key itself. */
    const auto* signing_key =
        bytes + link::kLinkHelloSessionSigningKeyOffsetV1;
    if (!same(signing_key, expected.peer_session_signing_public_key.data(), 65) ||
        !validate_point(validator_context, signing_key) ||
        same(signing_key, identity_key, 65))
        return fail(out_report, LinkControlIssueV1::BindingRef,
                    Status::InvalidField);
    if (!zeros(bytes + link::kLinkHelloReservedTailOffsetV1, 27))
        return fail(out_report, LinkControlIssueV1::Reserved,
                    Status::NonzeroReserved);

    if (!canonical_signature(bytes + link::kLinkHelloSignatureOffsetV1))
        return fail(out_report, LinkControlIssueV1::Signature,
                    Status::InvalidField);

    copy_out(signing_key, 65, out_parsed->signer_public_key.data());
    out_parsed->digest =
        domain_hash(kLinkHelloDigestDomainV1, bytes, kLinkHelloPretagSizeV1);
    copy_out(bytes + link::kLinkHelloSignatureOffsetV1, 64,
             out_parsed->signature.data());

    link::LinkHelloV1 parsed{};
    parse_pretag(bytes, &parsed);
    parsed.signature = out_parsed->signature;
    parsed.digest = out_parsed->digest;
    parsed.object_hash =
        domain_hash(kLinkHelloObjectHashDomainV1, bytes, kLinkHelloSizeV1);
    *out = parsed;
    return Status::Ok;
}

Status accept_link_hello_v1(const LinkControlParsedV1& parsed,
                            const link::LinkHelloV1& parsed_value,
                            LinkControlVerificationOutcomeV1 outcome,
                            LinkControlDecodeReportV1* out_report,
                            link::LinkHelloV1* out) noexcept
{
    if (outcome != LinkControlVerificationOutcomeV1::Accepted)
        return fail(out_report, LinkControlIssueV1::Signature,
                    Status::InvalidField);
    if (out == nullptr || !canonical_signature(parsed.signature.data()))
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

Status decode_link_hello_v1(
    const std::uint8_t* bytes, std::size_t size,
    const LinkHelloExpectationsV1& expected, P256PointValidatorV1 validate_point,
    void* validator_context, LinkControlSignatureVerifierV1 verify_signature,
    void* verifier_context, LinkControlDecodeReportV1* out_report,
    link::LinkHelloV1* out) noexcept
{
    if (verify_signature == nullptr)
        return fail(out_report, LinkControlIssueV1::None, Status::InvalidField);
    LinkControlParsedV1 parsed{};
    link::LinkHelloV1 value{};
    const auto parsed_status = parse_link_hello_v1(
        bytes, size, expected, validate_point, validator_context, out_report,
        &parsed, &value);
    if (parsed_status != Status::Ok) return parsed_status;
    const auto outcome =
        verify_signature(verifier_context, parsed.signer_public_key.data(),
                         parsed.digest.data(), parsed.signature.data())
            ? LinkControlVerificationOutcomeV1::Accepted
            : LinkControlVerificationOutcomeV1::Rejected;
    const auto accepted_status =
        accept_link_hello_v1(parsed, value, outcome, out_report, out);
    if (accepted_status != Status::Ok)
        return fail(out_report, LinkControlIssueV1::Signature,
                    Status::InvalidField);
    return Status::Ok;
}

Status link_control_verify_request_v1(
    const LinkControlParsedV1& parsed, const char* digest_domain,
    LinkControlVerifyRequestV1* out_request) noexcept
{
    if (out_request == nullptr || digest_domain == nullptr)
        return Status::InvalidField;
    out_request->public_key_x963 = parsed.signer_public_key;
    out_request->digest = parsed.digest;
    out_request->signature = parsed.signature;
    const auto* begin =
        reinterpret_cast<const std::uint8_t*>(digest_domain);
    out_request->domain.assign(begin,
                               begin + std::strlen(digest_domain));
    return Status::Ok;
}

} // namespace flynes::session::wire
