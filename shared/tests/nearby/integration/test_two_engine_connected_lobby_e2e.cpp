/*
 * W0 two-engine loopback end-to-end acceptance (MVP-LOBBY).
 *
 * This file is grown in steps, and each step must leave the repository green.
 * Steps 1 and 2 are in:
 *
 *   step 1 (done)  two independent public engines come up side by side through the
 *                  carved harness, each projecting READY / IDLE with its own
 *                  approval-token action.
 *   step 2 (done)  the P-256 point table (re-validated by the repository's own
 *                  curve check and pinned against its shipped constants) and the
 *                  shared deterministic provider world.
 *   step 3 (done)  the loopback TRANSPORT, which is the only producer of externally
 *                  originated events, and the counted, bounded provider pump. Both
 *                  ends are on one link and each engine really writes its own first
 *                  GATT fragment; the fragments are not carried across yet, so this
 *                  step claims neither CONNECTED_LOBBY nor a self-driven teardown.
 *   step 4         byte-accurate GATT/discovery loopback.
 *   step 5         byte-accurate QUIC loopback (bind stream + Control).
 *   step 6         alternate pumping until BOTH engines project
 *                  FLY_SESSION_LINK_CONNECTED_LOBBY_V2.
 *
 * The deliverable this file is working towards is the MVP-LOBBY acceptance line:
 * two public engines, real byte-level loopback, no ROM, zero fabricated peer
 * bytes, both sides in CONNECTED_LOBBY. Until step 6 lands, this file makes no
 * claim about CONNECTED_LOBBY and the test names say so.
 */

#include "../harness/two_engine_loopback_fixture.hpp"

#include "wire/sha256.hpp"
#include "wire/p256_point.hpp"

#include <cstring>

namespace {

namespace wire = flynes::session::wire;

using flynes::session::loopback::EngineFixture;
using flynes::session::loopback::check;
using flynes::session::loopback::failures;
using flynes::session::loopback::find_action;

/*
 * step 1: two public engines are constructed and driven to their READY
 * projection independently. This is the foundation the later steps build on: it
 * proves the carved harness really instantiates two fully separate engines (two
 * port sets, two executives, two views) rather than sharing any state.
 */
void two_engines_come_up_independently()
{
    EngineFixture inviter;
    EngineFixture joiner;

    check(inviter.engine != nullptr && joiner.engine != nullptr,
          "two public engines are constructed side by side");
    check(inviter.engine != joiner.engine,
          "the two engines are distinct instances");

    inviter.platform.ready();
    joiner.platform.ready();
    inviter.executor.run_all();
    joiner.executor.run_all();

    std::vector<fly_session_action_descriptor_v2> inviter_actions;
    std::vector<fly_session_action_descriptor_v2> joiner_actions;
    const auto inviter_ready = inviter.snapshot(&inviter_actions);
    const auto joiner_ready = joiner.snapshot(&joiner_actions);

    check(inviter_ready.engine_state == FLY_SESSION_ENGINE_READY_V2 &&
              inviter_ready.link_state == FLY_SESSION_LINK_IDLE_V2 &&
              joiner_ready.engine_state == FLY_SESSION_ENGINE_READY_V2 &&
              joiner_ready.link_state == FLY_SESSION_LINK_IDLE_V2,
          "both public engines project ready idle link facts");

    const auto* create =
        find_action(inviter_actions, FLY_SESSION_ACTION_CREATE_INVITE_V2);
    const auto* join =
        find_action(joiner_actions, FLY_SESSION_ACTION_JOIN_CODE_V2);
    check(create != nullptr && create->enabled && create->approval_token != 0,
          "the inviter engine publishes its create-invite approval token");
    check(join != nullptr && join->enabled && join->approval_token != 0,
          "the joiner engine publishes its join-code approval token");

    /* The two port sets really are separate: neither engine has touched its
     * discovery port just by coming up. */
    check(inviter.discovery.advertisements == 0 &&
              inviter.discovery.scans == 0 &&
              joiner.discovery.advertisements == 0 &&
              joiner.discovery.scans == 0,
          "neither engine has started any discovery operation yet");

    if (create != nullptr)
        fly_session_approval_token_release_v2(create->approval_token);
    if (join != nullptr)
        fly_session_approval_token_release_v2(join->approval_token);
}

/*
 * step 2a: the generated P-256 table is re-validated by the REPOSITORY'S OWN curve
 * check, and its first two entries are pinned against the constants the repository
 * already ships. That is what makes "the generator script computed these
 * correctly" a fact the repository enforces, rather than something the author and
 * a Python script agreed on.
 */
void p256_table_is_valid_and_matches_repository_constants()
{
    const auto& points = flynes::session::loopback::loopback_p256_points();
    check(points.size() == 12u, "the fixture ships twelve public points");

    for (std::size_t index = 0; index < points.size(); ++index)
        check(wire::validate_p256_uncompressed_point(points[index].data()),
              "every generated point passes the repository's own curve check");

    for (std::size_t left = 0; left < points.size(); ++left)
        for (std::size_t right = left + 1; right < points.size(); ++right)
            check(points[left] != points[right],
                  "the generated points are pairwise distinct");

    /* The two constants the repository already ships, byte for byte: G and 2G. If
     * the generator's arithmetic ever drifted, this fails. */
    const std::array<std::uint8_t, 65> generator = {{
        0x04, 0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47, 0xf8, 0xbc, 0xe6,
        0xe5, 0x63, 0xa4, 0x40, 0xf2, 0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33,
        0xa0, 0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96, 0x4f, 0xe3, 0x42,
        0xe2, 0xfe, 0x1a, 0x7f, 0x9b, 0x8e, 0xe7, 0xeb, 0x4a, 0x7c, 0x0f, 0x9e,
        0x16, 0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31, 0x5e, 0xce, 0xcb, 0xb6, 0x40,
        0x68, 0x37, 0xbf, 0x51, 0xf5}};
    const std::array<std::uint8_t, 65> twice_generator = {{
        0x04, 0x7c, 0xf2, 0x7b, 0x18, 0x8d, 0x03, 0x4f, 0x7e, 0x8a, 0x52, 0x38,
        0x03, 0x04, 0xb5, 0x1a, 0xc3, 0xc0, 0x89, 0x69, 0xe2, 0x77, 0xf2, 0x1b,
        0x35, 0xa6, 0x0b, 0x48, 0xfc, 0x47, 0x66, 0x99, 0x78, 0x07, 0x77, 0x55,
        0x10, 0xdb, 0x8e, 0xd0, 0x40, 0x29, 0x3d, 0x9a, 0xc6, 0x9f, 0x74, 0x30,
        0xdb, 0xba, 0x7d, 0xad, 0xe6, 0x3c, 0xe9, 0x82, 0x29, 0x9e, 0x04, 0xb7,
        0x9d, 0x22, 0x78, 0x73, 0xd1}};
    check(points[0] == generator,
          "point 1 is the repository's NIST P-256 generator, byte for byte");
    check(points[1] == twice_generator,
          "point 2 is the repository's second constant point, byte for byte");

    /* The curve check must actually reject damage, otherwise "all twelve pass"
     * would be vacuous. */
    auto damaged = points[0];
    damaged[64] ^= 0x01u;
    check(!wire::validate_p256_uncompressed_point(damaged.data()),
          "a one-bit mutation is still rejected by the curve check");
}

/*
 * step 2b: the shared world is deterministic and shared. These assertions are about
 * the HARNESS, not about the product: they pin the properties the later steps rely
 * on, so a broken world fails here instead of producing a confusing failure five
 * phases later.
 *
 * THIS TEST DOES NOT VERIFY NIST P-256 KEY AGREEMENT. LoopbackWorld::agree is an
 * ECDH stand-in that hashes the two public keys: it implements no curve arithmetic
 * and checks no contributory behaviour. The only property asserted below is the one
 * the protocol layer needs — both sides derive the same secret. Real key agreement
 * is certified by provider/device tests, never here.
 */
void loopback_world_is_deterministic_and_shared()
{
    using flynes::session::loopback::LoopbackSide;
    using flynes::session::loopback::LoopbackWorld;
    using flynes::session::loopback::loopback_hmac_sha256;
    using flynes::session::loopback::loopback_p256_points;

    const auto& points = loopback_p256_points();
    LoopbackWorld world;

    /* hkdf: same inputs, same output; different inputs, different output. */
    const std::vector<std::uint8_t> secret{1, 2, 3, 4, 5};
    const std::array<std::uint8_t, 4> salt{{9, 9, 9, 9}};
    const char* info = "flynes-test-info";
    const auto derived_a = world.hkdf(secret, salt.data(), salt.size(),
                                      reinterpret_cast<const std::uint8_t*>(info),
                                      std::strlen(info), 64);
    const auto derived_b = world.hkdf(secret, salt.data(), salt.size(),
                                      reinterpret_cast<const std::uint8_t*>(info),
                                      std::strlen(info), 64);
    check(derived_a.size() == 64u && derived_a == derived_b,
          "hkdf is deterministic for equal inputs and honours the size");
    const char* other_info = "other";
    const auto derived_other = world.hkdf(
        secret, salt.data(), salt.size(),
        reinterpret_cast<const std::uint8_t*>(other_info), std::strlen(other_info),
        64);
    check(derived_other != derived_a, "hkdf separates different info strings");

    /* hmac: the harness helper really is RFC 2104 HMAC over the repository's
     * sha256, checked by an independent recomputation of the construction. */
    const std::array<std::uint8_t, 3> key{{0x0b, 0x0b, 0x0b}};
    const std::array<std::uint8_t, 8> message{{'m', 'e', 's', 's', 'a', 'g', 'e', '!'}};
    const auto tag = loopback_hmac_sha256(key.data(), key.size(),
                                          message.data(), message.size());
    std::array<std::uint8_t, 64> block{};
    std::copy(key.begin(), key.end(), block.begin());
    std::vector<std::uint8_t> inner;
    for (std::uint8_t value : block)
        inner.push_back(static_cast<std::uint8_t>(value ^ 0x36u));
    inner.insert(inner.end(), message.begin(), message.end());
    const auto inner_hash = wire::sha256(inner.data(), inner.size());
    std::array<std::uint8_t, 96> outer{};
    for (std::size_t index = 0; index < block.size(); ++index)
        outer[index] = static_cast<std::uint8_t>(block[index] ^ 0x5cu);
    std::copy(inner_hash.begin(), inner_hash.end(), outer.begin() + 64);
    const auto expected_tag = wire::sha256(outer.data(), outer.size());
    check(tag == expected_tag,
          "the harness hmac really is RFC 2104 HMAC over the repository sha256");

    /* aead: faithful round trip, and the tag is bound to key, nonce and aad. */
    const std::vector<std::uint8_t> aead_key{7, 7, 7};
    const std::array<std::uint8_t, 12> nonce{{1, 2, 3}};
    const std::array<std::uint8_t, 5> aad{{4, 5, 6, 7, 8}};
    const std::array<std::uint8_t, 6> plaintext{{9, 8, 7, 6, 5, 4}};
    const auto sealed = world.seal(aead_key, nonce.data(), nonce.size(),
                                   aad.data(), aad.size(), plaintext.data(),
                                   plaintext.size());
    check(sealed.size() == plaintext.size() + 16u,
          "seal appends the expected tag length");
    std::vector<std::uint8_t> opened;
    check(world.open(aead_key, nonce.data(), nonce.size(), aad.data(), aad.size(),
                     sealed.data(), sealed.size(), &opened) &&
              opened == std::vector<std::uint8_t>(plaintext.begin(),
                                                  plaintext.end()),
          "open recovers exactly the sealed plaintext");
    const std::vector<std::uint8_t> other_key{8, 8, 8};
    check(!world.open(other_key, nonce.data(), nonce.size(), aad.data(),
                      aad.size(), sealed.data(), sealed.size(), &opened),
          "opening with a different key fails instead of returning garbage");
    check(!world.open(aead_key, nonce.data(), nonce.size(), aad.data(),
                      aad.size() - 1, sealed.data(), sealed.size(), &opened),
          "opening under different associated data fails");
    auto tampered = sealed;
    tampered[0] ^= 0x01u;
    check(!world.open(aead_key, nonce.data(), nonce.size(), aad.data(), aad.size(),
                      tampered.data(), tampered.size(), &opened),
          "opening a tampered ciphertext fails");

    /* agree: the same secret from either side. Nothing about P-256 is asserted. */
    const auto secret_a = world.agree(points[0], points[3]);
    const auto secret_b = world.agree(points[3], points[0]);
    check(secret_a == secret_b && !secret_a.empty(),
          "the ECDH stand-in yields the same secret on both sides (this asserts "
          "agreement symmetry only, never NIST P-256 key agreement)");
    check(world.agree(points[0], points[5]) != secret_a,
          "a different peer point yields a different secret");

    /* sign/verify: real recomputation, canonical low-S shape. */
    const std::array<std::uint8_t, 32> digest{{0x11}};
    const char* domain = "flynes-loopback-test-v1";
    const auto signature =
        world.sign(points[2], reinterpret_cast<const std::uint8_t*>(domain),
                   std::strlen(domain), digest.data());
    check(wire::link_control_signature_is_canonical_v1(signature.data()),
          "the mock signature is a canonical low-S encoding");
    check(world.verify(points[2], reinterpret_cast<const std::uint8_t*>(domain),
                       std::strlen(domain), digest.data(), signature.data()),
          "the mock verifier accepts the signature it produced");
    auto bad_signature = signature;
    bad_signature[10] ^= 0x01u;
    check(!world.verify(points[2], reinterpret_cast<const std::uint8_t*>(domain),
                        std::strlen(domain), digest.data(), bad_signature.data()),
          "the mock verifier rejects a tampered signature");
    check(!world.verify(points[4], reinterpret_cast<const std::uint8_t*>(domain),
                        std::strlen(domain), digest.data(), signature.data()),
          "the mock verifier rejects the signature under another key");

    /* random: per-side and per-call distinct. */
    const char* purpose = "flynes-test-random";
    const auto initiator_random = world.random(
        LoopbackSide::Initiator, 32,
        reinterpret_cast<const std::uint8_t*>(purpose), std::strlen(purpose));
    const auto responder_random = world.random(
        LoopbackSide::Responder, 32,
        reinterpret_cast<const std::uint8_t*>(purpose), std::strlen(purpose));
    check(initiator_random.size() == 32u && responder_random.size() == 32u,
          "random honours the requested size");
    check(initiator_random != responder_random,
          "the two sides never receive the same random bytes");
    const auto initiator_again = world.random(
        LoopbackSide::Initiator, 32,
        reinterpret_cast<const std::uint8_t*>(purpose), std::strlen(purpose));
    check(initiator_again != initiator_random,
          "successive draws on one side differ");

    /* Each purpose and side maps to a different on-curve point, which the wire
     * codecs require (the long-term identity key and the session signing key must
     * differ). */
    const auto identity_handle = world.allocate_point(
        EngineFixture::Key::point_index_for(FLY_SESSION_KEY_DEVICE_IDENTITY_V2,
                                            LoopbackSide::Initiator));
    const auto session_handle = world.allocate_point(
        EngineFixture::Key::point_index_for(FLY_SESSION_KEY_SESSION_SIGNING_V2,
                                            LoopbackSide::Initiator));
    const auto peer_identity_handle = world.allocate_point(
        EngineFixture::Key::point_index_for(FLY_SESSION_KEY_DEVICE_IDENTITY_V2,
                                            LoopbackSide::Responder));
    check(*world.point_of(identity_handle) != *world.point_of(session_handle),
          "the identity and session signing keys are different points");
    check(*world.point_of(identity_handle) !=
              *world.point_of(peer_identity_handle),
          "the same purpose maps to a different point on each side");
}

/*
 * The pump's report has to match the ports' report, request for request, on EVERY
 * port this harness answers.
 *
 * Every terminal the pump sends is counted, and every port callback that leaves an
 * operation pending is counted by the fixture itself. The two must line up exactly:
 * an extra terminal would mean the pump invented a completion, and a missing one
 * would mean a real request was left unanswered. The last check requires the answer
 * total to be accounted for by the per-port counts, so a port cannot be added to the
 * pump without being reported here.
 */
void check_pump_answers_match_the_ports(
    EngineFixture& fixture, flynes::session::loopback::PumpState& pump,
    const char* role)
{
    const auto& counts = pump.counts;
    char text[256];
    auto expect = [&](int pump_value, int port_value, const char* label) {
        std::snprintf(text, sizeof(text),
                      "the pump answered exactly the %s the %s engine really made",
                      label, role);
        check(pump_value == port_value, text);
    };

    expect(counts.key_handles, fixture.key.generates,
           "key generation requests");
    expect(counts.key_publics, fixture.key.public_reads,
           "public-key reads");
    expect(counts.key_agreements, fixture.key.agreements,
           "key-agreement requests");
    expect(counts.key_signatures, fixture.key.signs, "signature requests");
    expect(counts.crypto_randoms, fixture.crypto.randoms,
           "random-byte requests");
    expect(counts.crypto_secrets, fixture.crypto.hkdfs,
           "key-derivation requests");
    expect(counts.crypto_macs, fixture.crypto.hmacs, "mac requests");
    expect(counts.crypto_verifies, fixture.crypto.verifies,
           "signature-verification requests");
    expect(counts.crypto_aead_seals, fixture.crypto.aead_seals,
           "sealing requests");
    expect(counts.crypto_aead_opens, fixture.crypto.aead_opens,
           "opening requests");
    expect(counts.tls_materials, fixture.tls.creates,
           "TLS-material requests");
    expect(counts.bearer_capabilities, fixture.bearer.probes,
           "bearer capability probes");
    expect(counts.bearer_paths, fixture.bearer.creates + fixture.bearer.joins,
           "bearer path requests");
    expect(counts.bearer_credentials, fixture.bearer.prepares,
           "bearer credential requests");
    expect(counts.bearer_endpoints, fixture.bearer.resolves,
           "bearer endpoint resolutions");
    expect(counts.discovery_write_ends, fixture.discovery.writes,
           "GATT write completions");
    expect(counts.secure_store_revisions, fixture.secure_store.writes,
           "durable-store writes");
    expect(counts.object_puts, fixture.object_store.puts, "object writes");
    expect(counts.object_reads, fixture.object_store.reads, "object reads");

    std::snprintf(text, sizeof(text),
                  "a provider rejection is still an answer to the request the %s "
                  "engine really made, never an extra one",
                  role);
    check(counts.crypto_verify_failures <= counts.crypto_verifies &&
              counts.crypto_aead_open_failures <= counts.crypto_aead_opens,
          text);

    const int accounted =
        counts.key_handles + counts.key_publics + counts.key_agreements +
        counts.key_signatures + counts.crypto_randoms + counts.crypto_secrets +
        counts.crypto_macs + counts.crypto_verifies + counts.crypto_aead_seals +
        counts.crypto_aead_opens + counts.tls_materials +
        counts.bearer_capabilities + counts.bearer_paths +
        counts.bearer_credentials + counts.bearer_endpoints +
        counts.discovery_write_ends + counts.secure_store_revisions +
        counts.object_puts + counts.object_reads;
    std::snprintf(text, sizeof(text),
                  "every terminal the pump sent to the %s engine completed a request "
                  "one of its ports really made, and none is unaccounted for",
                  role);
    check(counts.answers == accounted, text);
}

/*
 * The same identity, plus what THIS phase cannot have reached: the bearer, the
 * durable store and the object store are later phases, so a non-zero count there
 * would mean the run went further than the step-3 test claims.
 */
void check_pump_matches_the_requests_this_phase_produced(
    EngineFixture& fixture, flynes::session::loopback::PumpState& pump,
    const char* role)
{
    check_pump_answers_match_the_ports(fixture, pump, role);

    const auto& counts = pump.counts;
    char message[256];
    std::snprintf(message, sizeof(message),
                  "the pump answered nothing on the bearer, durable-store and object "
                  "ports the %s engine never reached in this step",
                  role);
    check(counts.bearer_paths == 0 && counts.bearer_credentials == 0 &&
              counts.bearer_endpoints == 0 && counts.secure_store_revisions == 0 &&
              counts.object_puts == 0 && counts.object_reads == 0,
          message);
}

/*
 * step 3: the loopback TRANSPORT and the counted, bounded provider pump.
 *
 * The connection each engine receives is produced ONLY by `LoopbackTransport`, from
 * this run's own discovery roles and its own link handle. This test never authors a
 * connection, a byte or a stream record; the only events it delivers are the
 * completions of operations the ports themselves reported (see pump_once).
 *
 * WHAT THIS INCREMENT DOES NOT CLAIM
 *   It does not claim CONNECTED_LOBBY. With no byte loopback yet, the GATT fragments
 *   each engine writes are never carried to the other engine, so both engines stop
 *   after their own local work and wait for peer bytes. What IS asserted is: the
 *   transport connected both ends of one link, both engines moved into
 *   AUTHENTICATING, the pump answered precisely the requests this phase really
 *   produced, and both engines then destroy cleanly on their own - the destroy
 *   result is asserted here, not ignored. The byte-accurate GATT loopback (step 4),
 *   the QUIC loopback (step 5) and CONNECTED_LOBBY (step 6) are what make the
 *   remaining assertions possible.
 */
void two_engines_connect_through_the_transport_and_are_pumped_to_idle()
{
    using flynes::session::loopback::LoopbackRole;
    using flynes::session::loopback::LoopbackSide;
    using flynes::session::loopback::LoopbackTransport;
    using flynes::session::loopback::LoopbackWorld;
    using flynes::session::loopback::PumpLimits;
    using flynes::session::loopback::PumpState;
    using flynes::session::loopback::pump_engine;
    using flynes::session::loopback::shutdown_engine_with_the_pump;
    using flynes::session::loopback::submit;

    LoopbackWorld world;
    EngineFixture inviter(world, LoopbackSide::Initiator);
    EngineFixture joiner(world, LoopbackSide::Responder);

    inviter.platform.ready();
    joiner.platform.ready();
    inviter.executor.run_all();
    joiner.executor.run_all();

    std::vector<fly_session_action_descriptor_v2> inviter_actions;
    std::vector<fly_session_action_descriptor_v2> joiner_actions;
    inviter.snapshot(&inviter_actions);
    joiner.snapshot(&joiner_actions);
    const auto* create =
        find_action(inviter_actions, FLY_SESSION_ACTION_CREATE_INVITE_V2);
    const auto* join =
        find_action(joiner_actions, FLY_SESSION_ACTION_JOIN_CODE_V2);
    check(create != nullptr && join != nullptr,
          "both engines publish the link action their role needs");
    if (create == nullptr || join == nullptr)
    {
        for (auto& action : inviter_actions)
            fly_session_approval_token_release_v2(action.approval_token);
        for (auto& action : joiner_actions)
            fly_session_approval_token_release_v2(action.approval_token);
        return;
    }
    submit(inviter, *create, 501, false);
    submit(joiner, *join, 502, true);

    /* The two engines took opposite discovery roles, which is what the transport
     * below needs in order to know which end is which. */
    check(inviter.discovery.advertisements == 1 && inviter.discovery.scans == 0,
          "the inviter engine advertises and does not scan");
    check(joiner.discovery.scans == 1 && joiner.discovery.advertisements == 0,
          "the joiner engine scans and does not advertise");

    LoopbackTransport transport;
    transport.attach(LoopbackRole::AdvertiserPeripheral, inviter);
    transport.attach(LoopbackRole::ScannerCentral, joiner);

    check(transport.connect_ends() == 2,
          "the transport produced the connection at both ends of one link");
    check(transport.peripheral_connected() && transport.central_connected(),
          "both ends are on the shared link");
    /* Idempotence: a second connect must not re-deliver anything. */
    check(transport.connect_ends() == 0,
          "connecting an already connected pair delivers nothing further");
    check(transport.connections() == 2,
          "the transport reports exactly two connections for this run");

    PumpState inviter_pump;
    PumpState joiner_pump;
    const PumpLimits limits{};
    pump_engine(inviter, inviter_pump, limits);
    pump_engine(joiner, joiner_pump, limits);

    /*
     * Both engines accepted the connection the TRANSPORT produced: each subscribed
     * to the link and moved its own projection out of its discovery role into
     * AUTHENTICATING. Neither fact can be scripted by the test - they follow from the
     * transport's connection event alone.
     */
    check(inviter.discovery.subscriptions == 1 &&
              joiner.discovery.subscriptions == 1,
          "both engines subscribed to the link the transport connected");
    check(inviter.snapshot().link_state == FLY_SESSION_LINK_AUTHENTICATING_V2 &&
              joiner.snapshot().link_state == FLY_SESSION_LINK_AUTHENTICATING_V2,
          "both engines advanced past their discovery role into AUTHENTICATING");

    /*
     * Only the advertising side speaks first: it is the one that draws fresh random
     * bytes and publishes its own first GATT message, while the scanning side
     * correctly waits for a peer message before it has anything to answer. Both are
     * waiting for peer bytes by the end of this step, which is exactly what the byte
     * loopback in step 4 supplies.
     */
    check(inviter.crypto.randoms >= 1 &&
              inviter.discovery.writes > 0,
          "the advertising engine really drew its own randomness and wrote its own "
          "first GATT message");
    check(joiner.discovery.writes == 0 && joiner.crypto.randoms == 0,
          "the scanning engine wrote nothing, because no peer message has reached it "
          "yet in this step");

    check_pump_matches_the_requests_this_phase_produced(inviter, inviter_pump,
                                                        "advertising");
    check_pump_matches_the_requests_this_phase_produced(joiner, joiner_pump,
                                                        "scanning");

    /* No byte loopback yet, so neither engine can have finished the handshake. */
    check(inviter.snapshot().link_state != FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
              joiner.snapshot().link_state != FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
          "with no byte loopback yet neither engine is in the connected lobby");

    /* The one thing this increment must prove about teardown: both engines still
     * destroy cleanly once their own provider work is answered. */
    shutdown_engine_with_the_pump(inviter, inviter_pump, limits);
    shutdown_engine_with_the_pump(joiner, joiner_pump, limits);

    for (auto& action : inviter_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    for (auto& action : joiner_actions)
        fly_session_approval_token_release_v2(action.approval_token);
}

/*
 * Reports the logical message types a direction carried, in order, from the fragments
 * that actually crossed. The first-fragment flag (bit 0 of the flags byte,
 * fragment[2]) marks the start of a logical message, so this reads the peer's own
 * framing and adds nothing to it.
 */
void report_crossed_messages(
    const char* label, const std::vector<std::vector<std::uint8_t>>& fragments)
{
    std::printf("  %s carried", label);
    for (const auto& fragment : fragments)
    {
        if (fragment.size() < 3) continue;
        if ((fragment[2] & 0x01u) != 0u)
            std::printf(" 0x%02x", fragment[1]);
    }
    std::printf(" (%zu fragments)\n", fragments.size());
}

/*
 * step 4: the byte-accurate GATT/discovery loopback between two REAL engines.
 *
 * Every fragment that crosses from one engine to the other is that engine's OWN
 * encoder output, carried verbatim; the relay re-frames nothing and the test authors
 * no transport event. Both ends are on the one link the `LoopbackTransport`
 * connected, and the driver alternates carrying bytes with answering provider work.
 *
 * WHAT THIS INCREMENT CLAIMS
 *   The two engines exchange their own GATT bytes in both directions; what crossed is
 *   byte-for-byte and in order the writing engine's own `written_fragments`; the
 *   scanning engine, which writes nothing at all without peer bytes (step 3), now
 *   consumes the advertising engine's PairContext and answers with its own
 *   PairCommit/PairReveal/PairSignature; and the committed engine fix is visible end
 *   to end, because the scanning engine no longer fails on the peer's
 *   PairKnownStatus but verifies it and publishes its own.
 *
 * WHAT IT DOES NOT CLAIM
 *   CONNECTED_LOBBY. The QUIC stage is step 5, and if this run ever reached it the
 *   pump reports that as a failure instead of hiding it. The SAS confirmation is the
 *   app's decision, so this increment does not submit it (see the driver's
 *   `answer_the_app_action`): doing so would walk the link into that stage.
 */
void two_engines_exchange_their_own_gatt_bytes()
{
    using flynes::session::loopback::LoopbackRole;
    using flynes::session::loopback::LoopbackSide;
    using flynes::session::loopback::LoopbackTransport;
    using flynes::session::loopback::LoopbackWorld;
    using flynes::session::loopback::PumpLimits;
    using flynes::session::loopback::PumpState;
    using flynes::session::loopback::RelayReport;
    using flynes::session::loopback::pump_engine;
    using flynes::session::loopback::relay_and_pump_until_idle;
    using flynes::session::loopback::shutdown_engine_with_the_pump;
    using flynes::session::loopback::submit;

    LoopbackWorld world;
    EngineFixture inviter(world, LoopbackSide::Initiator);
    EngineFixture joiner(world, LoopbackSide::Responder);

    inviter.platform.ready();
    joiner.platform.ready();
    inviter.executor.run_all();
    joiner.executor.run_all();

    std::vector<fly_session_action_descriptor_v2> inviter_actions;
    std::vector<fly_session_action_descriptor_v2> joiner_actions;
    inviter.snapshot(&inviter_actions);
    joiner.snapshot(&joiner_actions);
    const auto* create =
        find_action(inviter_actions, FLY_SESSION_ACTION_CREATE_INVITE_V2);
    const auto* join =
        find_action(joiner_actions, FLY_SESSION_ACTION_JOIN_CODE_V2);
    check(create != nullptr && join != nullptr,
          "both engines publish the link action their role needs");
    if (create == nullptr || join == nullptr)
    {
        for (auto& action : inviter_actions)
            fly_session_approval_token_release_v2(action.approval_token);
        for (auto& action : joiner_actions)
            fly_session_approval_token_release_v2(action.approval_token);
        return;
    }
    submit(inviter, *create, 601, false);
    submit(joiner, *join, 602, true);

    check(inviter.discovery.advertisements == 1 && inviter.discovery.scans == 0,
          "the inviter engine advertises and does not scan");
    check(joiner.discovery.scans == 1 && joiner.discovery.advertisements == 0,
          "the joiner engine scans and does not advertise");

    LoopbackTransport transport;
    transport.attach(LoopbackRole::AdvertiserPeripheral, inviter);
    transport.attach(LoopbackRole::ScannerCentral, joiner);
    check(transport.connect_ends() == 2,
          "the transport produced the connection at both ends of one link");

    PumpState inviter_pump;
    PumpState joiner_pump;
    const PumpLimits limits{};

    /*
     * Both engines must be LISTENING before the first byte is relayed. Relaying into
     * an engine that has not subscribed yet loses the bytes it would have accepted a
     * moment later; an earlier attempt that relayed early deadlocked after
     * PairContext + PairCommit with the scanning engine having written nothing.
     */
    pump_engine(inviter, inviter_pump, limits);
    pump_engine(joiner, joiner_pump, limits);
    check(inviter.discovery.subscriptions == 1 && joiner.discovery.subscriptions == 1,
          "both engines subscribed to the link the transport connected, before any "
          "byte is relayed across it");

    RelayReport relayed;
    relay_and_pump_until_idle(transport, inviter, inviter_pump, joiner, joiner_pump,
                              limits, 400, 6, &relayed);

    const auto& crossed_to_central = relayed.peripheral_to_central;
    const auto& crossed_to_peripheral = relayed.central_to_peripheral;

    std::printf("step 4 measured exchange: rounds=%d app_actions=%d\n",
                relayed.rounds, relayed.app_actions);
    report_crossed_messages("advertising peripheral -> scanning central",
                            crossed_to_central.delivered);
    report_crossed_messages("scanning central -> advertising peripheral",
                            crossed_to_peripheral.delivered);
    std::printf("  physical ack fragments: %zu -> central, %zu -> peripheral\n",
                crossed_to_central.delivered_acks.size(),
                crossed_to_peripheral.delivered_acks.size());
    std::printf("  advertising engine: writes=%d randoms=%d generates=%d "
                "public_reads=%d signs=%d agreements=%d hkdf=%d mac=%d "
                "verify=%d seal=%d open=%d\n",
                inviter.discovery.writes, inviter.crypto.randoms,
                inviter.key.generates, inviter.key.public_reads,
                inviter.key.signs, inviter.key.agreements, inviter.crypto.hkdfs,
                inviter.crypto.hmacs, inviter.crypto.verifies,
                inviter.crypto.aead_seals, inviter.crypto.aead_opens);
    std::printf("  scanning engine:    writes=%d randoms=%d generates=%d "
                "public_reads=%d signs=%d agreements=%d hkdf=%d mac=%d "
                "verify=%d seal=%d open=%d\n",
                joiner.discovery.writes, joiner.crypto.randoms,
                joiner.key.generates, joiner.key.public_reads, joiner.key.signs,
                joiner.key.agreements, joiner.crypto.hkdfs, joiner.crypto.hmacs,
                joiner.crypto.verifies, joiner.crypto.aead_seals,
                joiner.crypto.aead_opens);
    std::printf("  link states: advertising=%u scanning=%u\n",
                static_cast<unsigned>(inviter.snapshot().link_state),
                static_cast<unsigned>(joiner.snapshot().link_state));
    std::printf("  transcript persists asked for: advertising=%d scanning=%d\n",
                inviter.object_store.puts, joiner.object_store.puts);
    std::printf("  pump answers: advertising=%d scanning=%d\n",
                inviter_pump.counts.answers, joiner_pump.counts.answers);

    /* 1. Fragments really crossed, in both directions. */
    check(!crossed_to_central.delivered.empty() &&
              !crossed_to_peripheral.delivered.empty(),
          "the transport carried this run's own GATT fragments in BOTH directions");

    /*
     * 2. What crossed IS the source engine's own encoder output, byte for byte and in
     *    order - and it is the WHOLE of it, so nothing the engine wrote was dropped
     *    or re-framed on the way. This is the "no fabricated peer bytes" proof: the
     *    relay keeps a copy of every fragment it delivered, and that copy is compared
     *    against the writer's own list rather than against a script.
     */
    check(crossed_to_central.delivered.size() ==
                  inviter.discovery.written_fragments.size() &&
              std::equal(crossed_to_central.delivered.begin(),
                         crossed_to_central.delivered.end(),
                         inviter.discovery.written_fragments.begin()),
          "every fragment the scanning engine received is the advertising engine's "
          "own written fragment, byte for byte and in order");
    check(crossed_to_peripheral.delivered.size() ==
                  joiner.discovery.written_fragments.size() &&
              std::equal(crossed_to_peripheral.delivered.begin(),
                         crossed_to_peripheral.delivered.end(),
                         joiner.discovery.written_fragments.begin()),
          "every fragment the advertising engine received is the scanning engine's "
          "own written fragment, byte for byte and in order");

    /*
     * 3. The fragments were CONSUMED: both engines drew their own randomness and
     *    wrote their own GATT messages. The scanning engine wrote nothing at all in
     *    step 3, where no peer byte ever reached it, and its first own message here
     *    is a PairCommit - which the protocol can only produce after decoding the
     *    peer's own PairContext bytes.
     */
    check(inviter.crypto.randoms > 0 && inviter.discovery.writes > 0,
          "the advertising engine drew its own randomness and wrote its own GATT "
          "fragments");
    check(joiner.crypto.randoms > 0 && joiner.discovery.writes > 0,
          "the scanning engine drew its own randomness and wrote its own GATT "
          "fragments, which it never does without peer bytes (step 3)");
    check(!inviter.discovery.written_fragments.empty() &&
              inviter.discovery.written_fragments.front().size() > 1 &&
              inviter.discovery.written_fragments.front()[1] ==
                  static_cast<std::uint8_t>(wire::GattLogicalType::PairContext),
          "the advertising engine's first own GATT message is a PairContext");
    check(!joiner.discovery.written_fragments.empty() &&
              joiner.discovery.written_fragments.front().size() > 1 &&
              joiner.discovery.written_fragments.front()[1] ==
                  static_cast<std::uint8_t>(wire::GattLogicalType::PairCommit),
          "the scanning engine's first own GATT message is a PairCommit, which can "
          "only exist after it consumed the advertising engine's own PairContext");

    /* 4. The pump's strict per-port identity holds for both engines. */
    check_pump_answers_match_the_ports(inviter, inviter_pump, "advertising");
    check_pump_answers_match_the_ports(joiner, joiner_pump, "scanning");

    /*
     * 5. The point of this increment: the committed engine fix (6ab2c7a, "hold an
     *    early peer pair-known status instead of failing the link") is now visible
     *    end to end. Before it, the slower side failed the link on the peer's
     *    PairKnownStatus arriving before its own PairKnownScheduler existed; the
     *    advertising engine's own PairKnownStatus does cross here, and the scanning
     *    engine must now get PAST it instead of closing the link on it.
     */
    bool status_crossed = false;
    for (const auto& fragment : crossed_to_central.delivered)
        if (fragment.size() > 1 &&
            fragment[1] == static_cast<std::uint8_t>(
                               wire::GattLogicalType::PairKnownStatus))
            status_crossed = true;
    check(status_crossed,
          "the advertising engine's own PairKnownStatus really crossed to the "
          "scanning engine");
    check(inviter.snapshot().link_state != FLY_SESSION_LINK_FAILED_V2 &&
              joiner.snapshot().link_state != FLY_SESSION_LINK_FAILED_V2,
          "neither engine failed the link: the scanning engine held the early peer "
          "PairKnownStatus instead of closing the link on it");
    check(joiner.crypto.hmacs > 0,
          "the scanning engine reached Stage::HmacSas, which is what verifying the "
          "peer's pair-known status requires and what the committed fix exists to "
          "make reachable");
    bool scanning_published_pair_known = false;
    for (const auto& fragment : crossed_to_peripheral.delivered)
        if (fragment.size() > 1 &&
            (fragment[1] == static_cast<std::uint8_t>(
                                wire::GattLogicalType::PairKnownStatus) ||
             fragment[1] == static_cast<std::uint8_t>(
                                wire::GattLogicalType::PairKnownBranch)))
            scanning_published_pair_known = true;
    check(scanning_published_pair_known,
          "the scanning engine published its own pair-known message (type 0x15 or "
          "0x11) once it had verified the peer's");
    /*
     * The transcript persist is what lets the NON-initiator build its own
     * PairKnownScheduler at all: `pair_signature_->ready()` is set by that persist,
     * and `start_pair_sas_locked()` - and therefore `pair_sas_`, which the fix's hold
     * gate requires - only runs once the signature scheduler is ready. Both engines
     * persist a transcript on this path, so both must ask for it.
     */
    check(inviter.object_store.puts > 0 && joiner.object_store.puts > 0,
          "both engines asked their object store to persist the pair transcript, "
          "which is what lets each side build its own PairKnownScheduler");

    /* No CONNECTED_LOBBY claim is made in this step; the QUIC stage is step 5. */
    check(inviter.snapshot().link_state != FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
              joiner.snapshot().link_state != FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
          "neither engine is in the connected lobby: the QUIC byte loopback is step "
          "5 and this run did not silently skip to a later claim");

    shutdown_engine_with_the_pump(inviter, inviter_pump, limits);
    shutdown_engine_with_the_pump(joiner, joiner_pump, limits);

    for (auto& action : inviter_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    for (auto& action : joiner_actions)
        fly_session_approval_token_release_v2(action.approval_token);
}

} // namespace

int main()
{
    two_engines_come_up_independently();
    p256_table_is_valid_and_matches_repository_constants();
    loopback_world_is_deterministic_and_shared();
    two_engines_connect_through_the_transport_and_are_pumped_to_idle();
    two_engines_exchange_their_own_gatt_bytes();

    if (failures != 0)
    {
        std::fprintf(stderr,
                     "%d two-engine loopback checks failed\n", failures);
        return 1;
    }
    std::puts("two-engine loopback (step 1: two engines come up; step 2: shared "
              "deterministic world; step 3: transport + bounded pump; step 4: "
              "byte-accurate GATT loopback) passed");
    return 0;
}
