/*
 * Task 10 / step 2: the two DUAL seam pieces the engine needs before it can own
 * a DUAL run.
 *
 *   1. the canonical input unit on the wire: encoder/decoder for the frozen
 *      154-byte CanonicalInputBundleV1 record (message tag 0xFF02), including
 *      the three asymmetries between that record and the in-memory bundle;
 *   2. the runtime port adapter: the frozen `dual::DualRuntimePort` implemented
 *      over the public C provider table, proved to be a faithful pass-through.
 *
 * Everything here is asserted against the release's own frozen validator
 * (`wire::check`), so "the encoder is right" means "the codec that will receive
 * these bytes accepts them".
 */

#include "dual/canonical_input_wire.hpp"
#include "dual/dual_runtime_adapter.hpp"
#include "wire/session_codec.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

namespace dual = flynes::session::dual;
namespace wire = flynes::session::wire;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::array<std::uint8_t, 16> id16(std::uint8_t first)
{
    std::array<std::uint8_t, 16> value{};
    value[0] = first;
    return value;
}

std::array<std::uint8_t, 32> id32(std::uint8_t first)
{
    std::array<std::uint8_t, 32> value{};
    value[0] = first;
    return value;
}

dual::DualInputKeyV1 make_key() noexcept
{
    dual::DualInputKeyV1 key{};
    key.session_id = id16(0x11);
    key.branch_id = id16(0x22);
    key.timeline_epoch = 7;
    key.seat_revision = 3;
    key.frame_index = 41;
    return key;
}

/* A canonical bundle for one seat, built through the release's own builder. */
dual::DualInputBundleV1 make_bundle(std::uint32_t mask, std::uint8_t seat,
                                    std::uint8_t owner_first) noexcept
{
    dual::DualPortInputArrayV1 samples{};
    for (std::uint32_t port = 0; port < dual::kDualPortCountV1; ++port)
    {
        samples[port].mask = mask;
        samples[port].sequence = 10 + port;
    }
    dual::DualInputBundleV1 bundle{};
    const auto status = dual::canonical_input_build_v1(
        make_key(), seat, id32(owner_first), samples, &bundle);
    check(status == dual::DualInputStatusV1::Ok,
          "the fixture bundle is canonical");
    return bundle;
}

dual::DualInputWireContextV1 make_context() noexcept
{
    dual::DualInputWireContextV1 context{};
    context.authority_term = 5;
    context.mode_generation = 9;
    context.batch_sequence = 12;
    return context;
}

/* ------------------------------------------------------ 1. the wire record */

void the_canonical_unit_round_trips_through_the_frozen_record()
{
    const auto bundle = make_bundle(0x51u, 0, 0x55);
    const auto context = make_context();
    std::uint8_t bytes[dual::kDualInputWireBytesV1] = {};

    check(dual::encode_dual_input_bundle_v1(bundle, context, bytes) ==
              dual::DualInputWireStatusV1::Ok,
          "a canonical bundle encodes");
    check(bytes[0] == 0u && bytes[1] == 1u, "the record carries version 1");
    check(bytes[82] == 0u, "no port is marked predicted");

    /* The release's own validator is the authority for these bytes. */
    std::uint8_t hash[32] = {};
    check(wire::check("CanonicalInputBundleV1", bytes,
                      dual::kDualInputWireBytesV1, hash) == wire::Status::Ok,
          "the encoder's output is accepted by the frozen codec");

    dual::DualInputBundleV1 decoded{};
    check(dual::decode_dual_input_bundle_v1(bytes, dual::kDualInputWireBytesV1,
                                            0, id32(0x55), &decoded) ==
              dual::DualInputWireStatusV1::Ok,
          "the record decodes");
    check(decoded == bundle,
          "the decoded bundle is byte identical to the encoded one");
    check(decoded.ports[0].mask == 0x51u && decoded.ports[0].input_sequence == 10u,
          "the port sample survives the round trip");
}

void a_predicted_port_keeps_its_marker_on_the_wire()
{
    auto bundle = make_bundle(0x0Fu, 1, 0x77);
    bundle.predicted_port_mask = 0x2u;
    auto context = make_context();
    context.stated[1] = true;
    context.source_kind[1] = dual::DualPortSourceKindV1::Predicted;

    std::uint8_t bytes[dual::kDualInputWireBytesV1] = {};
    check(dual::encode_dual_input_bundle_v1(bundle, context, bytes) ==
              dual::DualInputWireStatusV1::Ok,
          "a bundle with one predicted port encodes");
    check(bytes[82] == 0x02u,
          "the wire prediction mask follows the predicted source_kind");

    dual::DualInputBundleV1 decoded{};
    check(dual::decode_dual_input_bundle_v1(bytes, dual::kDualInputWireBytesV1,
                                            1, id32(0x77), &decoded) ==
              dual::DualInputWireStatusV1::Ok,
          "a predicted record decodes");
    check(decoded.predicted_port_mask == 0x2u,
          "the prediction marker is derived back from source_kind");
    check(decoded == bundle, "the predicted bundle survives the round trip");

    /* A prediction marker that disagrees with the stated kind is refused: the
     * receiver derives reality from source_kind, so a contradiction would let
     * one side call a prediction real. */
    auto lying = context;
    lying.source_kind[1] = dual::DualPortSourceKindV1::RealSample;
    check(dual::encode_dual_input_bundle_v1(bundle, lying, bytes) ==
              dual::DualInputWireStatusV1::InvalidContext,
          "a prediction marker that contradicts source_kind is refused");
}

void the_wire_cannot_name_its_own_owner_or_seat()
{
    const auto bundle = make_bundle(0x51u, 0, 0x55);
    std::uint8_t bytes[dual::kDualInputWireBytesV1] = {};
    check(dual::encode_dual_input_bundle_v1(bundle, make_context(), bytes) ==
              dual::DualInputWireStatusV1::Ok,
          "the fixture encodes");

    dual::DualInputBundleV1 decoded{};
    check(dual::decode_dual_input_bundle_v1(bytes, dual::kDualInputWireBytesV1,
                                            2, id32(0x99), &decoded) ==
              dual::DualInputWireStatusV1::Ok,
          "the record decodes against a caller supplied binding");
    check(decoded.logical_seat == 2 &&
              decoded.owner_signing_key_id == id32(0x99),
          "the seat and the owner key come from the authenticated caller, never "
          "from the bytes");
    check(decoded.key.session_id == id16(0x11) &&
              decoded.key.frame_index == 41 &&
              decoded.key.seat_revision == 3,
          "the record still carries its own key tuple");
}

void malformed_records_are_refused_by_the_frozen_validator()
{
    const auto bundle = make_bundle(0x51u, 0, 0x55);
    std::uint8_t bytes[dual::kDualInputWireBytesV1] = {};
    check(dual::encode_dual_input_bundle_v1(bundle, make_context(), bytes) ==
              dual::DualInputWireStatusV1::Ok,
          "the fixture encodes");

    dual::DualInputBundleV1 decoded{};

    auto short_record = std::array<std::uint8_t, dual::kDualInputWireBytesV1>{};
    std::memcpy(short_record.data(), bytes, short_record.size());
    check(dual::decode_dual_input_bundle_v1(short_record.data(),
                                            dual::kDualInputWireBytesV1 - 1, 0,
                                            id32(0x55), &decoded) ==
              dual::DualInputWireStatusV1::InvalidArgument,
          "a record of the wrong length is refused");

    auto reserved = std::array<std::uint8_t, dual::kDualInputWireBytesV1>{};
    std::memcpy(reserved.data(), bytes, reserved.size());
    reserved[83] = 1u;
    check(dual::decode_dual_input_bundle_v1(reserved.data(), reserved.size(), 0,
                                            id32(0x55), &decoded) ==
              dual::DualInputWireStatusV1::InvalidBytes,
          "a non-zero reserved byte is refused");

    auto unknown_kind = std::array<std::uint8_t, dual::kDualInputWireBytesV1>{};
    std::memcpy(unknown_kind.data(), bytes, unknown_kind.size());
    unknown_kind[90] = 0u;
    check(dual::decode_dual_input_bundle_v1(unknown_kind.data(),
                                            unknown_kind.size(), 0, id32(0x55),
                                            &decoded) ==
              dual::DualInputWireStatusV1::InvalidBytes,
          "an unknown source_kind is refused");

    /* A conflicted d-pad state is not transmittable at all: the canonical form
     * predicate rejects it, so the encoder must refuse rather than emit it. */
    auto conflicted = bundle;
    conflicted.ports[0].mask = 0x30u; /* UP | DOWN */
    check(dual::encode_dual_input_bundle_v1(conflicted, make_context(),
                                            bytes) ==
              dual::DualInputWireStatusV1::InvalidBundle,
          "a bundle carrying an impossible d-pad state never leaves the send "
          "path");

    auto missing_facts = make_context();
    missing_facts.authority_term = 0;
    check(dual::encode_dual_input_bundle_v1(bundle, missing_facts, bytes) ==
              dual::DualInputWireStatusV1::InvalidContext,
          "the session facts the record needs are required");
}

/* ------------------------------------------------------- 2. runtime adapter */

struct FakeAbiRuntime
{
    fly_session_result_v2 load_result = FLY_SESSION_V2_OK;
    fly_session_result_v2 step_result = FLY_SESSION_V2_OK;
    fly_session_result_v2 export_result = FLY_SESSION_V2_OK;
    fly_session_result_v2 import_result = FLY_SESSION_V2_OK;
    fly_session_result_v2 digest_result = FLY_SESSION_V2_OK;
    bool export_overclaims = false;

    int loads = 0;
    int steps = 0;
    int exports = 0;
    int imports = 0;
    int digests = 0;

    fly_session_dual_content_ref_v2 last_content{};
    fly_session_dual_input_bundle_v2 last_input{};
    fly_session_dual_frame_outcome_v2 step_outcome{};
    std::array<std::uint8_t, 32> last_export_hash{};
    std::size_t last_export_capacity = 0;
    std::uint8_t last_export_bytes[16] = {};
    std::size_t last_import_size = 0;
    std::uint64_t last_digest_frame = 0;
    fly_session_dual_state_digest_v2 digest_value{};
};

fly_session_result_v2 abi_load(void* context,
                              const fly_session_dual_content_ref_v2* content)
{
    auto* fake = static_cast<FakeAbiRuntime*>(context);
    ++fake->loads;
    fake->last_content = *content;
    return fake->load_result;
}

fly_session_result_v2 abi_step(void* context,
                              const fly_session_dual_input_bundle_v2* input,
                              fly_session_dual_frame_outcome_v2* out)
{
    auto* fake = static_cast<FakeAbiRuntime*>(context);
    ++fake->steps;
    fake->last_input = *input;
    if (fake->step_result == FLY_SESSION_V2_OK)
        *out = fake->step_outcome;
    else
        /* A misbehaving provider that writes on failure must not be believed. */
        out->frame_index = 0xDEADBEEFull;
    return fake->step_result;
}

fly_session_result_v2 abi_export(void* context, uint8_t* out, size_t capacity,
                                size_t* out_written, uint8_t hash_out[32])
{
    auto* fake = static_cast<FakeAbiRuntime*>(context);
    ++fake->exports;
    fake->last_export_capacity = capacity;
    const std::size_t readable = capacity < sizeof(fake->last_export_bytes)
                                     ? capacity
                                     : sizeof(fake->last_export_bytes);
    std::memcpy(fake->last_export_bytes, out, readable);
    const std::size_t written = fake->export_overclaims ? capacity + 1u : 8u;
    for (std::size_t index = 0; index < written && index < capacity; ++index)
        out[index] = static_cast<std::uint8_t>(0xA0u + index);
    *out_written = written;
    std::memcpy(hash_out, fake->last_export_hash.data(), 32u);
    return fake->export_result;
}

fly_session_result_v2 abi_import(void* context, const uint8_t* bytes, size_t size)
{
    auto* fake = static_cast<FakeAbiRuntime*>(context);
    ++fake->imports;
    fake->last_import_size = size;
    (void)bytes;
    return fake->import_result;
}

fly_session_result_v2 abi_digest(void* context, uint64_t frame_index,
                                fly_session_dual_state_digest_v2* out)
{
    auto* fake = static_cast<FakeAbiRuntime*>(context);
    ++fake->digests;
    fake->last_digest_frame = frame_index;
    if (fake->digest_result == FLY_SESSION_V2_OK)
        *out = fake->digest_value;
    return fake->digest_result;
}

fly_session_dual_runtime_port_v2 make_abi_port(FakeAbiRuntime* fake) noexcept
{
    fly_session_dual_runtime_port_v2 port{};
    port.struct_size = FLY_SESSION_DUAL_RUNTIME_PORT_V2_SIZE;
    port.abi_version = FLY_SESSION_ABI_VERSION_2;
    port.context = fake;
    port.retain = [](void*) {};
    port.release = [](void*) {};
    port.load = abi_load;
    port.step = abi_step;
    port.export_state = abi_export;
    port.import_state = abi_import;
    port.state_digest = abi_digest;
    return port;
}

void the_adapter_is_a_faithful_pass_through()
{
    FakeAbiRuntime fake;
    fake.last_export_hash[0] = 0x5A;
    fake.digest_value.state[0] = 0x11;
    fake.digest_value.frame[0] = 0x22;
    fake.digest_value.pcm[0] = 0x33;
    fake.step_outcome.frame_index = 77;
    fake.step_outcome.honoured_port_mask = 0x3;
    fake.step_outcome.applied_input_sequence[0] = 10;
    fake.step_outcome.applied_input_sequence[1] = 11;

    const auto port = make_abi_port(&fake);
    dual::CAbiDualRuntimePortV1 adapter(&port);

    dual::DualContentRefV1 content{};
    content.session_id = id16(0x11);
    content.branch_id = id16(0x22);
    content.content_hash = id32(0x42);
    content.timeline_epoch = 7;
    check(adapter.load(content) == FLY_SESSION_V2_OK && fake.loads == 1,
          "load is forwarded exactly once");
    check(std::memcmp(fake.last_content.session_id, content.session_id.data(),
                      16u) == 0 &&
              std::memcmp(fake.last_content.content_hash,
                          content.content_hash.data(), 32u) == 0 &&
              fake.last_content.timeline_epoch == 7,
          "the content reference crosses the boundary byte for byte");

    const auto bundle = make_bundle(0x51u, 0, 0x55);
    dual::DualFrameOutcomeV1 outcome{};
    check(adapter.step(bundle, &outcome) == FLY_SESSION_V2_OK &&
              fake.steps == 1,
          "step is forwarded exactly once");
    check(fake.last_input.struct_size ==
                  FLY_SESSION_DUAL_INPUT_BUNDLE_V2_SIZE &&
              fake.last_input.frame_index == bundle.key.frame_index &&
              fake.last_input.logical_seat == bundle.logical_seat &&
              fake.last_input.predicted_port_mask ==
                  bundle.predicted_port_mask &&
              fake.last_input.ports[0].mask == bundle.ports[0].mask &&
              fake.last_input.ports[0].input_sequence ==
                  bundle.ports[0].input_sequence,
          "the canonical bundle crosses the boundary field for field");
    check(outcome.frame_index == 77 && outcome.honoured_port_mask == 0x3u &&
              outcome.applied_input_sequence[0] == 10u,
          "the provider's outcome is copied back verbatim");

    std::uint8_t state[16] = {};
    std::size_t written = 0;
    std::array<std::uint8_t, 32> state_hash{};
    check(adapter.export_state(state, sizeof(state), &written, &state_hash) ==
              FLY_SESSION_V2_OK &&
              written == 8u && state_hash[0] == 0x5Au,
          "export_state forwards the state bytes and the provider's hash");
    check(fake.last_export_capacity == sizeof(state),
          "the capability offered to the provider is the caller's real one");

    const std::uint8_t commit[4] = {1, 2, 3, 4};
    check(adapter.import_state(commit, sizeof(commit)) == FLY_SESSION_V2_OK &&
              fake.imports == 1 && fake.last_import_size == sizeof(commit),
          "import_state forwards the exact bytes and size");

    dual::DualStateDigestV1 digest{};
    check(adapter.state_digest(41, &digest) == FLY_SESSION_V2_OK &&
              fake.digests == 1 && fake.last_digest_frame == 41,
          "state_digest forwards the requested frame");
    check(digest.state[0] == 0x11u && digest.frame[0] == 0x22u &&
              digest.pcm[0] == 0x33u,
          "the provider's digest is copied back verbatim");
    check(adapter.forwarded() == 5u, "every forwarded call is counted");
}

void a_failing_provider_cannot_leave_a_result_behind()
{
    FakeAbiRuntime fake;
    fake.step_result = FLY_SESSION_V2_PROTOCOL_VIOLATION;
    fake.digest_result = FLY_SESSION_V2_STALE;
    fake.export_result = FLY_SESSION_V2_BUFFER_TOO_SMALL;
    fake.export_overclaims = true;

    const auto port = make_abi_port(&fake);
    dual::CAbiDualRuntimePortV1 adapter(&port);

    dual::DualFrameOutcomeV1 outcome{};
    outcome.frame_index = 0xABABABABull;
    check(adapter.step(make_bundle(0x01u, 0, 0x55), &outcome) ==
              FLY_SESSION_V2_PROTOCOL_VIOLATION,
          "a provider's failure code is returned unchanged");
    check(outcome.frame_index == 0xABABABABull,
          "a failing step cannot leave a half-written outcome behind");

    dual::DualStateDigestV1 digest{};
    digest.state[0] = 0xCD;
    check(adapter.state_digest(3, &digest) == FLY_SESSION_V2_STALE,
          "a provider's digest failure is returned unchanged");
    check(digest.state[0] == 0xCDu,
          "a failing digest call cannot leave a partial digest behind");

    std::uint8_t state[8] = {};
    std::size_t written = 99;
    std::array<std::uint8_t, 32> state_hash{};
    check(adapter.export_state(state, sizeof(state), &written, &state_hash) ==
              FLY_SESSION_V2_BUFFER_TOO_SMALL && written == 99u,
          "a failing export does not report a size");

    /* A provider that claims to have written more than it was offered is a
     * contract violation, not a result the scheduler may act on. */
    fake.export_result = FLY_SESSION_V2_OK;
    check(adapter.export_state(state, sizeof(state), &written, &state_hash) ==
              FLY_SESSION_V2_CONTRACT_VIOLATION,
          "an over-claiming export is refused");
}

void a_table_without_a_call_is_unavailable()
{
    FakeAbiRuntime fake;
    auto port = make_abi_port(&fake);
    port.step = nullptr;
    dual::CAbiDualRuntimePortV1 adapter(&port);

    dual::DualFrameOutcomeV1 outcome{};
    check(adapter.step(make_bundle(0x01u, 0, 0x55), &outcome) ==
              FLY_SESSION_V2_UNAVAILABLE,
          "a missing callback is unavailable rather than silently skipped");

    dual::CAbiDualRuntimePortV1 null_adapter(nullptr);
    check(null_adapter.load({}) == FLY_SESSION_V2_UNAVAILABLE,
          "an absent provider table is unavailable");
}

} // namespace

int main()
{
    the_canonical_unit_round_trips_through_the_frozen_record();
    a_predicted_port_keeps_its_marker_on_the_wire();
    the_wire_cannot_name_its_own_owner_or_seat();
    malformed_records_are_refused_by_the_frozen_validator();
    the_adapter_is_a_faithful_pass_through();
    a_failing_provider_cannot_leave_a_result_behind();
    a_table_without_a_call_is_unavailable();
    if (failures != 0)
    {
        std::fprintf(stderr, "nearby_dual_runtime_seam: %d failure(s)\n",
                     failures);
        return 1;
    }
    std::puts("nearby_dual_runtime_seam: PASS");
    return 0;
}
