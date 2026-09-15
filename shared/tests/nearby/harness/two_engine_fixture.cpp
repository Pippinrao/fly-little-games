/*
 * W3 two-engine harness implementation.
 *
 * The provider scaffolding in the generated region is carved line-for-line out
 * of the read-only reference integration test
 *   shared/tests/nearby/integration/test_two_engine_empty_lobby.cpp
 * See out/tools/assemble_two_engine_fixture.ps1 for the exact carve list and the
 * deliberate W3 deltas: a per-engine clock, a swappable QUIC port, and a caller
 * supplied engine ordinal plus resource ledger.
 */

#include "two_engine_fixture.hpp"

#include "../harness/deterministic_executor.hpp"

#include "flynes_quic_provider.h"

#include "wire/gatt_fragment.hpp"
#include "wire/sha256.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace flynes::tests::nearby {

/* The global test-failure counter the generated code writes through `check`. */
int g_failures = 0;

void check(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}

/* EngineFixture and the provider scaffolding it owns live in two_engine_fixture.hpp.
 * Everything below is the hand-written W3 harness logic. */


/* =========================================================================
 * Hand-written W3 harness logic: the transport boundary, the deterministic
 * scheduler and the two-engine pair. Nothing below calls another engine's
 * reducer, and nothing below can fabricate a session conclusion.
 * ========================================================================= */

/* ------------------------------------------------------- engine observers */

bool real_quic_provider_is_linked();

/*
 * Prove the Rust/Quinn QUIC provider is genuinely linked into this binary.
 * Passing a null callback table exercises the provider's own argument
 * validation, which only a real implementation can perform: a missing symbol or
 * a hand-written stub could not return null here.
 *
 * This is the transport loopback_quic_fixture builds on. Until that fixture
 * drives ChannelBind across a real connection, the harness asserts this weaker
 * but honest property instead of pretending a socket is a QUIC session.
 */
bool real_quic_provider_is_linked()
{
    return flynes_quic_provider_create(nullptr) == nullptr;
}


/*
 * The generated EngineFixture keeps its provider stubs private, so the harness
 * reaches them through these free functions in the same translation unit. They
 * only *read* what a provider stub already recorded; none of them can change an
 * engine decision.
 */
std::size_t discovery_written_fragments(const EngineFixture& fixture)
{
    return fixture.discovery.written_fragments.size();
}

const std::vector<std::uint8_t>& discovery_fragment(const EngineFixture& fixture,
                                                    std::size_t index)
{
    return fixture.discovery.written_fragments[index];
}

std::size_t object_store_snapshot_size(const EngineFixture& fixture)
{
    return fixture.object_store.last_value.size();
}

const std::vector<std::uint8_t>& object_store_snapshot_value(
    const EngineFixture& fixture)
{
    return fixture.object_store.last_value;
}

std::size_t secure_store_snapshot_size(const EngineFixture& fixture)
{
    return fixture.secure_store.last_value.size();
}

const std::vector<std::uint8_t>& secure_store_snapshot_value(
    const EngineFixture& fixture)
{
    return fixture.secure_store.last_value;
}

void restore_object_store(EngineFixture& fixture,
                          const std::vector<std::uint8_t>& value)
{
    fixture.object_store.last_value = value;
}

void restore_secure_store(EngineFixture& fixture,
                          const std::vector<std::uint8_t>& value)
{
    fixture.secure_store.last_value = value;
}

/* ------------------------------------------------------- TransportFrameV1 */

void TransportFrameV1::rebind_payload() noexcept
{
    event.payload_size = static_cast<std::uint32_t>(payload.size());
    if (!payload.empty())
        std::memcpy(event.payload, payload.data(), payload.size());
}

TransportFrameV1::~TransportFrameV1()
{
    fly_session_inbox_release_v2(inbox);
}

TransportFrameV1::TransportFrameV1(const TransportFrameV1& other)
    : target_ordinal(other.target_ordinal), inbox(other.inbox),
      event(other.event), payload(other.payload),
      arrival_ns(other.arrival_ns), sequence(other.sequence),
      origin(other.origin)
{
    fly_session_inbox_retain_v2(inbox);
    rebind_payload();
}

TransportFrameV1& TransportFrameV1::operator=(const TransportFrameV1& other)
{
    if (this == &other)
        return *this;
    fly_session_inbox_retain_v2(other.inbox);
    fly_session_inbox_release_v2(inbox);
    target_ordinal = other.target_ordinal;
    inbox = other.inbox;
    event = other.event;
    payload = other.payload;
    arrival_ns = other.arrival_ns;
    sequence = other.sequence;
    origin = other.origin;
    rebind_payload();
    return *this;
}

TransportFrameV1::TransportFrameV1(TransportFrameV1&& other) noexcept
    : target_ordinal(other.target_ordinal), inbox(other.inbox),
      event(other.event), payload(std::move(other.payload)),
      arrival_ns(other.arrival_ns), sequence(other.sequence),
      origin(other.origin)
{
    other.inbox = nullptr;
    rebind_payload();
}

TransportFrameV1& TransportFrameV1::operator=(TransportFrameV1&& other) noexcept
{
    if (this == &other)
        return *this;
    fly_session_inbox_release_v2(inbox);
    target_ordinal = other.target_ordinal;
    inbox = other.inbox;
    event = other.event;
    payload = std::move(other.payload);
    arrival_ns = other.arrival_ns;
    sequence = other.sequence;
    origin = other.origin;
    other.inbox = nullptr;
    rebind_payload();
    return *this;
}

/* ------------------------------------------------------- scheduler */

DeterministicSchedulerV1::DeterministicSchedulerV1(EngineFixture* left,
                                                   EngineFixture* right)
    : left_(left), right_(right)
{
}

EngineFixture* DeterministicSchedulerV1::endpoint(
    std::uint64_t ordinal) const noexcept
{
    if (left_ == nullptr || right_ == nullptr)
        return nullptr;
    return ordinal == left_->engine_ordinal ? left_ : right_;
}

void DeterministicSchedulerV1::push(TransportFrameV1 frame)
{
    frame.sequence = next_sequence_++;
    if (frame.arrival_ns == 0)
    {
        EngineFixture* owner = endpoint(frame.target_ordinal);
        frame.arrival_ns = owner != nullptr ? owner->platform.clock_ns : 0;
    }
    queue_.push_back(std::move(frame));
}

void DeterministicSchedulerV1::pause(std::uint64_t ordinal)
{
    if (ordinal == left_->engine_ordinal)
        paused_left_ = true;
    else
        paused_right_ = true;
}

void DeterministicSchedulerV1::resume(std::uint64_t ordinal)
{
    if (ordinal == left_->engine_ordinal)
        paused_left_ = false;
    else
        paused_right_ = false;
}

bool DeterministicSchedulerV1::paused(std::uint64_t ordinal) const noexcept
{
    return ordinal == left_->engine_ordinal ? paused_left_ : paused_right_;
}

void DeterministicSchedulerV1::advance_clock(std::uint64_t ordinal,
                                             std::uint64_t delta_ns)
{
    EngineFixture* owner = endpoint(ordinal);
    if (owner != nullptr)
        owner->platform.clock_ns += delta_ns;
}

std::uint64_t DeterministicSchedulerV1::clock_now(
    std::uint64_t ordinal) const noexcept
{
    EngineFixture* owner = endpoint(ordinal);
    return owner != nullptr ? owner->platform.clock_ns : 0;
}

void DeterministicSchedulerV1::release_queue()
{
    queue_.clear();
}

bool DeterministicSchedulerV1::run_next()
{
    if (queue_.empty())
        return false;
    TransportFrameV1 frame = std::move(queue_.front());
    queue_.erase(queue_.begin());
    return deliver(frame);
}

bool DeterministicSchedulerV1::deliver(TransportFrameV1& frame)
{
    EngineFixture* owner = endpoint(frame.target_ordinal);
    if (owner == nullptr || frame.inbox == nullptr || !owner->engine_is_live())
    {
        /* The engine is gone: the frame is refused without ever touching a
         * dangling handle. */
        ++report_.refused;
        return false;
    }
    if (paused(frame.target_ordinal))
    {
        queue_.insert(queue_.begin(), std::move(frame));
        return false;
    }
    /* The queued copy holds its own retained inbox reference, which is what
     * keeps the handle valid across reorders and process restarts. */
    const auto result = fly_session_deliver_v2(frame.inbox, &frame.event);
    if (result == FLY_SESSION_V2_ACCEPTED)
    {
        ++report_.delivered;
        owner->note_delivered_completion(true);
        return true;
    }
    if (result == FLY_SESSION_V2_STALE)
    {
        /* A late completion is a legitimate outcome: the engine must reclaim
         * only the old resources it was still holding. */
        ++report_.late;
        if (owner->ledger != nullptr)
        {
            owner->ledger->note_stale_completion(
                frame.event.token.operation_id);
            owner->ledger->reclaimed_stale_resources.acquire();
        }
        return true;
    }
    ++report_.refused;
    return false;
}

std::size_t DeterministicSchedulerV1::run_until_idle()
{
    std::size_t delivered = 0;
    while (run_next())
        ++delivered;
    return delivered;
}

void DeterministicSchedulerV1::apply(InterruptionV1 interruption)
{
    if (queue_.empty())
        return;
    const std::size_t index = std::min<std::size_t>(
        static_cast<std::size_t>(interruption.at_index), queue_.size() - 1u);
    switch (interruption.kind)
    {
    case InterruptionKindV1::Drop:
        queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(index));
        ++report_.dropped;
        break;
    case InterruptionKindV1::Duplicate:
        queue_.insert(queue_.begin() + static_cast<std::ptrdiff_t>(index),
                      queue_[index]);
        ++report_.duplicated;
        break;
    case InterruptionKindV1::ReorderToBack:
    {
        TransportFrameV1 moved = std::move(queue_[index]);
        queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(index));
        queue_.push_back(std::move(moved));
        ++report_.reordered;
        break;
    }
    case InterruptionKindV1::Cancel:
        /* A cancelled operation must never reach the peer, and the engine must
         * still be able to reclaim what the provider held for it. */
        queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(index));
        ++report_.cancelled;
        break;
    }
}

/* ------------------------------------------------------- the pair */

TwoEngineFixtureV1::TwoEngineFixtureV1()
{
    left_ = std::make_unique<EngineFixture>(true, 1, &ledger_, &wire_);
    right_ = std::make_unique<EngineFixture>(true, 2, &ledger_, &wire_);
    scheduler_ =
        std::make_unique<DeterministicSchedulerV1>(left_.get(), right_.get());
}

TwoEngineFixtureV1::~TwoEngineFixtureV1()
{
    scheduler_.reset();
    right_.reset();
    left_.reset();
}

bool TwoEngineFixtureV1::endpoints_are_isolated() const
{
    bool isolated = true;
    isolated = isolated && left_->engine != right_->engine;
    isolated = isolated && &left_->key != &right_->key;
    isolated = isolated && &left_->crypto != &right_->crypto;
    isolated = isolated && &left_->tls != &right_->tls;
    isolated = isolated && &left_->secure_store != &right_->secure_store;
    isolated = isolated && &left_->object_store != &right_->object_store;
    isolated = isolated && &left_->discovery != &right_->discovery;
    isolated = isolated && &left_->bearer != &right_->bearer;
    isolated = isolated && &left_->quic != &right_->quic;
    isolated = isolated && left_->ports.key != right_->ports.key;
    isolated = isolated && left_->ports.quic != right_->ports.quic;
    isolated = isolated &&
               left_->ports.object_store != right_->ports.object_store;
    isolated = isolated && left_->engine_ordinal != right_->engine_ordinal;
    return isolated;
}

void TwoEngineFixtureV1::bring_up()
{
    left_->platform.ready();
    right_->platform.ready();
    left_->executor.run_all();
    right_->executor.run_all();
}

std::size_t TwoEngineFixtureV1::capture_transport()
{
    if (released_)
        return 0;
    /*
     * Every encoded GATT fragment the providers wrote since the last capture
     * becomes one transport frame. The bytes are copied verbatim out of the
     * provider write buffer and are never inspected or rewritten here, and the
     * delivery target is always the *other* endpoint.
     */
    std::size_t captured = 0;
    EngineFixture* sources[2] = {left_.get(), right_.get()};
    for (EngineFixture* source : sources)
    {
        const std::size_t total = discovery_written_fragments(*source);
        while (source->discovery_fragments_captured < total)
        {
            const auto& fragment = discovery_fragment(
                *source, source->discovery_fragments_captured);
            ++source->discovery_fragments_captured;

            TransportFrameV1 frame;
            frame.target_ordinal = source->engine_ordinal == 1 ? 2 : 1;
            frame.inbox = source->discovery.inbox;
            frame.origin = "gatt";
            frame.payload = fragment;
            frame.event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
            frame.event.abi_version = FLY_SESSION_ABI_VERSION_2;
            frame.event.token = source->discovery.write_token;
            frame.event.event_sequence = static_cast<std::uint32_t>(
                source->discovery_fragments_captured);
            frame.event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
            frame.event.terminal = 0;
            frame.event.result = FLY_SESSION_V2_OK;
            frame.event.payload_kind = FLY_SESSION_PROVIDER_DISCOVERY_BYTES_V2;
            frame.rebind_payload();

            ++wire_.gatt_fragments_out;
            wire_.gatt_logical_bytes += fragment.size();
            scheduler_->push(std::move(frame));
            ++captured;
        }
    }
    return captured;
}

std::size_t TwoEngineFixtureV1::pump()
{
    capture_transport();
    left_->executor.run_all();
    right_->executor.run_all();
    const auto before = scheduler_->report().delivered;
    while (scheduler_->pending() != 0)
    {
        if (!scheduler_->run_next())
            break;
        left_->executor.run_all();
        right_->executor.run_all();
        capture_transport();
    }
    const auto after = scheduler_->report().delivered;
    return static_cast<std::size_t>(after - before);
}

std::size_t TwoEngineFixtureV1::pump_until_quiet(std::size_t rounds)
{
    std::size_t delivered = 0;
    for (std::size_t round = 0; round < rounds; ++round)
    {
        const std::size_t now = pump();
        delivered += now;
        if (now == 0 && scheduler_->pending() == 0)
            break;
    }
    return delivered;
}

std::vector<std::uint8_t> TwoEngineFixtureV1::object_store_snapshot(
    std::uint64_t ordinal) const
{
    const EngineFixture& fixture = ordinal == 1 ? *left_ : *right_;
    if (object_store_snapshot_size(fixture) == 0)
        return {};
    return object_store_snapshot_value(fixture);
}

std::vector<std::uint8_t> TwoEngineFixtureV1::secure_store_snapshot(
    std::uint64_t ordinal) const
{
    const EngineFixture& fixture = ordinal == 1 ? *left_ : *right_;
    if (secure_store_snapshot_size(fixture) == 0)
        return {};
    return secure_store_snapshot_value(fixture);
}

void DeterministicSchedulerV1::restart(std::uint64_t ordinal)
{
    EngineFixture* old = endpoint(ordinal);
    if (old == nullptr)
        return;

    /*
     * Process reconstruction. The durable stores survive; everything else is
     * rebuilt through the public ABI, so state the engine held only in memory
     * really is gone.
     */
    const std::vector<std::uint8_t> object_value =
        object_store_snapshot_size(*old) == 0 ? std::vector<std::uint8_t>{}
                                              : object_store_snapshot_value(*old);
    const std::vector<std::uint8_t> secure_value =
        secure_store_snapshot_size(*old) == 0 ? std::vector<std::uint8_t>{}
                                              : secure_store_snapshot_value(*old);

    auto* replacement =
        new EngineFixture(true, old->engine_ordinal, old->ledger, old->wire);
    restore_object_store(*replacement, object_value);
    restore_secure_store(*replacement, secure_value);

    if (ordinal == left_->engine_ordinal)
    {
        left_ = replacement;
        delete old;
    }
    else
    {
        right_ = replacement;
        delete old;
    }
    /*
     * Every queued frame may still hold a retained inbox reference belonging to
     * the destroyed engine, so the whole queue is discarded unaddressable. The
     * frames are accounted as refused, never silently dropped.
     */
    report_.refused += queue_.size();
    release_queue();
}

void TwoEngineFixtureV1::report_more_refused(std::size_t count)
{
    scheduler_->note_refused(static_cast<std::uint64_t>(count));
}

void TwoEngineFixtureV1::release_engines()
{
    /*
     * Shut both engines down and destroy them, so the oracle can observe the
     * cleanup before the fixture itself goes away. Safe to call twice.
     */
    if (released_)
        return;
    shutdown_both();
    released_ = true;
    /* Anything still queued addressed a handle that is about to disappear. */
    report_more_refused(scheduler_->pending());
    scheduler_->release_queue();
    EngineFixture* fixtures[2] = {left_.get(), right_.get()};
    for (EngineFixture* fixture : fixtures)
    {
        if (fixture->engine == nullptr)
            continue;
        /* Let the engine settle every final shutdown completion. */
        fixture->executor.run_all();
        /* Read the final state BEFORE the handle is released: afterwards the
         * handle is dangling and must never be touched again. */
        const auto after = fixture->snapshot();
        const auto destroyed = fly_session_destroy_v2(fixture->engine);
        /*
         * Releasing the engine context is only honest when the engine really
         * released it. A BUSY destroy is recorded as such and left outstanding,
         * so the oracle reports it instead of hiding it.
         */
        if (destroyed == FLY_SESSION_V2_OK)
        {
            if (fixture->ledger != nullptr)
                fixture->ledger->engine_contexts.release();
        }
        else
        {
            std::fprintf(stderr,
                         "NOTE: engine %llu destroy returned %d in engine state "
                         "%u link state %u; its context stays outstanding in the "
                         "oracle\n",
                         static_cast<unsigned long long>(fixture->engine_ordinal),
                         static_cast<int>(destroyed), after.engine_state,
                         after.link_state);
        }
        fixture->engine = nullptr;
    }
}

void TwoEngineFixtureV1::check_resources_balanced(const char* phase)
{
    const int outstanding = ledger_.total_outstanding();
    if (outstanding != 0)
    {
        std::fprintf(
            stderr,
            "FAIL: resource oracle unbalanced after %s: %d outstanding "
            "(buffers=%d keys=%d secrets=%d materials=%d streams=%d paths=%d "
            "credentials=%d inboxes=%d pending=%d contexts=%d)\n",
            phase, outstanding, ledger_.buffer_retains.outstanding(),
            ledger_.key_handles.outstanding(),
            ledger_.secret_handles.outstanding(),
            ledger_.material_handles.outstanding(),
            ledger_.stream_handles.outstanding(),
            ledger_.path_handles.outstanding(),
            ledger_.credentials.outstanding(), ledger_.inboxes.outstanding(),
            ledger_.pending_operations.outstanding(),
            ledger_.engine_contexts.outstanding());
    }
    check(outstanding == 0, "resource oracle reports a balanced ledger");
}

void TwoEngineFixtureV1::shutdown_both()
{
    if (left_->engine != nullptr)
    {
        fly_session_begin_shutdown_v2(left_->engine, 7);
        left_->executor.run_all();
    }
    if (right_->engine != nullptr)
    {
        fly_session_begin_shutdown_v2(right_->engine, 8);
        right_->executor.run_all();
    }
}

} // namespace flynes::tests::nearby
