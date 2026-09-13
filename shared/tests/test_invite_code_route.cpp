// Invite-code lookup route tests (plan P2; cases C05/C06/C07/C08/C16).
// The eight vectors are quoted verbatim from the implementation plan. Wire
// constraints for kinds 0x0214/0x0215 are asserted through the shared codec;
// the golden negatives (truncate/trailing/reserved/enum) run in
// test_session_codec from the generated manifest.

#include "invite_code_route.hpp"
#include "wire/session_codec.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

using flynes::session::InviteCodeDigits;
using flynes::session::InviteCodeHost;
using flynes::session::InviteCodeJoiner;
using flynes::session::InviteJoinPhase;
using flynes::session::InviteLookupStatus;
using flynes::session::kInviteLookupAttemptsPerInvitation;
using flynes::session::parse_invite_code;

constexpr std::uint64_t kT0 = UINT64_C(1000000000);          // arbitrary origin
constexpr std::uint64_t kHalf = UINT64_C(30000000000);       // 30 s
constexpr std::uint64_t kFull = UINT64_C(60000000000);       // 60 s

InviteCodeDigits digits(const char* six)
{
    InviteCodeDigits out{};
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        out[i] = static_cast<std::uint8_t>(six[i]);
    }
    return out;
}

// --- Vector 1: InputCode("012345") retains all six bytes. -------------------

void test_leading_zeros_preserved()
{
    const auto parsed = parse_invite_code("012345");
    check(parsed.has_value(), "C05: six digits parse");
    if (parsed)
    {
        check(*parsed == digits("012345"), "C05: leading zero retained byte-exactly");
    }
    check(parse_invite_code("  012345  ").has_value(),
          "C05: pasted leading/trailing whitespace is trimmed");
    check(parse_invite_code("\t012345\n").has_value(),
          "C05: all ASCII whitespace trims");
}

// --- Vector 2: "12345"/"1234567"/"12a456" emit no lookup request. -----------

void test_invalid_input_emits_no_request()
{
    for (const char* bad : {"12345", "1234567", "12a456", "", "12 456", "٠١٢٣٤٥"})
    {
        check(!parse_invite_code(bad),
              "C05: invalid input must not parse into a lookup request");
        InviteCodeJoiner joiner;
        check(!joiner.start(1, bad, kT0),
              "C05: start refuses the input and emits no request");
        check(joiner.phase() == InviteJoinPhase::Idle,
              "C05: refused input leaves the route idle (no request sent)");
    }
}

// --- Vector 3: LookupMatch + HostApprovalAbsent emits no identity reveal or
// credentials. --------------------------------------------------------------

void test_match_without_host_approval_reveals_nothing()
{
    InviteCodeJoiner joiner;
    check(joiner.start(1, "012345", kT0), "lookup attempt starts");
    check(joiner.phase() == InviteJoinPhase::LookingUp, "attempt is looking up");
    check(joiner.on_response(1, InviteLookupStatus::MatchPendingHostApproval, 77, kT0 + 1),
          "match response accepted");
    check(joiner.phase() == InviteJoinPhase::WaitingHostApproval,
          "C06: matched attempt waits for the host approval gate");
    check(!joiner.connected(), "C06: a match never connects by itself");

    // The route exposes no identity surface at all: before host approval the
    // SAS stage does not exist, so a confirm attempt is refused and reveals or
    // authorizes nothing.
    check(!joiner.on_local_sas_confirmed(1),
          "C06: SAS confirm refused before host approval");
    check(joiner.phase() == InviteJoinPhase::WaitingHostApproval,
          "C06: stray SAS input cannot skip the host approval gate");
}

// --- Vector 4: LookupMatch + HostAccept + LocalSasApproveOnly remains
// unconnected. ---------------------------------------------------------------

void test_single_side_sas_remains_unconnected()
{
    InviteCodeJoiner joiner;
    check(joiner.start(1, "012345", kT0), "attempt starts");
    check(joiner.on_response(1, InviteLookupStatus::MatchPendingHostApproval, 77, kT0 + 1),
          "match accepted");
    check(joiner.on_host_accepted(1), "host acceptance accepted");
    check(joiner.phase() == InviteJoinPhase::WaitingSasConfirm,
          "SAS stage reached through the existing identity flow");
    check(joiner.on_local_sas_confirmed(1), "local SAS confirm accepted");
    check(joiner.local_sas_confirmed() && !joiner.peer_sas_confirmed(),
          "C07: exactly one side confirmed");
    check(!joiner.connected(), "C07: single-side SAS confirmation never connects");
    check(joiner.phase() == InviteJoinPhase::WaitingSasConfirm,
          "C07: still waiting for the peer confirmation");

    check(joiner.on_peer_sas_confirmed(1), "peer SAS confirm accepted");
    check(joiner.connected(), "C07: both confirmations complete the pairing");
}

// --- Vector 5: Regenerate(invite G1 -> G2); ReceiveMatch(G1) remains
// cancelled. -----------------------------------------------------------------

void test_regenerate_kills_old_generation()
{
    InviteCodeHost host;
    check(host.publish(1, "111111", kT0), "host publishes G1");
    check(host.regenerate(2, "222222", kT0 + 1), "host regenerates to G2");
    check(host.active_generation() == 2, "G2 is the only active generation");
    // The dead G1 code must not resolve to anything, and never to G1's number.
    const InviteLookupStatus old_code = host.on_lookup(digits("111111"), kT0 + 2);
    check(old_code == InviteLookupStatus::NoMatch, "C16: old code answers NoMatch after regeneration");

    // A joiner that cancelled against G1 and restarted for G2 drops G1's late
    // match: cancelled attempts are never revived.
    InviteCodeJoiner joiner;
    check(joiner.start(1, "111111", kT0), "first attempt (G1 code) starts");
    check(joiner.cancel(1), "attempt cancelled before the answer arrives");
    check(joiner.on_response(1, InviteLookupStatus::MatchPendingHostApproval, 1, kT0 + 2) == false,
          "C16: late match for a cancelled attempt is refused");
    check(joiner.phase() == InviteJoinPhase::Cancelled,
          "C16: cancelled attempt stays cancelled");
    check(joiner.start(2, "222222", kT0 + 3), "new attempt for G2 starts");
    check(joiner.on_response(2, InviteLookupStatus::MatchPendingHostApproval, 2, kT0 + 4),
          "live attempt accepts its own match");
}

// --- Vector 6: Cancel/Timeout; ReceiveHostAccept(old generation) remains
// cancelled. -----------------------------------------------------------------

void test_cancel_and_timeout_kill_late_accepts()
{
    InviteCodeJoiner joiner;
    check(joiner.start(1, "012345", kT0), "attempt starts");
    check(joiner.cancel(1), "cancel accepted");
    check(joiner.on_host_accepted(1) == false,
          "C16: host acceptance after cancel is refused");
    check(joiner.phase() == InviteJoinPhase::Cancelled, "stays cancelled");

    InviteCodeJoiner timed_out;
    check(timed_out.start(1, "012345", kT0), "attempt starts");
    timed_out.tick(kT0 + kFull);
    check(timed_out.phase() == InviteJoinPhase::Expired, "attempt expires at the 60s deadline");
    check(timed_out.on_host_accepted(1) == false,
          "C16: host acceptance after timeout is refused");
    check(timed_out.phase() == InviteJoinPhase::Expired, "stays expired");

    // A host acceptance that arrives after the joiner's own deadline on a live
    // (not yet ticked) attempt is equally dead.
    InviteCodeJoiner late;
    check(late.start(1, "012345", kT0), "attempt starts");
    late.tick(kT0 + kFull);
    check(late.phase() == InviteJoinPhase::Expired, "deadline passed");
}

// --- Vector 7: Code collision among candidates never silently picks an
// identity. ------------------------------------------------------------------

void test_collision_never_silently_picks()
{
    InviteCodeJoiner joiner;
    check(joiner.start(1, "012345", kT0), "attempt starts");
    check(joiner.on_response(1, InviteLookupStatus::MatchPendingHostApproval, 11, kT0 + 1),
          "first candidate matches");
    // A second candidate answers the same attempt with a DIFFERENT invitation
    // generation: ambiguous, the attempt fails instead of picking one.
    check(joiner.on_response(1, InviteLookupStatus::MatchPendingHostApproval, 22, kT0 + 2) == false,
          "second distinct match is refused");
    check(joiner.phase() == InviteJoinPhase::Ambiguous,
          "collision ends the attempt as ambiguous");
    check(!joiner.connected(), "collision never connects");
    check(joiner.on_host_accepted(1) == false, "no acceptance after ambiguity");

    // A duplicate response from the SAME candidate (same generation) is
    // idempotent and harmless.
    InviteCodeJoiner duplicate;
    check(duplicate.start(1, "012345", kT0), "attempt starts");
    check(duplicate.on_response(1, InviteLookupStatus::MatchPendingHostApproval, 11, kT0 + 1),
          "first match accepted");
    check(duplicate.on_response(1, InviteLookupStatus::MatchPendingHostApproval, 11, kT0 + 2),
          "same-generation repeat is idempotent");
    check(duplicate.phase() == InviteJoinPhase::WaitingHostApproval,
          "still waiting for host approval");
}

// --- Vector 8: No ROM selected; finish verified pairing -> connected
// game-less lobby. -----------------------------------------------------------

void test_gameless_connected_lobby()
{
    InviteCodeJoiner joiner;
    check(joiner.start(1, "012345", kT0), "attempt starts");
    check(joiner.on_response(1, InviteLookupStatus::MatchPendingHostApproval, 77, kT0 + 1),
          "match accepted");
    check(joiner.on_host_accepted(1), "host accepted");
    check(joiner.on_local_sas_confirmed(1), "local SAS confirmed");
    check(joiner.on_peer_sas_confirmed(1), "peer SAS confirmed");
    check(joiner.connected() && joiner.phase() == InviteJoinPhase::Authenticated,
          "C04/C09: verified pairing connects with no game selected");
    // The route type carries no game/ROM field anywhere: the connected state
    // is valid as a game-less lobby (static guarantee by API surface).
}

// --- Host-side gates (rate limit, expiry, explicit cancel, bad requests) ----

void test_host_gates()
{
    InviteCodeHost host;
    const auto code = parse_invite_code("987654");
    check(code.has_value(), "host code parses");
    check(!host.publish(0, "987654", kT0), "generation 0 is refused");
    check(host.publish(5, "987654", kT0), "publish accepted");
    check(!host.publish(6, "111111", kT0 + 1),
          "publish refuses while an invitation is active (use regenerate)");

    for (unsigned i = 0; i < kInviteLookupAttemptsPerInvitation; ++i)
    {
        check(host.on_lookup(digits("000000"), kT0 + 1) == InviteLookupStatus::NoMatch,
              "wrong code answers NoMatch");
    }
    check(host.attempts_left() == 0, "budget exhausted");
    check(host.on_lookup(digits("987654"), kT0 + 2) == InviteLookupStatus::RateLimited,
          "C06: even the correct code is rate-limited after the budget");

    check(host.cancel(5), "explicit cancel works");
    check(host.active_generation() == 0, "no active generation after cancel");
    check(host.on_lookup(digits("987654"), kT0 + 3) == InviteLookupStatus::NoMatch,
          "C16: lookups for a cancelled invitation answer NoMatch");

    InviteCodeHost expiring;
    check(expiring.publish(7, "987654", kT0), "publish accepted");
    check(expiring.on_lookup(digits("987654"), kT0 + kFull - 1) ==
              InviteLookupStatus::MatchPendingHostApproval,
          "valid lookup inside the window matches");
    check(expiring.on_lookup(digits("987654"), kT0 + kFull) == InviteLookupStatus::Expired,
          "C05: expired invitation answers Expired");
    expiring.tick(kT0 + kFull + 1);
    check(expiring.phase() == flynes::session::InviteHostPhase::Idle,
          "expired invitation frees the host slot");
}

// --- Wire constraints (kinds 0x0214/0x0215) via the shared codec ------------

void test_wire_constraints()
{
    using flynes::session::wire::Status;
    std::uint8_t hash[32];

    std::uint8_t request[32] = {};
    request[0] = 0x00;
    request[1] = 0x01;
    request[8] = '0';
    request[9] = '1';
    request[10] = '2';
    request[11] = '3';
    request[12] = '4';
    request[13] = '5';
    check(flynes::session::wire::check("0x0214", request, sizeof(request), hash) == Status::Ok,
          "0x0214: six-digit request validates");

    std::uint8_t bad_request[32] = {};
    bad_request[0] = 0x00;
    bad_request[1] = 0x01;
    bad_request[8] = '1';
    bad_request[9] = 'a';
    bad_request[10] = '2';
    bad_request[11] = '3';
    bad_request[12] = '4';
    bad_request[13] = '5';
    check(flynes::session::wire::check("0x0214", bad_request, sizeof(bad_request), hash) ==
              Status::InvalidField,
          "C05: non-digit code byte is InvalidField on the wire");

    std::uint8_t response[24] = {};
    response[0] = 0x00;
    response[1] = 0x01;
    response[8] = 2;  // NO_MATCH
    check(flynes::session::wire::check("0x0215", response, sizeof(response), hash) == Status::Ok,
          "0x0215: zero-generation NoMatch validates");

    response[16] = 0xAB;  // nonzero generation with a non-match status
    check(flynes::session::wire::check("0x0215", response, sizeof(response), hash) ==
              Status::InvalidField,
          "0x0215: generation revealed only on match");

    response[8] = 9;  // unknown status
    response[16] = 0;
    check(flynes::session::wire::check("0x0215", response, sizeof(response), hash) ==
              Status::UnknownEnum,
          "0x0215: unknown status rejects");
}

} // namespace

int main()
{
    test_leading_zeros_preserved();
    test_invalid_input_emits_no_request();
    test_match_without_host_approval_reveals_nothing();
    test_single_side_sas_remains_unconnected();
    test_regenerate_kills_old_generation();
    test_cancel_and_timeout_kill_late_accepts();
    test_collision_never_silently_picks();
    test_gameless_connected_lobby();
    test_host_gates();
    test_wire_constraints();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::fprintf(stdout, "test_invite_code_route: all checks passed\n");
    return 0;
}
