#include "wire/prebind_record_queue.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using flynes::session::wire::OwnedAppFrame;
using flynes::session::wire::PreBindQueueResult;
using flynes::session::wire::PreBindRecordQueue;
using flynes::session::wire::QueuedAppRecord;

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

OwnedAppFrame frame(std::uint8_t value, std::size_t body_size)
{
    OwnedAppFrame result;
    result.frame_type_tag = 0x0210;
    result.type_name = "suspend_intent_v1";
    result.object_bytes.assign(body_size, value);
    result.object_hash[0] = value;
    result.app_frame_hash[0] = static_cast<std::uint8_t>(value + 1u);
    return result;
}

void test_records_release_only_after_every_gate()
{
    PreBindRecordQueue queue(1024, 4);
    check(queue.enqueue(7, 1, frame(1, 40)) == PreBindQueueResult::Queued,
          "first legal record is held");
    check(queue.enqueue(9, 1, frame(2, 50)) == PreBindQueueResult::Queued,
          "interleaved stream has an independent identity");

    std::vector<QueuedAppRecord> released;
    check(queue.release(&released) == PreBindQueueResult::Blocked && released.empty(),
          "no reducer input before transport authentication");
    check(queue.mark_transport_authenticated(true, true, true) ==
              PreBindQueueResult::Blocked,
          "TLS, pin, and exporter alone are insufficient");
    check(queue.mark_channel_bound() == PreBindQueueResult::Blocked,
          "ChannelBind alone is insufficient");
    check(queue.mark_link_ready(true, false) == PreBindQueueResult::Blocked,
          "one-sided LINK_READY is insufficient");
    check(queue.release(&released) == PreBindQueueResult::Blocked && released.empty(),
          "blocked release has zero side effects");
    check(queue.mark_link_ready(true, true) == PreBindQueueResult::Released,
          "the final LINK gate opens the queue");
    check(queue.release(&released) == PreBindQueueResult::Released &&
              released.size() == 2 && released[0].stream_id == 7 &&
              released[1].stream_id == 9 && released[0].frame.object_bytes[0] == 1 &&
              released[1].frame.object_bytes[0] == 2,
          "records release once in arrival order after every gate");
    check(queue.buffered_bytes() == 0 && queue.buffered_records() == 0,
          "released records no longer count against the pre-bind budget");
    check(queue.release(&released) == PreBindQueueResult::Released &&
              released.size() == 2,
          "repeated release cannot duplicate reducer input");
}

void test_failure_and_budget_discard_without_effects()
{
    PreBindRecordQueue bad_transport(1024, 4);
    check(bad_transport.enqueue(1, 1, frame(3, 20)) == PreBindQueueResult::Queued,
          "fixture queued before failed transport gate");
    check(bad_transport.mark_transport_authenticated(true, false, true) ==
              PreBindQueueResult::ProtocolViolation,
          "missing pin verification fails closed");
    std::vector<QueuedAppRecord> released;
    check(bad_transport.release(&released) == PreBindQueueResult::Closed &&
              released.empty() && bad_transport.buffered_bytes() == 0,
          "failed authentication discards bytes with no reducer effects");

    PreBindRecordQueue over_budget(64, 4);
    check(over_budget.enqueue(2, 1, frame(4, 40)) == PreBindQueueResult::Queued,
          "record below byte budget is held");
    check(over_budget.enqueue(2, 2, frame(5, 40)) ==
              PreBindQueueResult::Backpressure,
          "aggregate byte budget is enforced before retaining excess bytes");
    check(over_budget.failed() && over_budget.buffered_bytes() == 0,
          "budget failure drops the complete pre-bind queue");

    PreBindRecordQueue over_count(1024, 1);
    check(over_count.enqueue(3, 1, frame(6, 1)) == PreBindQueueResult::Queued &&
              over_count.enqueue(3, 2, frame(7, 1)) ==
                  PreBindQueueResult::Backpressure,
          "record-count budget is independently enforced");
}

void test_duplicate_and_invalid_records_fail_closed()
{
    PreBindRecordQueue duplicate(1024, 4);
    check(duplicate.enqueue(4, 8, frame(8, 10)) == PreBindQueueResult::Queued,
          "first stream sequence accepted");
    check(duplicate.enqueue(4, 8, frame(8, 10)) ==
              PreBindQueueResult::ProtocolViolation,
          "duplicate stream sequence is not replayed");
    check(duplicate.failed() && duplicate.buffered_records() == 0,
          "duplicate closes and clears the queue");

    PreBindRecordQueue invalid_budget(0, 1);
    check(invalid_budget.enqueue(1, 1, frame(1, 1)) ==
              PreBindQueueResult::InvalidArgument,
          "zero byte budget cannot create an unbounded special case");

    PreBindRecordQueue invalid_record(1024, 4);
    check(invalid_record.enqueue(0, 1, frame(1, 1)) ==
              PreBindQueueResult::InvalidArgument,
          "zero stream id is rejected");
    check(invalid_record.enqueue(1, 0, frame(1, 1)) ==
              PreBindQueueResult::InvalidArgument,
          "zero stream sequence is rejected");

    PreBindRecordQueue wrong_order(1024, 4);
    check(wrong_order.mark_channel_bound() ==
              PreBindQueueResult::ProtocolViolation,
          "ChannelBind cannot precede authenticated TLS/exporter facts");
    PreBindRecordQueue early_link(1024, 4);
    check(early_link.mark_transport_authenticated(true, true, true) ==
              PreBindQueueResult::Blocked &&
              early_link.mark_link_ready(true, true) ==
                  PreBindQueueResult::ProtocolViolation,
          "LINK_READY cannot precede ChannelBind");
}

} // namespace

int main()
{
    test_records_release_only_after_every_gate();
    test_failure_and_budget_discard_without_effects();
    test_duplicate_and_invalid_records_fail_closed();
    if (failures != 0)
        return 1;
    std::cout << "bounded pre-bind record queue passed\n";
    return 0;
}
