#ifndef FLYNES_TESTS_NEARBY_HARNESS_CRASHABLE_OBJECT_STORE_HPP
#define FLYNES_TESTS_NEARBY_HARNESS_CRASHABLE_OBJECT_STORE_HPP

/*
 * W3 harness: durable-store fault injection for the two-engine recovery
 * scenarios.
 *
 * This is a *policy* object, not a second store implementation. The store
 * itself is the reference ObjectStore stub inside EngineFixture (an in-memory
 * last-value store carved out of the frozen reference test); this header only
 * decides what the harness does around a call to it, so object-store semantics
 * stay defined in exactly one place.
 *
 * Faults modelled (all of them are cases a real durable store can produce):
 *   * a lost completion after the write was already durable (the engine must
 *     re-drive, not lose data);
 *   * a stale completion for a superseded operation (the engine must reclaim
 *     the OLD resource, never a new one);
 *   * a terminal failure that must surface as an explicit error rather than a
 *     silent success.
 */

#include <cstdint>

namespace flynes::tests::nearby {

enum class DurableFaultV1 : std::uint8_t
{
    None = 0,
    /* The write succeeded durably but its completion never arrives. */
    CompletionLostAfterDurable = 1,
    /* A completion arrives for an operation that was already superseded. */
    StaleCompletion = 2,
    /* The store fails terminally; the engine must fail closed. */
    TerminalFailure = 3
};

/*
 * Per-store fault policy. The harness reads exactly one decision per put, so a
 * scenario can say "the third put is lost" and get a deterministic sequence.
 */
struct CrashableObjectStorePolicyV1 final
{
    DurableFaultV1 fault = DurableFaultV1::None;
    /* 1-based put index the fault applies to; 0 means "every put". */
    std::uint64_t fault_at_put = 0;

    std::uint64_t puts_seen = 0;
    std::uint64_t faults_injected = 0;
    std::uint64_t stale_completions_injected = 0;

    /* Must the harness swallow this put's completion? */
    bool swallow_completion() noexcept
    {
        if (fault != DurableFaultV1::CompletionLostAfterDurable)
            return false;
        if (fault_at_put != 0 && fault_at_put != puts_seen)
            return false;
        ++faults_injected;
        return true;
    }

    /* Must the harness replay a completion for a superseded operation? */
    bool inject_stale_completion() noexcept
    {
        if (fault != DurableFaultV1::StaleCompletion)
            return false;
        if (fault_at_put != 0 && fault_at_put != puts_seen)
            return false;
        ++faults_injected;
        ++stale_completions_injected;
        return true;
    }

    bool fail_terminal() const noexcept
    {
        return fault == DurableFaultV1::TerminalFailure &&
               (fault_at_put == 0 || fault_at_put == puts_seen);
    }

    void note_put() noexcept { ++puts_seen; }
    void reset() noexcept
    {
        fault = DurableFaultV1::None;
        fault_at_put = 0;
        puts_seen = 0;
        faults_injected = 0;
        stale_completions_injected = 0;
    }
};

} // namespace flynes::tests::nearby

#endif
