/*
 * Proves the Rust nearby QUIC provider is genuinely built and linked into a
 * C++ test binary.
 *
 * Why this exists: before 2026-09-15 nothing in shared/CMakeLists.txt
 * referenced shared/nearby-quic-provider at all, so no CTest suite had ever
 * executed real provider code. The two-engine harness needs a real loopback
 * QUIC transport, and "we added a link line" is not evidence that the
 * staticlib loads. This test calls across the C ABI and asserts the provider's
 * own argument validation, which can only succeed if the real library ran.
 */

#include "flynes_quic_provider.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;

void check(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

struct CallbackCounters
{
    int retains = 0;
    int releases = 0;
    int completions = 0;
};

void context_retain(void* context)
{
    if (context != nullptr)
        static_cast<CallbackCounters*>(context)->retains += 1;
}

void context_release(void* context)
{
    if (context != nullptr)
        static_cast<CallbackCounters*>(context)->releases += 1;
}

void quic_completion(void* context, std::uint64_t resource, std::int32_t result,
                     std::uint32_t payload_kind, std::uint64_t sequence,
                     const std::uint8_t* bytes, std::size_t size)
{
    (void)resource;
    (void)result;
    (void)payload_kind;
    (void)sequence;
    (void)bytes;
    (void)size;
    if (context != nullptr)
        static_cast<CallbackCounters*>(context)->completions += 1;
}

FlynesQuicCallbacks complete_callbacks(CallbackCounters* counters)
{
    FlynesQuicCallbacks callbacks{};
    callbacks.struct_size =
        static_cast<std::uint32_t>(sizeof(FlynesQuicCallbacks));
    callbacks.abi_version = FLYNES_QUIC_PROVIDER_ABI_V1;
    callbacks.context = counters;
    callbacks.retain = &context_retain;
    callbacks.release = &context_release;
    callbacks.completion = &quic_completion;
    return callbacks;
}

/* The provider validates its own callback table, so a null return here is the
 * library actively rejecting the input: proof that the real code executed. */
void rejection_contract()
{
    check(flynes_quic_provider_create(nullptr) == nullptr,
          "a null callback table is rejected");

    CallbackCounters counters{};
    auto callbacks = complete_callbacks(&counters);

    callbacks.struct_size =
        static_cast<std::uint32_t>(sizeof(FlynesQuicCallbacks)) - 1u;
    check(flynes_quic_provider_create(&callbacks) == nullptr,
          "a short callback table is rejected");

    callbacks = complete_callbacks(&counters);
    callbacks.abi_version = FLYNES_QUIC_PROVIDER_ABI_V1 + 1u;
    check(flynes_quic_provider_create(&callbacks) == nullptr,
          "a mismatched callback ABI version is rejected");

    callbacks = complete_callbacks(&counters);
    callbacks.retain = nullptr;
    check(flynes_quic_provider_create(&callbacks) == nullptr,
          "a missing retain callback is rejected");

    callbacks = complete_callbacks(&counters);
    callbacks.release = nullptr;
    check(flynes_quic_provider_create(&callbacks) == nullptr,
          "a missing release callback is rejected");

    callbacks = complete_callbacks(&counters);
    callbacks.completion = nullptr;
    check(flynes_quic_provider_create(&callbacks) == nullptr,
          "a missing completion callback is rejected");

    /* None of the rejections may have touched the context. */
    check(counters.retains == 0 && counters.releases == 0,
          "rejected callback tables never retain or release the context");
}

/* A complete table must succeed and the provider must own exactly one
 * reference, released exactly once by the matching release call. */
void lifecycle_contract()
{
    CallbackCounters counters{};
    const auto callbacks = complete_callbacks(&counters);
    FlynesQuicProvider* provider = flynes_quic_provider_create(&callbacks);
    check(provider != nullptr,
          "a complete callback table creates a live provider");
    if (provider == nullptr)
        return;

    check(counters.retains == 1 && counters.releases == 0,
          "create retains the callback context exactly once");

    /* Release through a null handle must be a harmless no-op. */
    flynes_quic_provider_release(nullptr);
    check(counters.releases == 0,
          "releasing a null provider does not release the context");

    flynes_quic_provider_retain(provider);
    flynes_quic_provider_release(provider);
    check(counters.releases == 0,
          "an extra reference keeps the context alive");

    flynes_quic_provider_release(provider);
    check(counters.releases == 1,
          "the final release drops the callback context exactly once");
    check(counters.completions == 0,
          "creating and destroying a provider raises no completion");
}

} // namespace

int main()
{
    rejection_contract();
    lifecycle_contract();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d QUIC provider linkage checks failed\n",
                     failures);
        return 1;
    }
    std::puts("QUIC provider linkage tests passed");
    return 0;
}
