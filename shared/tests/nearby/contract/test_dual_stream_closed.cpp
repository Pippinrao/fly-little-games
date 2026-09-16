/*
 * Task 10, step 3: STREAM is closed explicitly in this release.
 *
 * The DUAL MVP may not advertise, negotiate or call any STREAM/media path. This
 * test is the DUAL gate's own record of that closure. It asserts three things:
 *
 *   1. the machine's capability advertisement is DUAL and nothing else, and a
 *      peer that can only offer STREAM is refused with an explicit
 *      "unsupported by this release" reason rather than silently accepted;
 *   2. the DUAL mode discriminator admits DUAL alone: HOST_STREAM is refused
 *      with FLY_SESSION_V2_UNAVAILABLE;
 *   3. structurally, the new public DUAL runtime seam ends at `state_digest` and
 *      the public provider table ends at the DUAL runtime slot, so there is no
 *      codec, encoder, decoder, sink, video or audio entry point in the public
 *      session ABI for a STREAM call to reach.
 *
 * Point 3 is a compile-time property of the ABI, which is why it is asserted
 * with static_assert here instead of counted at runtime: the acceptance record
 * carries the separate source audit that no codec/media provider call exists in
 * shared/src/session (see out/logs/task10-integration-notes.md).
 */

#include "dual/dual_runtime_contract.hpp"
#include "flynes/flynes_session.h"
#include "link/link_control_contract.hpp"

#include <cstdint>
#include <cstdio>

namespace {

namespace link = flynes::session::link;
namespace dual = flynes::session::dual;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

/* ------------------------------------------------------- 3. structural pins */

/*
 * The DUAL runtime table's last member is `state_digest`. Appending anything
 * after it (a codec, an encoder, a sink) breaks the ABI shape this build
 * promises, so the assert fails rather than the addition slipping in.
 */
static_assert(FLY_SESSION_DUAL_RUNTIME_PORT_V2_SIZE ==
                  offsetof(fly_session_dual_runtime_port_v2, state_digest) +
                      sizeof(((fly_session_dual_runtime_port_v2*)0)->state_digest),
              "the DUAL runtime provider ends at state_digest");

/* The public provider table's last member is the DUAL runtime slot. */
static_assert(FLY_SESSION_PORTS_V2_SIZE ==
                  FLY_SESSION_PORTS_V2_R1_SIZE +
                      sizeof(((fly_session_ports_v2*)0)->dual_runtime),
              "the provider table ends at the DUAL runtime slot");

/* The DUAL content/bundle/outcome/digest values carry no media surface. */
static_assert(FLY_SESSION_DUAL_INPUT_BUNDLE_V2_SIZE ==
                  offsetof(fly_session_dual_input_bundle_v2, ports) +
                      FLY_SESSION_DUAL_PORT_COUNT_V2 *
                          sizeof(fly_session_dual_port_sample_v2),
              "the DUAL bundle ends at its four port samples");
static_assert(FLY_SESSION_DUAL_STATE_DIGEST_V2_SIZE == 96,
              "the DUAL state digest is state||frame||pcm");

/*
 * The capability vocabulary this release advertises contains DUAL and nothing
 * else, and the two STREAM bits are known-but-unsupported rather than unknown.
 */
static_assert(link::kLinkSupportedCapabilityMaskV1 == link::kLinkCapabilityDualV1,
              "this release advertises DUAL only");
static_assert((link::kLinkSupportedCapabilityMaskV1 &
               link::kLinkCapabilityStreamVideoV1) == 0,
              "STREAM video is never advertised");
static_assert((link::kLinkSupportedCapabilityMaskV1 &
               link::kLinkCapabilityStreamAudioV1) == 0,
              "STREAM audio is never advertised");
static_assert(link::kLinkKnownCapabilityMaskV1 ==
                  static_cast<std::uint16_t>(
                      link::kLinkCapabilityDualV1 |
                      link::kLinkCapabilityStreamVideoV1 |
                      link::kLinkCapabilityStreamAudioV1),
              "STREAM bits stay known, so a STREAM-only peer is refused with a "
              "reason instead of being treated as an unknown extension");
static_assert(dual::kDualSupportedModeMaskV1 == 1u,
              "only the DUAL mode discriminant is selectable");

/* ------------------------------------------------------- 1. capability gate */

void stream_is_refused_with_an_explicit_reason()
{
    check(link::evaluate_link_proposal_v1(link::kLinkCapabilityDualV1) ==
              link::LinkProposalSupportV1::Supported,
          "a DUAL proposal is supported");

    const auto stream_only = static_cast<std::uint16_t>(
        link::kLinkCapabilityStreamVideoV1 | link::kLinkCapabilityStreamAudioV1);
    check(link::evaluate_link_proposal_v1(stream_only) ==
              link::LinkProposalSupportV1::UnsupportedByThisRelease,
          "a peer that offers only STREAM is refused as unsupported by this "
          "release");
    check(link::evaluate_link_proposal_v1(link::kLinkCapabilityStreamVideoV1) ==
              link::LinkProposalSupportV1::UnsupportedByThisRelease,
          "a STREAM video only proposal is refused as unsupported");
    check(link::evaluate_link_proposal_v1(link::kLinkCapabilityStreamAudioV1) ==
              link::LinkProposalSupportV1::UnsupportedByThisRelease,
          "a STREAM audio only proposal is refused as unsupported");
    check(link::evaluate_link_proposal_v1(0) ==
              link::LinkProposalSupportV1::UnsupportedByThisRelease,
          "an empty proposal is refused as unsupported");

    /* A bit this release does not know is a different, harder refusal. */
    check(link::evaluate_link_proposal_v1(0x8000) ==
              link::LinkProposalSupportV1::UnknownCriticalCapability,
          "an unknown capability bit is an unknown critical capability");
    check(link::evaluate_link_proposal_v1(static_cast<std::uint16_t>(
              link::kLinkCapabilityDualV1 | 0x8000)) ==
              link::LinkProposalSupportV1::UnknownCriticalCapability,
          "an unknown bit is refused even alongside a supported one");
    check(link::evaluate_link_proposal_v1(static_cast<std::uint16_t>(
              link::kLinkCapabilityDualV1 | link::kLinkCapabilityStreamVideoV1 |
              link::kLinkCapabilityStreamAudioV1)) ==
              link::LinkProposalSupportV1::Supported,
          "a DUAL plus STREAM proposal still negotiates DUAL: the STREAM bits "
          "are known, this release never selects STREAM, and the local "
          "advertisement never carries them, so a peer asking for STREAM can "
          "never open a STREAM path");

    /* The two refusal values are distinct, so the reason is not conflated. */
    check(link::LinkProposalSupportV1::UnsupportedByThisRelease !=
              link::LinkProposalSupportV1::UnknownCriticalCapability,
          "unsupported and unknown are distinct reasons");
}

/* ------------------------------------------------------------ 2. mode gate */

void host_stream_mode_is_refused()
{
    check(dual::evaluate_dual_mode_v1(dual::DualModeV1::Dual) ==
              FLY_SESSION_V2_OK,
          "DUAL mode is selectable");
    check(dual::evaluate_dual_mode_v1(dual::DualModeV1::HostStream) ==
              FLY_SESSION_V2_UNAVAILABLE,
          "HOST_STREAM mode is refused as unavailable");
    check((dual::kDualSupportedModeMaskV1 &
           (1u << (static_cast<std::uint8_t>(dual::DualModeV1::HostStream) -
                   1u))) == 0,
          "the supported mode mask excludes HOST_STREAM");
}

/*
 * The public action vocabulary has no STREAM start: the only run-start kinds are
 * the DUAL selector and starter appended in step 1.
 */
void the_public_action_vocabulary_has_no_stream_start()
{
    check(FLY_SESSION_ACTION_START_DUAL_V2 == 43 &&
              FLY_SESSION_ACTION_SELECT_CONTENT_V2 == 42,
          "the DUAL controls keep their appended discriminants");
    check(FLY_SESSION_DUAL_MODE_DUAL_V2 == 1 &&
              FLY_SESSION_DUAL_MODE_NONE_V2 == 0,
          "the published DUAL mode vocabulary admits DUAL alone");
    check(FLY_SESSION_DUAL_FREEZE_NONE_V2 == 0 &&
              FLY_SESSION_DUAL_FREEZE_DIGEST_MISMATCH_V2 == 4 &&
              FLY_SESSION_DUAL_FREEZE_TRANSPORT_TERMINAL_V2 == 6 &&
              FLY_SESSION_DUAL_FREEZE_SHUTDOWN_V2 == 8,
          "the published freeze vocabulary matches the frozen DUAL seam, so a "
          "freeze can never be reported as a silent mode switch");
}

} // namespace

int main()
{
    stream_is_refused_with_an_explicit_reason();
    host_stream_mode_is_refused();
    the_public_action_vocabulary_has_no_stream_start();
    if (failures != 0)
    {
        std::fprintf(stderr, "nearby_dual_stream_closed: %d failure(s)\n",
                     failures);
        return 1;
    }
    std::puts("nearby_dual_stream_closed: PASS");
    return 0;
}
