/*
 * Task 11 / CONTENT-DUAL step 2: ROM bytes travel only on the dedicated bulk
 * stream, and only after send+receive consents match.
 */

#include "content/content_transfer_controller.hpp"
#include "wire/sha256.hpp"

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

content::ContentOfferDeclV1 make_decl(const std::uint8_t* payload, std::size_t size)
{
    content::ContentOfferDeclV1 decl{};
    decl.offer_id.fill(0x11);
    decl.content_id.fill(0x22);
    decl.declared_length = static_cast<std::uint32_t>(size);
    decl.payload_sha256 = wire::sha256(payload, size);
    return decl;
}

void pump(content::ContentTransferControllerV1& offerer,
          content::ContentTransferControllerV1& receiver)
{
    for (int round = 0; round < 64; ++round)
    {
        bool progress = false;
        if (auto effect = offerer.poll_effect())
        {
            progress = true;
            if (effect->kind ==
                content::ContentTransferControllerV1::EffectKind::OpenStream)
            {
                check(effect->stream_kind == 5u,
                      "the bulk stream is QuicChannel::Rom, not Control/StateCommit");
                check(offerer.on_stream_open(11, 12) == FLY_SESSION_V2_OK,
                      "offerer opens the ROM stream");
                check(receiver.on_stream_open(21, 22) == FLY_SESSION_V2_OK,
                      "receiver accepts the ROM stream");
            }
            else if (effect->kind ==
                     content::ContentTransferControllerV1::EffectKind::Write)
            {
                check(receiver.ingest_remote_bytes(effect->bytes.data(),
                                                   effect->bytes.size()) ==
                          FLY_SESSION_V2_OK,
                      "receiver ingests a bulk record");
                check(offerer.on_write_complete() == FLY_SESSION_V2_OK,
                      "offerer write completes");
            }
        }
        if (auto effect = receiver.poll_effect())
        {
            progress = true;
            if (effect->kind ==
                content::ContentTransferControllerV1::EffectKind::OpenStream)
            {
                check(effect->stream_kind == 5u,
                      "receiver also opens Rom, never Control");
                check(receiver.on_stream_open(21, 22) == FLY_SESSION_V2_OK &&
                          offerer.on_stream_open(11, 12) == FLY_SESSION_V2_OK,
                      "both sides attach the ROM stream");
            }
            else if (effect->kind ==
                     content::ContentTransferControllerV1::EffectKind::GrantRead)
            {
                check(true, "receiver grants read on the ROM stream");
            }
        }
        if (!progress)
            break;
    }
}

void send_only_does_not_open_stream()
{
    std::puts("content transfer: send-only writes zero bulk bytes");
    const std::uint8_t payload[] = {0x4E, 0x45, 0x53, 0x1A};
    auto decl = make_decl(payload, sizeof(payload));
    content::ContentTransferControllerV1 offerer;
    offerer.reset(content::ContentOfferRoleV1::Offerer);
    offerer.attach_connection(7, false);
    check(offerer.offer(decl, payload, sizeof(payload)) == FLY_SESSION_V2_OK,
          "offer binds payload");
    check(offerer.approve_send() == FLY_SESSION_V2_OK, "send consented");
    check(!offerer.poll_effect().has_value() && offerer.bulk_writes() == 0 &&
              !offerer.stream_open(),
          "send-only must not open the ROM stream or write bytes");
}

void both_consents_move_bytes_on_rom_stream()
{
    std::puts("content transfer: both consents move bytes on Rom");
    const std::uint8_t payload[] = {0x4E, 0x45, 0x53, 0x1A, 0x01, 0x01, 0x00,
                                    0x00, 0xAA, 0xBB};
    auto decl = make_decl(payload, sizeof(payload));
    content::ContentTransferControllerV1 offerer;
    content::ContentTransferControllerV1 receiver;
    offerer.reset(content::ContentOfferRoleV1::Offerer);
    receiver.reset(content::ContentOfferRoleV1::Receiver);
    offerer.attach_connection(7, false);
    receiver.attach_connection(7, true);
    check(offerer.offer(decl, payload, sizeof(payload)) == FLY_SESSION_V2_OK,
          "offerer has payload");
    check(receiver.accept_offer(decl) == FLY_SESSION_V2_OK, "receiver has offer");
    check(offerer.approve_send() == FLY_SESSION_V2_OK &&
              receiver.approve_receive() == FLY_SESSION_V2_OK &&
              offerer.approve_receive() == FLY_SESSION_V2_OK &&
              receiver.approve_send() == FLY_SESSION_V2_OK,
          "both consents match");
    pump(offerer, receiver);
    check(offerer.stream_open() && receiver.stream_open(),
          "ROM stream opened on both sides");
    check(offerer.bulk_writes() >= 2u, "offer + at least one chunk were written");
    check(receiver.scheduler().transferred_bytes() == sizeof(payload),
          "receiver assembled the declared payload");
    check(receiver.scheduler().staging_live() &&
              !receiver.scheduler().catalog_published(),
          "verified bytes wait on APPROVE_IMPORT");
    check(receiver.approve_import() == FLY_SESSION_V2_OK &&
              receiver.scheduler().catalog_published(),
          "import publishes catalog once");
}

void refuse_writes_zero_bytes()
{
    std::puts("content transfer: refuse writes zero bulk bytes");
    const std::uint8_t payload[] = {0x01, 0x02};
    auto decl = make_decl(payload, sizeof(payload));
    content::ContentTransferControllerV1 offerer;
    offerer.reset(content::ContentOfferRoleV1::Offerer);
    offerer.attach_connection(7, false);
    check(offerer.offer(decl, payload, sizeof(payload)) == FLY_SESSION_V2_OK,
          "offer binds");
    check(offerer.cancel() == FLY_SESSION_V2_OK, "offerer cancels");
    check(!offerer.poll_effect().has_value() && offerer.bulk_writes() == 0,
          "a refused transfer never writes ROM bytes");
    check(!offerer.scheduler().dual_available(),
          "refused transfer leaves DUAL unavailable");
}

} // namespace

int main()
{
    send_only_does_not_open_stream();
    both_consents_move_bytes_on_rom_stream();
    refuse_writes_zero_bytes();
    if (failures != 0)
    {
        std::fprintf(stderr, "%d content-transfer checks failed\n", failures);
        return 1;
    }
    std::puts("flynes_content_transfer_controller passed");
    return 0;
}
