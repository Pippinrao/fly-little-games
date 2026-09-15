#ifndef FLYNES_TEST_VERIFIED_PAIR_EVIDENCE_HPP
#define FLYNES_TEST_VERIFIED_PAIR_EVIDENCE_HPP

#include "initial_plan_lock.hpp"

namespace flynes::session {

// Legacy plan-lock tests start after authentication by design. This friend is
// linked only into test executables; production has no equivalent constructor.
struct VerifiedPairEvidenceTestFactory
{
    static VerifiedPairEvidence seal(VerifiedPairEvidence evidence) noexcept
    {
        evidence.seal_for_authenticated_pipeline();
        return evidence;
    }
};

} // namespace flynes::session

#endif
