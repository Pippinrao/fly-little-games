#include "quic_stream_gate.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using flynes::session::wire::BindingKindV1;
using flynes::session::wire::QuicGateResult;
using flynes::session::wire::QuicStreamGate;
using flynes::session::wire::TlsSideV1;

namespace {

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<std::uint8_t> preamble(const char magic[4], std::uint16_t kind)
{
    return {static_cast<std::uint8_t>(magic[0]), static_cast<std::uint8_t>(magic[1]),
            static_cast<std::uint8_t>(magic[2]), static_cast<std::uint8_t>(magic[3]),
            0, 1, static_cast<std::uint8_t>(kind >> 8u), static_cast<std::uint8_t>(kind)};
}

std::vector<std::uint8_t> record(std::size_t size, std::uint8_t type = 0)
{
    std::vector<std::uint8_t> out(size + 4, 0);
    out[0] = static_cast<std::uint8_t>(size >> 24u);
    out[1] = static_cast<std::uint8_t>(size >> 16u);
    out[2] = static_cast<std::uint8_t>(size >> 8u);
    out[3] = static_cast<std::uint8_t>(size);
    if (size != 0)
        out[4] = type;
    return out;
}

void send(QuicStreamGate& gate, std::uint64_t stream, TlsSideV1 sender,
          const std::vector<std::uint8_t>& bytes, const char* message)
{
    for (const auto byte : bytes)
    {
        const auto result = gate.data(stream, sender, &byte, 1);
        check(result == QuicGateResult::NeedMore || result == QuicGateResult::Accepted,
              message);
    }
}

void test_initial_bind_and_early_app_gate()
{
    QuicStreamGate gate(BindingKindV1::Initial, true);
    check(gate.open_bidi(1, TlsSideV1::Listener) == QuicGateResult::ProtocolViolation,
          "listener cannot open the bind stream");

    QuicStreamGate valid(BindingKindV1::Initial, true);
    check(valid.open_bidi(7, TlsSideV1::Connector) == QuicGateResult::Accepted,
          "connector opens exactly one bind stream");
    check(valid.open_bidi(8, TlsSideV1::Connector) == QuicGateResult::ProtocolViolation,
          "duplicate bind stream is terminal");

    QuicStreamGate flow(BindingKindV1::Initial, true);
    check(flow.open_bidi(9, TlsSideV1::Connector) == QuicGateResult::Accepted,
          "initial bind opens");
    check(flow.normal_app_stream_allowed() == false,
          "normal app streams blocked before FNB1");
    send(flow, 9, TlsSideV1::Connector, preamble("FNB1", 1), "split FNB1 accepted");
    send(flow, 9, TlsSideV1::Connector, record(248), "connector proof framed");
    send(flow, 9, TlsSideV1::Listener, record(248), "listener proof framed");
    send(flow, 9, TlsSideV1::Connector, record(128), "connector ACK framed");
    check(flow.finish(9, TlsSideV1::Listener) == QuicGateResult::ProtocolViolation,
          "listener cannot FIN before observing connector FIN");

    QuicStreamGate done(BindingKindV1::Initial, true);
    done.open_bidi(10, TlsSideV1::Connector);
    send(done, 10, TlsSideV1::Connector, preamble("FNB1", 1), "FNB1");
    send(done, 10, TlsSideV1::Connector, record(248), "proof");
    send(done, 10, TlsSideV1::Listener, record(248), "proof");
    send(done, 10, TlsSideV1::Connector, record(128), "ack");
    check(done.finish(10, TlsSideV1::Connector) == QuicGateResult::Accepted,
          "connector ACK plus FIN accepted");
    check(done.finish(10, TlsSideV1::Listener) == QuicGateResult::ChannelBound &&
              done.normal_app_stream_allowed(),
          "listener FIN is the only ChannelBound release point");
}

void test_same_path_requires_fnr1_then_fnb1()
{
    QuicStreamGate gate(BindingKindV1::SamePathReconnect, true);
    gate.open_bidi(20, TlsSideV1::Connector);
    send(gate, 20, TlsSideV1::Connector, preamble("FNR1", 2), "FNR1");
    for (const auto& item : std::array<std::pair<std::size_t, std::uint8_t>, 5>{{
             {356u, std::uint8_t{1}}, {356u, std::uint8_t{3}},
             {289u, std::uint8_t{13}}, {181u, std::uint8_t{20}},
             {173u, std::uint8_t{14}}}})
        send(gate, 20, TlsSideV1::Connector, record(item.first, item.second), "initiator record");
    for (const auto& item : std::array<std::pair<std::size_t, std::uint8_t>, 5>{{
             {356u, std::uint8_t{2}}, {356u, std::uint8_t{4}},
             {181u, std::uint8_t{20}}, {289u, std::uint8_t{13}},
             {173u, std::uint8_t{14}}}})
        send(gate, 20, TlsSideV1::Listener, record(item.first, item.second), "responder record");
    check(gate.finish(20, TlsSideV1::Connector) == QuicGateResult::Accepted &&
              gate.finish(20, TlsSideV1::Listener) == QuicGateResult::PrebindComplete,
          "both FNR1 directions must finish");
    check(!gate.normal_app_stream_allowed(), "FNR1 alone never releases app data");
    check(gate.open_bidi(21, TlsSideV1::Connector) == QuicGateResult::Accepted,
          "second connector bidi reserved for FNB1");
    send(gate, 21, TlsSideV1::Connector, preamble("FNB1", 1), "FNB1 after FNR1");
    send(gate, 21, TlsSideV1::Connector, record(248), "connector proof");
    send(gate, 21, TlsSideV1::Listener, record(248), "listener proof");
    send(gate, 21, TlsSideV1::Connector, record(128), "connector ACK");
    check(gate.finish(21, TlsSideV1::Connector) == QuicGateResult::Accepted &&
              gate.finish(21, TlsSideV1::Listener) == QuicGateResult::ChannelBound,
          "same-path reaches ChannelBound only after the second stream");

    QuicStreamGate wrong(BindingKindV1::SamePathReconnect, true);
    wrong.open_bidi(30, TlsSideV1::Connector);
    const auto fnb = preamble("FNB1", 1);
    check(wrong.data(30, TlsSideV1::Connector, fnb.data(), fnb.size()) ==
              QuicGateResult::ProtocolViolation,
          "same-path cannot skip FNR1");
}

} // namespace

int main()
{
    test_initial_bind_and_early_app_gate();
    test_same_path_requires_fnr1_then_fnb1();
    if (failures != 0)
        return 1;
    std::cout << "QUIC pre-bind/bind stream gate passed\n";
    return 0;
}
