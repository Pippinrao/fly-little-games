#include "gatt_fragment.hpp"
#include "gatt_lookup_v2.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using flynes::session::wire::GattFragmentResult;
using flynes::session::wire::GattPhysicalDirection;
using flynes::session::wire::GattReassembler;
using flynes::session::wire::GattLogicalMessageView;
using flynes::session::wire::Status;

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

std::vector<std::uint8_t> request(std::uint8_t nonce_seed = 0xa0)
{
    const std::array<std::uint8_t, 6> code{{'0', '1', '2', '3', '4', '5'}};
    std::array<std::uint8_t, 16> nonce{};
    for (std::size_t i = 0; i < nonce.size(); ++i)
        nonce[i] = static_cast<std::uint8_t>(nonce_seed + i);
    std::vector<std::uint8_t> out(72);
    std::size_t written = 0;
    check(flynes::session::wire::encode_gatt_lookup_request_v2(
              code, nonce, out.data(), out.size(), &written) == Status::Ok &&
              written == out.size(), "request fixture");
    return out;
}

void test_fragment_sizes_and_round_trip()
{
    const auto logical = request();
    for (const std::size_t cap : {std::size_t{20}, std::size_t{182}, std::size_t{244}})
    {
        std::vector<std::vector<std::uint8_t>> fragments;
        check(flynes::session::wire::fragment_gatt_logical(
                  logical.data(), logical.size(), 27, 7, cap, &fragments) ==
                  GattFragmentResult::Accepted,
              "fragment at supported ATT value cap");
        const std::size_t expected = (logical.size() + (cap - 14) - 1) / (cap - 14);
        check(fragments.size() == expected, "fragment count uses cap minus 14-byte header");
        for (std::size_t i = 0; i < fragments.size(); ++i)
        {
            check(fragments[i].size() >= 15 && fragments[i].size() <= cap,
                  "each physical value is nonempty and bounded");
            check(fragments[i][0] == 1 && fragments[i][1] == 27 &&
                      fragments[i][3] == 0,
                  "physical version/type/reserved");
            const std::uint8_t flags = fragments[i][2];
            check(((flags & 1u) != 0u) == (i == 0), "FIRST matches index");
            check(((flags & 2u) != 0u) == (i + 1 == fragments.size()),
                  "LAST matches index");
        }

        GattReassembler receiver(91, GattPhysicalDirection::CentralToPeripheral);
        std::vector<std::uint8_t> completed;
        std::reverse(fragments.begin(), fragments.end());
        for (std::size_t i = 0; i < fragments.size(); ++i)
        {
            const auto result = receiver.accept(91, 100 + i, fragments[i].data(),
                                                fragments[i].size(), &completed);
            check(result == (i + 1 == fragments.size() ? GattFragmentResult::Complete
                                                        : GattFragmentResult::Accepted),
                  "out-of-order fragments complete once");
        }
        check(completed == logical, "reassembled exact logical bytes");
        check(receiver.buffered_bytes() == 0 && receiver.incomplete_count() == 0,
              "completion releases incomplete budget");
    }
}

void test_duplicate_conflict_and_generation()
{
    const auto logical = request();
    std::vector<std::vector<std::uint8_t>> fragments;
    check(flynes::session::wire::fragment_gatt_logical(
              logical.data(), logical.size(), 27, 8, 20, &fragments) ==
              GattFragmentResult::Accepted, "duplicate fixture fragments");
    GattReassembler receiver(100, GattPhysicalDirection::PeripheralToCentral);
    std::vector<std::uint8_t> completed;
    check(receiver.accept(99, 10, fragments[0].data(), fragments[0].size(), &completed) ==
              GattFragmentResult::Stale,
          "old connection generation rejected");
    check(receiver.accept(100, 10, fragments[0].data(), fragments[0].size(), &completed) ==
              GattFragmentResult::Accepted,
          "first fragment accepted");
    check(receiver.accept(100, 11, fragments[0].data(), fragments[0].size(), &completed) ==
              GattFragmentResult::Duplicate,
          "byte-identical duplicate is idempotent");
    auto conflict = fragments[0];
    ++conflict.back();
    check(receiver.accept(100, 12, conflict.data(), conflict.size(), &completed) ==
              GattFragmentResult::ProtocolViolation,
          "same index with different bytes is terminal conflict");
    check(receiver.incomplete_count() == 0, "conflict drops the affected assembly");
}

void test_completed_message_id_never_reused()
{
    const auto logical = request();
    std::vector<std::vector<std::uint8_t>> fragments;
    check(flynes::session::wire::fragment_gatt_logical(
              logical.data(), logical.size(), 27, 12, 244, &fragments) ==
              GattFragmentResult::Accepted,
          "completed-id fixture");
    GattReassembler receiver(101, GattPhysicalDirection::CentralToPeripheral);
    std::vector<std::uint8_t> completed;
    check(receiver.accept(101, 1, fragments[0].data(), fragments[0].size(), &completed) ==
              GattFragmentResult::Complete,
          "message completes once");
    check(receiver.accept(101, 2, fragments[0].data(), fragments[0].size(), &completed) ==
              GattFragmentResult::Duplicate,
          "completed exact fragment remains idempotent");

    const auto different = request(0xb0);
    std::vector<std::vector<std::uint8_t>> reused;
    check(flynes::session::wire::fragment_gatt_logical(
              different.data(), different.size(), 27, 12, 244, &reused) ==
              GattFragmentResult::Accepted,
          "message-id reuse fixture");
    check(receiver.accept(101, 3, reused[0].data(), reused[0].size(), &completed) ==
              GattFragmentResult::ProtocolViolation,
          "same message id cannot name different logical bytes");
}

void test_bounds_and_timeout()
{
    check(flynes::session::wire::fragment_gatt_logical(
              nullptr, 0, 27, 1, 20, nullptr) == GattFragmentResult::InvalidArgument,
          "invalid sender arguments");
    auto logical = request();
    std::vector<std::vector<std::uint8_t>> fragments;
    check(flynes::session::wire::fragment_gatt_logical(
              logical.data(), logical.size(), 27, 1, 19, &fragments) ==
              GattFragmentResult::NotSupported,
          "ATT value cap below 20 rejected");
    check(flynes::session::wire::fragment_gatt_logical(
              logical.data(), logical.size(), 27, 0, 20, &fragments) ==
              GattFragmentResult::ProtocolViolation,
          "zero message id rejected");
    auto corrupt_logical = logical;
    ++corrupt_logical.back();
    check(flynes::session::wire::fragment_gatt_logical(
              corrupt_logical.data(), corrupt_logical.size(), 27, 1, 20, &fragments) ==
              GattFragmentResult::ProtocolViolation,
          "sender rejects a logical message with a bad exact hash");
    std::vector<std::uint8_t> too_large(4097, 0);
    check(flynes::session::wire::fragment_gatt_logical(
              too_large.data(), too_large.size(), 1, 1, 20, &fragments) ==
              GattFragmentResult::Backpressure,
          "logical message above 4096 rejected before slicing");

    std::vector<std::vector<std::uint8_t>> first;
    std::vector<std::vector<std::uint8_t>> second;
    std::vector<std::vector<std::uint8_t>> third;
    const auto logical2 = request(0xb0);
    const auto logical3 = request(0xc0);
    check(flynes::session::wire::fragment_gatt_logical(
              logical.data(), logical.size(), 27, 1, 20, &first) == GattFragmentResult::Accepted,
          "first bounded fixture");
    check(flynes::session::wire::fragment_gatt_logical(
              logical2.data(), logical2.size(), 27, 2, 20, &second) == GattFragmentResult::Accepted,
          "second bounded fixture");
    check(flynes::session::wire::fragment_gatt_logical(
              logical3.data(), logical3.size(), 27, 3, 20, &third) == GattFragmentResult::Accepted,
          "third bounded fixture");
    GattReassembler receiver(7, GattPhysicalDirection::CentralToPeripheral);
    std::vector<std::uint8_t> completed;
    check(receiver.accept(7, 100, first[0].data(), first[0].size(), &completed) ==
              GattFragmentResult::Accepted,
          "first incomplete accepted");
    check(receiver.accept(7, 100, second[0].data(), second[0].size(), &completed) ==
              GattFragmentResult::Accepted,
          "second incomplete accepted");
    check(receiver.accept(7, 100, third[0].data(), third[0].size(), &completed) ==
              GattFragmentResult::Backpressure,
          "third concurrent incomplete rejected");
    check(receiver.expire(5'000'000'099ULL) == 0, "not expired before five seconds");
    check(receiver.expire(5'000'000'100ULL) == 2, "fixed five-second timeout expires both");
    check(receiver.incomplete_count() == 0 && receiver.buffered_bytes() == 0,
          "expiry returns all budget");
}

void test_header_and_logical_validation()
{
    const auto logical = request();
    std::vector<std::vector<std::uint8_t>> fragments;
    check(flynes::session::wire::fragment_gatt_logical(
              logical.data(), logical.size(), 27, 11, 244, &fragments) ==
              GattFragmentResult::Accepted && fragments.size() == 1,
          "single fragment fixture");
    GattReassembler receiver(3, GattPhysicalDirection::CentralToPeripheral);
    std::vector<std::uint8_t> completed;
    const std::array<std::pair<std::size_t, std::uint8_t>, 8> mutations{{
        {{0}, {2}}, {{1}, {28}}, {{2}, {0}}, {{3}, {1}},
        {{6}, {1}}, {{8}, {1}}, {{10}, {1}}, {{12}, {1}}}};
    for (const auto& mutation : mutations)
    {
        auto bad = fragments[0];
        bad[mutation.first] = mutation.second;
        GattReassembler isolated(3, GattPhysicalDirection::CentralToPeripheral);
        check(isolated.accept(3, 1, bad.data(), bad.size(), &completed) ==
                  GattFragmentResult::ProtocolViolation,
              "physical header mutation rejected");
    }
    auto bad_logical = fragments[0];
    ++bad_logical.back();
    check(receiver.accept(3, 1, bad_logical.data(), bad_logical.size(), &completed) ==
              GattFragmentResult::ProtocolViolation,
          "completed logical hash mismatch rejected");
    check(!flynes::session::wire::gatt_ack_produces_authentication_evidence(),
          "type 16 physical ACK never authenticates");
}

void test_public_logical_decoder_routes_one_exact_type()
{
    const auto logical = request();
    GattLogicalMessageView view{};
    check(flynes::session::wire::decode_gatt_logical_message(
              logical.data(), logical.size(), 27, &view) ==
              GattFragmentResult::Complete,
          "strict logical decoder accepts the expected registered type");
    check(view.version == 2 && view.logical_type == 27 &&
              view.body_size == 32 && view.body == logical.data() + 8 &&
              view.logical_hash == logical.data() + logical.size() - 32,
          "logical decoder exposes borrowed exact body and hash windows");
    check(flynes::session::wire::decode_gatt_logical_message(
              logical.data(), logical.size(), 28, &view) ==
              GattFragmentResult::ProtocolViolation,
          "outer and inner logical types cannot be confused");
    auto trailing = logical;
    trailing.push_back(0);
    check(flynes::session::wire::decode_gatt_logical_message(
              trailing.data(), trailing.size(), 27, &view) ==
              GattFragmentResult::ProtocolViolation,
          "logical decoder rejects trailing bytes");
    check(flynes::session::wire::decode_gatt_logical_message(
              nullptr, 0, 27, &view) == GattFragmentResult::InvalidArgument,
          "logical decoder rejects invalid pointer arguments");
}

void test_public_logical_encoder_is_canonical()
{
    const std::array<std::uint8_t, 3> body{{0x11, 0x22, 0x33}};
    std::vector<std::uint8_t> logical;
    check(flynes::session::wire::encode_gatt_logical_message(
              static_cast<std::uint8_t>(
                  flynes::session::wire::GattLogicalType::PairCommit),
              body.data(), body.size(), &logical) ==
              GattFragmentResult::Accepted,
          "generic logical encoder accepts one registered message type");
    GattLogicalMessageView view{};
    check(logical.size() == 43 && logical[0] == 1 && logical[1] == 4 &&
              logical[4] == 0 && logical[5] == 0 && logical[6] == 0 &&
              logical[7] == 3 &&
              flynes::session::wire::decode_gatt_logical_message(
                  logical.data(), logical.size(), 4, &view) ==
                  GattFragmentResult::Complete &&
              std::equal(body.begin(), body.end(), view.body),
          "generic logical encoder emits the frozen header, body, and hash");
    check(flynes::session::wire::encode_gatt_logical_message(
              29, body.data(), body.size(), &logical) ==
              GattFragmentResult::NotSupported,
          "unregistered parallel-draft type 29 cannot be encoded");
}

void test_physical_ack_exact_body_and_completed_metadata()
{
    flynes::session::wire::GattLogicalAckV1 ack{};
    ack.original_sender_physical_role =
        GattPhysicalDirection::CentralToPeripheral;
    ack.acked_logical_type = 4;
    ack.message_id = 0x1234;
    for (std::size_t i = 0; i < ack.logical_hash.size(); ++i)
        ack.logical_hash[i] = static_cast<std::uint8_t>(0x80 + i);
    std::array<std::uint8_t, flynes::session::wire::kGattLogicalAckBodySize> body{};
    check(flynes::session::wire::encode_gatt_logical_ack_v1(ack, &body) ==
              GattFragmentResult::Accepted &&
              body[0] == 1 && body[1] == 4 && body[2] == 0x12 && body[3] == 0x34,
          "physical ACK has the frozen 36-byte layout");
    flynes::session::wire::GattLogicalAckV1 decoded{};
    check(flynes::session::wire::decode_gatt_logical_ack_v1(
              body.data(), body.size(), &decoded) == GattFragmentResult::Complete &&
              decoded.original_sender_physical_role == ack.original_sender_physical_role &&
              decoded.acked_logical_type == ack.acked_logical_type &&
              decoded.message_id == ack.message_id &&
              decoded.logical_hash == ack.logical_hash,
          "physical ACK round trips exactly");
    auto invalid = body;
    invalid[1] = 16;
    check(flynes::session::wire::decode_gatt_logical_ack_v1(
              invalid.data(), invalid.size(), &decoded) ==
              GattFragmentResult::ProtocolViolation,
          "an ACK cannot acknowledge another ACK");
    check(flynes::session::wire::decode_gatt_logical_ack_v1(
              body.data(), body.size() - 1, &decoded) ==
              GattFragmentResult::ProtocolViolation,
          "physical ACK rejects truncation");

    const auto logical = request();
    std::vector<std::vector<std::uint8_t>> fragments;
    check(flynes::session::wire::fragment_gatt_logical(
              logical.data(), logical.size(), 27, 0x4567, 244, &fragments) ==
              GattFragmentResult::Accepted,
          "completed metadata fixture");
    GattReassembler receiver(9, GattPhysicalDirection::CentralToPeripheral);
    std::vector<std::uint8_t> completed;
    flynes::session::wire::GattCompletedMetadata metadata{};
    check(receiver.accept(9, 1, fragments[0].data(), fragments[0].size(),
                          &completed, &metadata) == GattFragmentResult::Complete &&
              metadata.logical_type == 27 && metadata.message_id == 0x4567 &&
              std::equal(metadata.logical_hash.begin(), metadata.logical_hash.end(),
                         logical.end() - 32),
          "reassembler preserves exact ACK identity after completion");
    metadata = {};
    check(receiver.accept(9, 2, fragments[0].data(), fragments[0].size(),
                          &completed, &metadata) == GattFragmentResult::Duplicate &&
              metadata.message_id == 0x4567,
          "completed duplicate exposes the same ACK identity");
}

} // namespace

int main()
{
    test_fragment_sizes_and_round_trip();
    test_duplicate_conflict_and_generation();
    test_completed_message_id_never_reused();
    test_bounds_and_timeout();
    test_header_and_logical_validation();
    test_public_logical_decoder_routes_one_exact_type();
    test_public_logical_encoder_is_canonical();
    test_physical_ack_exact_body_and_completed_metadata();
    if (failures != 0)
        return 1;
    std::cout << "GATT physical fragmentation contract passed\n";
    return 0;
}
