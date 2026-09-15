#ifndef FLYNES_TESTS_NEARBY_HARNESS_CRASHABLE_SECURE_STORE_HPP
#define FLYNES_TESTS_NEARBY_HARNESS_CRASHABLE_SECURE_STORE_HPP

/*
 * W3 harness: secure-store fault injection for the two-engine recovery
 * scenarios.
 *
 * Same shape and the same rationale as crashable_object_store.hpp: the store
 * implementation stays the reference SecureStore stub inside EngineFixture, and
 * this header only decides which faults the harness injects around it.
 *
 * The secure store is the one that must never lie: a compare-and-replace that
 * reported success without being durable would let a link resume with stale
 * key material. These faults therefore all demand an explicit, observable
 * engine outcome.
 */

#include "crashable_object_store.hpp"

namespace flynes::tests::nearby {

enum class SecureStoreFaultV1 : std::uint8_t
{
    None = 0,
    /* The record was written but its completion never arrives. */
    CompletionLostAfterDurable = 1,
    /* The record's revision moved on: a compare-and-replace must fail. */
    RevisionConflict = 2,
    /* The device key is gone (factory reset, restored backup, wipe). */
    MaterialMissing = 3,
    /* The store fails terminally; the engine must fail closed. */
    TerminalFailure = 4
};

struct CrashableSecureStorePolicyV1 final
{
    SecureStoreFaultV1 fault = SecureStoreFaultV1::None;
    /* 1-based compare-replace index the fault applies to; 0 means "every". */
    std::uint64_t fault_at_write = 0;

    std::uint64_t writes_seen = 0;
    std::uint64_t faults_injected = 0;

    bool swallow_completion() noexcept
    {
        if (fault != SecureStoreFaultV1::CompletionLostAfterDurable)
            return false;
        if (fault_at_write != 0 && fault_at_write != writes_seen)
            return false;
        ++faults_injected;
        return true;
    }

    bool conflicts() const noexcept
    {
        return fault == SecureStoreFaultV1::RevisionConflict &&
               (fault_at_write == 0 || fault_at_write == writes_seen);
    }

    bool read_fails() const noexcept
    {
        return fault == SecureStoreFaultV1::MaterialMissing ||
               fault == SecureStoreFaultV1::TerminalFailure;
    }

    void note_write() noexcept { ++writes_seen; }
    void reset() noexcept
    {
        fault = SecureStoreFaultV1::None;
        fault_at_write = 0;
        writes_seen = 0;
        faults_injected = 0;
    }
};

/*
 * Both policies share the same "which call is this" bookkeeping, so a scenario
 * can drive one index generator over both stores without caring which is which.
 */
using CrashableStoreFaultV1 = DurableFaultV1;

} // namespace flynes::tests::nearby

#endif
