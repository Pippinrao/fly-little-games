/*
 * Task 11 / CONTENT-DUAL step 1: offer/check/permission state machine.
 *
 * CONTENT01: send-only does not open the ROM stream; send+receive does;
 *            hash success does not publish catalog; APPROVE_IMPORT does,
 *            exactly once.
 * CONTENT02: oversize, wrong hash, OOB offset, 10s stall, cancel, disk-full,
 *            multi-payload leave staging discarded and DUAL unavailable.
 * Saving a friend never grants send/receive/import.
 */

#include "content/content_offer_scheduler.hpp"

#include "wire/sha256.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

namespace content = flynes::session::content;
namespace wire = flynes::session::wire;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

content::ContentOfferDeclV1 make_decl(std::uint32_t length,
                                      const std::uint8_t* payload)
{
    content::ContentOfferDeclV1 decl{};
    decl.offer_id.fill(0x11);
    decl.content_id.fill(0x22);
    decl.declared_length = length;
    if (payload != nullptr && length > 0)
        decl.payload_sha256 = wire::sha256(payload, length);
    return decl;
}

void content01_permissions_and_import_are_independent()
{
    std::puts("content offer: CONTENT01 send/receive/import");
    const std::uint8_t payload[] = {0x4E, 0x45, 0x53, 0x1A, 0x01, 0x01, 0x00,
                                    0x00};
    auto decl = make_decl(static_cast<std::uint32_t>(sizeof(payload)), payload);

    content::ContentOfferSchedulerV1 offerer;
    offerer.reset(content::ContentOfferRoleV1::Offerer);
    check(offerer.offer(decl) == FLY_SESSION_V2_OK, "offerer accepts a legal offer");
    check(offerer.approve_send() == FLY_SESSION_V2_OK, "APPROVE_SEND is applied");
    check(offerer.send_consented() && !offerer.receive_consented(),
          "send-only does not invent receive consent");
    check(!offerer.may_open_rom_stream() && offerer.transferred_bytes() == 0,
          "send-only must not open the ROM stream or move bytes");

    content::ContentOfferSchedulerV1 receiver;
    receiver.reset(content::ContentOfferRoleV1::Receiver);
    check(receiver.accept_offer(decl) == FLY_SESSION_V2_OK,
          "receiver accepts the same declaration");
    check(receiver.approve_receive() == FLY_SESSION_V2_OK,
          "APPROVE_RECEIVE is applied");
    check(!receiver.may_open_rom_stream(),
          "receive-only must not open the ROM stream");

    check(offerer.approve_receive() == FLY_SESSION_V2_OK,
          "offerer records matching receive consent");
    check(receiver.approve_send() == FLY_SESSION_V2_OK,
          "receiver records matching send consent");
    check(offerer.may_open_rom_stream() && receiver.may_open_rom_stream(),
          "both consents matching opens the ROM stream on both sides");

    check(receiver.append_bytes(0, payload, sizeof(payload)) == FLY_SESSION_V2_OK,
          "legal bytes land in staging after both consents");
    check(receiver.transferred_bytes() == sizeof(payload),
          "declared length is fully received");
    check(receiver.staging_live(), "staging is live after a complete transfer");
    check(!receiver.catalog_published() && !receiver.dual_available(),
          "hash success must not publish catalog or claim DUAL");
    check(receiver.approve_import() == FLY_SESSION_V2_OK,
          "APPROVE_IMPORT after a verified payload is applied");
    check(receiver.catalog_published() && receiver.dual_available(),
          "import publishes catalog and makes DUAL available");
    check(receiver.approve_import() == FLY_SESSION_V2_OK,
          "a second APPROVE_IMPORT is idempotent");
    check(receiver.catalog_published(),
          "catalog stays published after the idempotent second import");
}

void content01_receiver_may_pre_consent_while_idle()
{
    std::puts("content offer: receiver Idle pre-consent opens Rom");
    content::ContentOfferSchedulerV1 receiver;
    receiver.reset(content::ContentOfferRoleV1::Receiver);
    check(receiver.approve_send() == FLY_SESSION_V2_OK &&
              receiver.approve_receive() == FLY_SESSION_V2_OK,
          "a receiver may pre-consent before the offer record arrives");
    check(receiver.may_open_rom_stream() &&
              receiver.state() == content::ContentOfferStateV1::Idle,
          "pre-consent opens Rom so the offer record can arrive");
    content::ContentOfferSchedulerV1 offerer;
    offerer.reset(content::ContentOfferRoleV1::Offerer);
    check(offerer.approve_send() == FLY_SESSION_V2_INVALID_STATE,
          "an offerer cannot pre-consent before OFFER_CONTENT");
}

void content01_friend_does_not_authorize()
{
    std::puts("content offer: saved friend does not authorize");
    const std::uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    auto decl = make_decl(static_cast<std::uint32_t>(sizeof(payload)), payload);
    content::ContentOfferSchedulerV1 offerer;
    offerer.reset(content::ContentOfferRoleV1::Offerer);
    check(offerer.offer(decl) == FLY_SESSION_V2_OK, "offer binds");
    check(offerer.note_friend_saved() == FLY_SESSION_V2_OK,
          "saving a friend is recorded");
    check(!offerer.send_consented() && !offerer.receive_consented() &&
              !offerer.import_consented() && !offerer.may_open_rom_stream(),
          "a saved friend grants no send/receive/import consent");
}

void content02_oversize_is_rejected()
{
    std::puts("content offer: CONTENT02 oversize");
    content::ContentOfferDeclV1 decl{};
    decl.offer_id.fill(0x11);
    decl.content_id.fill(0x22);
    decl.declared_length = content::kContentRomMaxBytesV1 + 1u;
    content::ContentOfferSchedulerV1 offerer;
    offerer.reset(content::ContentOfferRoleV1::Offerer);
    check(offerer.offer(decl) == FLY_SESSION_V2_INVALID_ARGUMENT,
          "declared length above 8 MiB is rejected");
    check(offerer.state() == content::ContentOfferStateV1::Failed &&
              offerer.fail_reason() == content::ContentOfferFailV1::Oversize,
          "oversize fails closed");
    check(!offerer.staging_live() && !offerer.dual_available(),
          "oversize must not stage bytes or claim DUAL");
}

void content02_wrong_hash_discards_staging()
{
    std::puts("content offer: CONTENT02 wrong hash");
    const std::uint8_t payload[] = {0xAA, 0xBB, 0xCC, 0xDD};
    auto decl = make_decl(static_cast<std::uint32_t>(sizeof(payload)), payload);
    decl.payload_sha256.fill(0xEE);
    content::ContentOfferSchedulerV1 receiver;
    receiver.reset(content::ContentOfferRoleV1::Receiver);
    check(receiver.accept_offer(decl) == FLY_SESSION_V2_OK, "offer binds");
    check(receiver.approve_receive() == FLY_SESSION_V2_OK &&
              receiver.approve_send() == FLY_SESSION_V2_OK,
          "both consents");
    check(receiver.append_bytes(0, payload, sizeof(payload)) ==
              FLY_SESSION_V2_PROTOCOL_VIOLATION,
          "payload SHA-256 mismatch is a protocol violation");
    check(receiver.fail_reason() == content::ContentOfferFailV1::HashMismatch,
          "hash mismatch is the fail reason");
    check(!receiver.staging_live() && !receiver.catalog_published() &&
              !receiver.dual_available(),
          "tampered bytes are discarded and never imported");
}

void content02_offset_oob_and_cancel_and_stall()
{
    std::puts("content offer: CONTENT02 offset/cancel/stall/disk/multi");
    const std::uint8_t payload[] = {0x10, 0x20, 0x30, 0x40};
    auto decl = make_decl(static_cast<std::uint32_t>(sizeof(payload)), payload);

    content::ContentOfferSchedulerV1 oob;
    oob.reset(content::ContentOfferRoleV1::Receiver);
    check(oob.accept_offer(decl) == FLY_SESSION_V2_OK &&
              oob.approve_receive() == FLY_SESSION_V2_OK &&
              oob.approve_send() == FLY_SESSION_V2_OK,
          "oob fixture is consented");
    check(oob.append_bytes(8, payload, sizeof(payload)) ==
              FLY_SESSION_V2_INVALID_ARGUMENT,
          "offset past declared length is rejected");
    check(oob.fail_reason() == content::ContentOfferFailV1::OffsetOutOfRange &&
              !oob.staging_live() && !oob.dual_available(),
          "OOB offset discards staging");

    content::ContentOfferSchedulerV1 cancelled;
    cancelled.reset(content::ContentOfferRoleV1::Receiver);
    check(cancelled.accept_offer(decl) == FLY_SESSION_V2_OK &&
              cancelled.approve_receive() == FLY_SESSION_V2_OK &&
              cancelled.approve_send() == FLY_SESSION_V2_OK,
          "cancel fixture is consented");
    check(cancelled.append_bytes(0, payload, 2) == FLY_SESSION_V2_OK,
          "partial bytes stage");
    check(cancelled.staging_live(), "partial transfer keeps staging live");
    check(cancelled.cancel() == FLY_SESSION_V2_OK, "cancel is applied");
    check(cancelled.state() == content::ContentOfferStateV1::Cancelled &&
              !cancelled.staging_live() && !cancelled.dual_available(),
          "cancel clears this offer's staging and does not claim DUAL");

    content::ContentOfferSchedulerV1 stall;
    stall.reset(content::ContentOfferRoleV1::Receiver);
    check(stall.accept_offer(decl) == FLY_SESSION_V2_OK &&
              stall.approve_receive() == FLY_SESSION_V2_OK &&
              stall.approve_send() == FLY_SESSION_V2_OK,
          "stall fixture is consented");
    check(stall.on_clock(1) == FLY_SESSION_V2_OK, "clock starts");
    check(stall.append_bytes(0, payload, 1) == FLY_SESSION_V2_OK, "one byte");
    check(stall.on_clock(1 + content::kContentStallTimeoutNsV1) ==
              FLY_SESSION_V2_TIMEOUT,
          "10s without progress times out");
    check(stall.fail_reason() == content::ContentOfferFailV1::StallTimeout &&
              !stall.staging_live() && !stall.dual_available(),
          "stall discards staging");

    content::ContentOfferSchedulerV1 disk;
    disk.reset(content::ContentOfferRoleV1::Receiver);
    check(disk.accept_offer(decl) == FLY_SESSION_V2_OK &&
              disk.approve_receive() == FLY_SESSION_V2_OK &&
              disk.approve_send() == FLY_SESSION_V2_OK,
          "disk fixture is consented");
    check(disk.note_disk_full() == FLY_SESSION_V2_UNAVAILABLE,
          "disk full fails closed");
    check(disk.fail_reason() == content::ContentOfferFailV1::DiskFull &&
              !disk.staging_live() && !disk.dual_available(),
          "disk full never publishes catalog");

    content::ContentOfferSchedulerV1 multi;
    multi.reset(content::ContentOfferRoleV1::Receiver);
    check(multi.accept_offer(decl) == FLY_SESSION_V2_OK &&
              multi.approve_receive() == FLY_SESSION_V2_OK &&
              multi.approve_send() == FLY_SESSION_V2_OK,
          "multi-payload fixture is consented");
    check(multi.note_multi_payload() == FLY_SESSION_V2_INVALID_ARGUMENT,
          "multi-payload archives are rejected");
    check(multi.fail_reason() == content::ContentOfferFailV1::MultiPayload &&
              !multi.catalog_published(),
          "multi-payload never enters the catalog");
}

void refused_transfer_leaves_dual_unavailable()
{
    std::puts("content offer: refuse stays DUAL-unavailable, no STREAM fallback");
    const std::uint8_t payload[] = {0x01};
    auto decl = make_decl(1, payload);
    content::ContentOfferSchedulerV1 receiver;
    receiver.reset(content::ContentOfferRoleV1::Receiver);
    check(receiver.accept_offer(decl) == FLY_SESSION_V2_OK, "offer binds");
    check(receiver.cancel() == FLY_SESSION_V2_OK, "receiver refuses");
    check(!receiver.dual_available() &&
              receiver.state() == content::ContentOfferStateV1::Cancelled,
          "a refused transfer leaves DUAL unavailable");
}

} // namespace

int main()
{
    content01_permissions_and_import_are_independent();
    content01_receiver_may_pre_consent_while_idle();
    content01_friend_does_not_authorize();
    content02_oversize_is_rejected();
    content02_wrong_hash_discards_staging();
    content02_offset_oob_and_cancel_and_stall();
    refused_transfer_leaves_dual_unavailable();
    if (failures != 0)
    {
        std::fprintf(stderr, "%d content-offer checks failed\n", failures);
        return 1;
    }
    std::puts("flynes_content_offer_scheduler passed");
    return 0;
}
