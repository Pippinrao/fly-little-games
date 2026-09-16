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
 * The pump's report has to match the ports' report, request for request.
 *
 * Every terminal the pump sends is counted, and every port callback that leaves an
 * operation pending is counted by the fixture itself. The two must line up exactly:
 * an extra terminal would mean the pump invented a completion, and a missing one
 * would mean a real request was left unanswered. The later phases (paths,
 * credentials, endpoints, durable stores, TLS material) are asserted to zero here,
 * because this phase genuinely produced none of them - if a real run ever did, this
 * is where it must be noticed rather than answered from a script.
 */
void check_pump_matches_the_requests_this_phase_produced(
    EngineFixture& fixture, flynes::session::loopback::PumpState& pump,
    const char* role)
{
    const auto& counts = pump.counts;
    char message[256];
    std::snprintf(message, sizeof(message),
                  "the pump answered exactly the GATT write terminals the %s engine "
                  "really produced", role);
    check(counts.discovery_write_ends == fixture.discovery.writes, message);

    std::snprintf(message, sizeof(message),
                  "the pump answered exactly the bearer capability probes the %s "
                  "engine really made", role);
    check(counts.bearer_capabilities == fixture.bearer.probes, message);

    std::snprintf(message, sizeof(message),
                  "every terminal the pump sent to the %s engine completed a request "
                  "one of its ports really made", role);
    check(counts.answers == counts.discovery_write_ends +
                                counts.bearer_capabilities,
          message);

    std::snprintf(message, sizeof(message),
                  "the pump answered nothing on the ports the %s engine never reached "
                  "in this step", role);
    check(counts.tls_materials == 0 && counts.bearer_paths == 0 &&
              counts.bearer_credentials == 0 && counts.bearer_endpoints == 0 &&
              counts.secure_store_revisions == 0 && counts.object_puts == 0 &&
              counts.object_reads == 0,
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

} // namespace

int main()
{
    two_engines_come_up_independently();
    p256_table_is_valid_and_matches_repository_constants();
    loopback_world_is_deterministic_and_shared();
    two_engines_connect_through_the_transport_and_are_pumped_to_idle();

    if (failures != 0)
    {
        std::fprintf(stderr,
                     "%d two-engine loopback checks failed\n", failures);
        return 1;
    }
    std::puts("two-engine loopback (step 1: two engines come up; step 2: shared "
              "deterministic world; step 3: transport + bounded pump) passed");
    return 0;
}
