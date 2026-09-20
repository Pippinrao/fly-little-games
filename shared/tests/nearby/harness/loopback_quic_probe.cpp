/*
 * W3 acceptance probe: one real loopback QUIC handshake through the product's
 * own Rust/Quinn provider.
 *
 * Sequence (mirrors the provider crate's own tests/ffi.rs so the fixture is
 * checked against the same behaviour the crate asserts):
 *   create -> register_tls_material -> listen -> accept -> connect
 *   -> inspect_handshake (both ends) -> exporter (both ends, equal)
 *   -> bidi stream round-trip -> datagram round-trip
 *   -> close every resource -> release -> signer retain/release balanced.
 *
 * Prints `PROBE <name>: PASS|FAIL` lines and a final `PROBE RESULT:`.
 * Exit code 0 only when every check passed.
 */

#include "flynes_quic_provider.h"
#include "loopback_quic_fixture.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using flynes::tests::nearby::LoopbackSignerAccountingV1;

int failures = 0;

void check(bool value, const char* name, const char* detail = "")
{
    if (value)
    {
        std::printf("PROBE %s: PASS\n", name);
        std::fflush(stdout);
        return;
    }
    std::printf("PROBE %s: FAIL %s\n", name, detail);
    std::fflush(stdout);
    ++failures;
}

struct Event final
{
    std::uint64_t operation = 0;
    std::int32_t result = 0;
    std::uint32_t flags = 0;
    std::uint64_t resource = 0;
    std::vector<std::uint8_t> bytes;
};

struct EventLog final
{
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<Event> events;

    void push(const Event& event)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            events.push_back(event);
        }
        changed.notify_all();
    }

    bool wait(std::uint64_t operation, std::chrono::milliseconds timeout,
              Event* out)
    {
        std::unique_lock<std::mutex> lock(mutex);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true)
        {
            for (auto it = events.begin(); it != events.end(); ++it)
            {
                if (it->operation == operation)
                {
                    *out = *it;
                    events.erase(it);
                    return true;
                }
            }
            if (changed.wait_until(lock, deadline) == std::cv_status::timeout)
                return false;
        }
    }
};

EventLog g_log;

extern "C" void probe_retain(void*) {}

extern "C" void probe_release(void*) {}

extern "C" void probe_completion(void*, std::uint64_t operation,
                                 std::int32_t result, std::uint32_t flags,
                                 std::uint64_t resource,
                                 const std::uint8_t* bytes, std::size_t size)
{
    Event event;
    event.operation = operation;
    event.result = result;
    event.flags = flags;
    event.resource = resource;
    if (bytes != nullptr && size != 0)
        event.bytes.assign(bytes, bytes + size);
    g_log.push(event);
}

constexpr std::chrono::milliseconds kWait{10000};

Event await(const char* name, std::uint64_t operation)
{
    Event event;
    if (!g_log.wait(operation, kWait, &event))
    {
        const std::string detail =
            std::string("operation ") + std::to_string(operation) +
            " timed out";
        check(false, name, detail.c_str());
        return event;
    }
    return event;
}

std::uint64_t be_u64(const std::uint8_t* bytes)
{
    std::uint64_t value = 0;
    for (int index = 0; index < 8; ++index)
        value = (value << 8u) | bytes[index];
    return value;
}

std::string as_string(const std::vector<std::uint8_t>& bytes)
{
    return std::string(reinterpret_cast<const char*>(bytes.data()),
                       bytes.size());
}

} // namespace

int main()
{
    using flynes::tests::nearby::loopback_spki_pin_v1;
    using flynes::tests::nearby::loopback_tls_identity_v1;
    using flynes::tests::nearby::loopback_tls_sign_callback_v1;
    using flynes::tests::nearby::loopback_tls_sign_release_v1;
    using flynes::tests::nearby::loopback_tls_sign_retain_v1;

    const auto& identity = loopback_tls_identity_v1();
    check(identity.spki_der.size() == 91, "identity-spki-is-91-bytes", "");
    check(identity.certificate_der.size() > 91 &&
              identity.certificate_der.size() <= 4096,
          "identity-certificate-is-bounded", "");
    check(!identity.private_key_der.empty(), "identity-private-key-loaded", "");

    FlynesQuicCallbacks callbacks{};
    callbacks.struct_size = sizeof(FlynesQuicCallbacks);
    callbacks.abi_version = FLYNES_QUIC_PROVIDER_ABI_V1;
    callbacks.context = nullptr;
    callbacks.retain = probe_retain;
    callbacks.release = probe_release;
    callbacks.completion = probe_completion;

    FlynesQuicProvider* provider = flynes_quic_provider_create(&callbacks);
    check(provider != nullptr, "provider-create", "");

    /* A null callback table must be rejected by the real implementation. */
    check(flynes_quic_provider_create(nullptr) == nullptr,
          "provider-rejects-null-callbacks", "");
    if (provider == nullptr)
    {
        std::printf("PROBE RESULT: FAIL\n");
        return 1;
    }

    LoopbackSignerAccountingV1 accounting;
    FlynesTlsSignerCallbacks signer{};
    signer.context = &accounting;
    signer.retain = loopback_tls_sign_retain_v1;
    signer.release = loopback_tls_sign_release_v1;
    signer.sign = loopback_tls_sign_callback_v1;

    const auto registered = flynes_quic_provider_register_tls_material(
        provider, 1, identity.certificate_der.data(),
        identity.certificate_der.size(), identity.spki_der.data(),
        identity.spki_der.size(), &signer);
    check(registered == FLYNES_QUIC_ACCEPTED, "register-tls-material-accepted",
          std::to_string(registered).c_str());
    const Event material = await("register-tls-material-result", 1);
    check(material.result == FLYNES_QUIC_OK, "register-tls-material-ok",
          std::to_string(material.result).c_str());

    const char* bind = "127.0.0.1:0";
    const auto listen_called = flynes_quic_provider_listen(
        provider, 2, reinterpret_cast<const std::uint8_t*>(bind),
        std::strlen(bind), material.resource, 2000);
    check(listen_called == FLYNES_QUIC_ACCEPTED, "listen-accepted",
          std::to_string(listen_called).c_str());
    const Event listener = await("listen-result", 2);
    check(listener.result == FLYNES_QUIC_OK, "listen-ok",
          std::to_string(listener.result).c_str());

    const std::string address = as_string(listener.bytes);
    check(!address.empty() && address.find(':') != std::string::npos,
          "listener-address-reported", address.c_str());
    std::printf("PROBE listener=%s\n", address.c_str());

    check(flynes_quic_provider_accept(provider, 3, listener.resource) ==
              FLYNES_QUIC_ACCEPTED,
          "accept-accepted", "");

    const auto pin = loopback_spki_pin_v1();
    check(pin.size() == 32, "pin-is-one-sha256", "");
    const auto connect_called = flynes_quic_provider_connect(
        provider, 4, reinterpret_cast<const std::uint8_t*>(bind),
        std::strlen(bind),
        reinterpret_cast<const std::uint8_t*>(address.data()), address.size(),
        pin.data(), pin.size(), 2000);
    check(connect_called == FLYNES_QUIC_ACCEPTED, "connect-accepted",
          std::to_string(connect_called).c_str());

    const Event client = await("connect-result", 4);
    const Event server = await("accept-result", 3);
    check(client.result == FLYNES_QUIC_OK && server.result == FLYNES_QUIC_OK,
          "loopback-handshake-completed",
          (std::to_string(client.result) + "/" +
           std::to_string(server.result))
              .c_str());

    /* Handshake facts: TLS 1.3 only, exact ALPN, pin verified on the connector. */
    check(flynes_quic_provider_inspect_handshake(provider, 5, client.resource) ==
              FLYNES_QUIC_ACCEPTED,
          "inspect-client-accepted", "");
    const Event client_facts = await("inspect-client-result", 5);
    bool client_ok = client_facts.result == FLYNES_QUIC_OK &&
                     client_facts.bytes.size() ==
                         sizeof(FlynesQuicHandshakeFactsV1);
    if (client_ok)
    {
        FlynesQuicHandshakeFactsV1 facts{};
        std::memcpy(&facts, client_facts.bytes.data(), sizeof(facts));
        const std::string alpn(reinterpret_cast<const char*>(facts.alpn),
                               facts.alpn_size);
        bool pin_match = true;
        for (std::size_t index = 0; index < pin.size(); ++index)
            if (facts.peer_der_spki_hash[index] != pin[index])
                pin_match = false;
        client_ok = facts.tls_major == 1 && facts.tls_minor == 3 &&
                    facts.full_handshake == 1 &&
                    facts.pin_verifier_invoked == 1 &&
                    facts.peer_certificate_verified == 1 &&
                    facts.resumed == 0 && facts.zero_rtt == 0 &&
                    alpn == "flynes-nearby/2" && pin_match;
    }
    check(client_ok, "connector-handshake-facts", "");

    check(flynes_quic_provider_inspect_handshake(provider, 6, server.resource) ==
              FLYNES_QUIC_ACCEPTED,
          "inspect-listener-accepted", "");
    const Event server_facts = await("inspect-listener-result", 6);
    check(server_facts.result == FLYNES_QUIC_OK &&
              server_facts.bytes.size() ==
                  sizeof(FlynesQuicHandshakeFactsV1),
          "listener-handshake-facts", "");

    /* Keying material must agree on both ends. */
    const char* label = "w3-loopback-label";
    check(flynes_quic_provider_exporter(
              provider, 7, client.resource,
              reinterpret_cast<const std::uint8_t*>(label),
              std::strlen(label)) == FLYNES_QUIC_ACCEPTED,
          "exporter-client-accepted", "");
    check(flynes_quic_provider_exporter(
              provider, 8, server.resource,
              reinterpret_cast<const std::uint8_t*>(label),
              std::strlen(label)) == FLYNES_QUIC_ACCEPTED,
          "exporter-listener-accepted", "");
    const Event client_exporter = await("exporter-client-result", 7);
    const Event server_exporter = await("exporter-listener-result", 8);
    check(client_exporter.result == FLYNES_QUIC_OK &&
              server_exporter.result == FLYNES_QUIC_OK &&
              client_exporter.bytes.size() == 32 &&
              client_exporter.bytes == server_exporter.bytes,
          "exporters-agree", "");

    /* A real bidirectional stream round-trip. */
    check(flynes_quic_provider_accept_bidi(provider, 9, server.resource) ==
              FLYNES_QUIC_ACCEPTED,
          "accept-bidi-accepted", "");
    check(flynes_quic_provider_open_bidi(provider, 10, client.resource) ==
              FLYNES_QUIC_ACCEPTED,
          "open-bidi-accepted", "");
    const Event opened = await("open-bidi-result", 10);
    check(opened.result == FLYNES_QUIC_OK, "open-bidi-ok",
          std::to_string(opened.result).c_str());
    const std::uint64_t client_stream = opened.resource;

    const char* payload = "w3-two-engine-loopback";
    const std::size_t payload_size = std::strlen(payload);
    check(flynes_quic_provider_write(
              provider, 11, client_stream,
              reinterpret_cast<const std::uint8_t*>(payload), payload_size,
              0) == FLYNES_QUIC_ACCEPTED,
          "write-accepted", "");
    check(await("write-result", 11).result == FLYNES_QUIC_OK, "write-ok", "");

    check(flynes_quic_provider_finish(provider, 12, client_stream) ==
              FLYNES_QUIC_ACCEPTED,
          "finish-accepted", "");
    check(await("finish-result", 12).result == FLYNES_QUIC_OK, "finish-ok", "");

    const Event accepted = await("accept-bidi-result", 9);
    check(accepted.result == FLYNES_QUIC_OK && accepted.bytes.size() >= 8,
          "accept-bidi-returns-stream", "");
    if (accepted.result == FLYNES_QUIC_OK && accepted.bytes.size() >= 8)
    {
        const std::uint64_t server_stream = be_u64(accepted.bytes.data());
        check(flynes_quic_provider_grant_read_credit(provider, 13,
                                                     server_stream, 64) ==
                  FLYNES_QUIC_ACCEPTED,
              "grant-read-credit-accepted", "");
        const Event read = await("grant-read-credit-result", 13);
        const std::string received = as_string(read.bytes);
        check(read.result == FLYNES_QUIC_OK && received == payload,
              "stream-payload-round-trip", received.c_str());
    }
    else
    {
        check(false, "stream-payload-round-trip", "no stream handle");
    }

    /* A datagram round-trip on the same connection. */
    check(flynes_quic_provider_read_datagram(provider, 14, server.resource) ==
              FLYNES_QUIC_ACCEPTED,
          "read-datagram-accepted", "");
    const char* ping = "w3-ping";
    check(flynes_quic_provider_send_datagram(
              provider, 15, client.resource,
              reinterpret_cast<const std::uint8_t*>(ping), std::strlen(ping)) ==
              FLYNES_QUIC_ACCEPTED,
          "send-datagram-accepted", "");
    const Event sent = await("send-datagram-result", 15);
    const Event received_datagram = await("read-datagram-result", 14);
    check(sent.result == FLYNES_QUIC_OK &&
              as_string(received_datagram.bytes) == ping,
          "datagram-round-trip", "");

    /* Clean shutdown of every provider resource. */
    bool closed = true;
    std::uint64_t close_operation = 16;
    const std::uint64_t resources[4] = {client.resource, server.resource,
                                        listener.resource, material.resource};
    for (const std::uint64_t resource : resources)
    {
        if (flynes_quic_provider_close(provider, close_operation, resource,
                                       0) != FLYNES_QUIC_ACCEPTED)
            closed = false;
        else if (await("close-result", close_operation).result !=
                 FLYNES_QUIC_OK)
            closed = false;
        ++close_operation;
    }
    check(closed, "all-resources-closed", "");

    flynes_quic_provider_release(provider);

    /*
     * The signer context is released from the provider's own worker thread as
     * the runtime shuts down, so this is a bounded wait rather than an
     * immediate read (the provider crate's own test waits the same way). The
     * assertion is unchanged: exactly one retain and one release.
     */
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (accounting.releases == 0 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(accounting.retains == 1 && accounting.releases == 1,
          "signer-context-balanced",
          (std::to_string(accounting.retains) + "/" +
           std::to_string(accounting.releases))
              .c_str());

    if (failures != 0)
    {
        std::printf("PROBE RESULT: FAIL (%d)\n", failures);
        return 1;
    }
    std::printf("PROBE RESULT: PASS\n");
    return 0;
}
