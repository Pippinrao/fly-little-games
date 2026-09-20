/*
 * ENG-05: pending-config fingerprint from a catalog choice.
 *
 * SELECT_CONTENT binds a pending config. It must not mark seats confirmed, must
 * not treat a UI display name or source_choice_ref as the fingerprint, and must
 * drop both confirms when the bound config changes. Unverified / mismatched
 * catalog rows cannot become the pending config.
 */

#include "dual/canonical_input_wire.hpp"
#include "dual/dual_session_controller.hpp"
#include "wire/app_frame.hpp"
#include "wire/sha256.hpp"
#include <flynes/product/dual_start_identity.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using flynes::session::dual::DualSessionController;
using flynes::session::dual::DualStartInputsV1;
using flynes::session::dual::DualInputKeyV1;
using flynes::session::dual::DualPortInputArrayV1;
using flynes::session::dual::DualInputBundleV1;
using flynes::session::dual::DualInputStatusV1;
using flynes::session::dual::DualInputWireContextV1;
using flynes::session::dual::DualInputWireStatusV1;
using flynes::session::dual::canonical_input_build_v1;
using flynes::session::dual::encode_dual_input_bundle_v1;
using flynes::session::dual::kDualPortCountV1;
using flynes::session::dual::kDualInputWireBytesV1;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

bool id_zero(const std::uint8_t* bytes, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i)
    {
        if (bytes[i] != 0)
            return false;
    }
    return true;
}

fly_session_game_choice_v2 make_choice(std::uint8_t tag, const char* name,
                                       std::uint32_t selectable)
{
    fly_session_game_choice_v2 choice{};
    choice.struct_size = FLY_SESSION_GAME_CHOICE_V2_SIZE;
    choice.abi_version = FLY_SESSION_ABI_VERSION_2;
    choice.source_choice_ref[0] = tag;
    choice.content_id[0] = static_cast<std::uint8_t>(tag + 0x40u);
    choice.catalog_revision = 1;
    choice.progress_revision = 1;
    choice.selectable = selectable;
    choice.core_id[0] = static_cast<std::uint8_t>(tag + 0x80u);
    choice.profile_id[0] = static_cast<std::uint8_t>(tag + 0x10u);
    choice.options_id[0] = static_cast<std::uint8_t>(tag + 0x20u);
    const auto name_size = static_cast<std::uint32_t>(std::strlen(name));
    choice.display_name_size = name_size;
    std::memcpy(choice.display_name, name, name_size);
    return choice;
}

void seed_controller(DualSessionController& controller,
                     const fly_session_game_choice_v2& choice)
{
    controller.begin_catalog();
    check(controller.publish_imported_choice(choice) == FLY_SESSION_V2_OK,
          "catalog accepts a typed choice ref");
}

void store_be32(std::uint8_t* bytes, std::uint32_t value)
{
    bytes[0] = static_cast<std::uint8_t>(value >> 24u);
    bytes[1] = static_cast<std::uint8_t>(value >> 16u);
    bytes[2] = static_cast<std::uint8_t>(value >> 8u);
    bytes[3] = static_cast<std::uint8_t>(value);
}

std::array<std::uint8_t, 32> named_id(const char* domain, const char* name)
{
    return flynes::session::wire::domain_hash(
        domain, reinterpret_cast<const std::uint8_t*>(name), std::strlen(name));
}

void store_u32le(std::uint8_t* bytes, std::uint32_t value)
{
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8u);
    bytes[2] = static_cast<std::uint8_t>(value >> 16u);
    bytes[3] = static_cast<std::uint8_t>(value >> 24u);
}

std::array<std::uint8_t, 32> applied_ntsc_2p_profile_id()
{
    std::uint8_t bytes[20]{};
    store_u32le(bytes + 0, 0u);   /* NES_FAVORED_NES_NTSC, nes_create default */
    store_u32le(bytes + 4, 2u);   /* P1+P2 controllers actually selected */
    store_u32le(bytes + 8, 0u);   /* NES_PIXFMT_RGB565, fly_runtime default */
    store_u32le(bytes + 12, 256u);
    store_u32le(bytes + 16, 240u);
    return flynes::session::wire::domain_hash(
        "flynes-dual-profile-id-v1", bytes, sizeof(bytes));
}

std::array<std::uint8_t, 32> applied_deterministic_options_id()
{
    std::uint8_t bytes[12]{};
    store_u32le(bytes + 0, 0u);     /* SetRamPowerState(0) before Power */
    store_u32le(bytes + 4, 48000u); /* FLY_RUNTIME_DEFAULT_SAMPLE_RATE */
    store_u32le(bytes + 8, 0u);     /* mono PCM from nes_run_frames */
    return flynes::session::wire::domain_hash(
        "flynes-dual-options-id-v1", bytes, sizeof(bytes));
}

std::vector<std::uint8_t> encode_choice_record(
    std::uint16_t version, const std::uint8_t ref[16],
    const std::uint8_t content[32], const char* name,
    const std::uint8_t* core, const std::uint8_t* profile,
    const std::uint8_t* options)
{
    const auto name_size = static_cast<std::uint32_t>(std::strlen(name));
    const auto tail = (version >= 2) ? 96u : 0u;
    std::vector<std::uint8_t> record(
        FLY_SESSION_CONTENT_CHOICE_V2_HEADER_SIZE + name_size + tail, 0);
    record[0] = static_cast<std::uint8_t>(version >> 8u);
    record[1] = static_cast<std::uint8_t>(version);
    std::memcpy(record.data() + 4, ref, 16u);
    std::memcpy(record.data() + 20, content, 32u);
    store_be32(record.data() + 52, name_size);
    std::memcpy(record.data() + 56, name, name_size);
    if (tail != 0)
    {
        auto* start = record.data() + FLY_SESSION_CONTENT_CHOICE_V2_HEADER_SIZE +
                      name_size;
        if (core)
            std::memcpy(start, core, 32u);
        if (profile)
            std::memcpy(start + 32, profile, 32u);
        if (options)
            std::memcpy(start + 64, options, 32u);
    }
    return record;
}

std::array<std::uint8_t, 32> choice_record_hash(
    const std::vector<std::uint8_t>& record)
{
    return flynes::session::wire::domain_hash(
        "flynes-content-choice-v1", record.data(), record.size());
}

void seed_from_record(DualSessionController& controller,
                      const std::vector<std::uint8_t>& record)
{
    controller.begin_catalog();
    check(controller.ingest_content_choice_record(
              record.data(), record.size(), choice_record_hash(record)) ==
              FLY_SESSION_V2_OK,
          "production catalog record is accepted");
    check(controller.on_content_empty() == FLY_SESSION_V2_OK,
          "catalog walk completes");
}

void test_select_does_not_confirm_seats_or_pending()
{
    std::puts("eng-05: SELECT is not confirm");
    DualSessionController controller;
    seed_controller(controller, make_choice(0x11, "alpha", 1));
    const std::uint8_t ref[16] = {0x11};
    check(controller.select_content(ref) == FLY_SESSION_V2_OK,
          "verified catalog row can be selected");

    fly_session_snapshot_v2 snapshot{};
    controller.fill_snapshot(snapshot);
    check(snapshot.dual_seats_confirmed == 0,
          "SELECT must not write dual_seats_confirmed");
    check(!id_zero(snapshot.pending_config_id, 32),
          "SELECT binds a pending_config_id fingerprint");
    check(snapshot.pending_config_local_confirmed == 0 &&
              snapshot.pending_config_peer_confirmed == 0,
          "one-sided confirm stays unset after SELECT");
    check(snapshot.pending_config_revision != 0,
          "a bound config has a non-zero revision");
    check(std::memcmp(snapshot.pending_config_id, snapshot.dual_content_hash,
                      32) != 0,
          "fingerprint is not the raw content hash");
    check(std::memcmp(snapshot.pending_config_id, ref, 16) != 0,
          "fingerprint is not the UI/catalog source_choice_ref");
}

void test_same_catalog_row_agrees_display_name_does_not_matter()
{
    std::puts("eng-05: same identity, same fingerprint");
    DualSessionController left;
    DualSessionController right;
    seed_controller(left, make_choice(0x21, "left-label", 1));
    seed_controller(right, make_choice(0x21, "right-label", 1));
    const std::uint8_t ref[16] = {0x21};
    check(left.select_content(ref) == FLY_SESSION_V2_OK &&
              right.select_content(ref) == FLY_SESSION_V2_OK,
          "both sides select the same typed choice ref");

    fly_session_snapshot_v2 a{};
    fly_session_snapshot_v2 b{};
    left.fill_snapshot(a);
    right.fill_snapshot(b);
    check(std::memcmp(a.pending_config_id, b.pending_config_id, 32) == 0,
          "both ends compute the same pending_config_id");
}

void test_different_content_or_unverified_cannot_bind()
{
    std::puts("eng-05: mismatch and unverified profile");
    DualSessionController left;
    DualSessionController right;
    seed_controller(left, make_choice(0x31, "rom-a", 1));
    seed_controller(right, make_choice(0x32, "rom-b", 1));
    const std::uint8_t left_ref[16] = {0x31};
    const std::uint8_t right_ref[16] = {0x32};
    check(left.select_content(left_ref) == FLY_SESSION_V2_OK &&
              right.select_content(right_ref) == FLY_SESSION_V2_OK,
          "each side can bind its own verified row");

    fly_session_snapshot_v2 a{};
    fly_session_snapshot_v2 b{};
    left.fill_snapshot(a);
    right.fill_snapshot(b);
    check(std::memcmp(a.pending_config_id, b.pending_config_id, 32) != 0,
          "different ROM identities must not share a pending_config_id");

    DualSessionController unverified;
    seed_controller(unverified, make_choice(0x33, "unknown-profile", 0));
    const std::uint8_t bad_ref[16] = {0x33};
    check(unverified.select_content(bad_ref) == FLY_SESSION_V2_INVALID_ARGUMENT,
          "unverified profile cannot become the pending config");
}

void test_config_change_clears_both_confirms()
{
    std::puts("eng-05: config change revokes confirms");
    DualSessionController controller;
    controller.begin_catalog();
    check(controller.publish_imported_choice(make_choice(0x41, "one", 1)) ==
              FLY_SESSION_V2_OK,
          "first row");
    check(controller.publish_imported_choice(make_choice(0x42, "two", 1)) ==
              FLY_SESSION_V2_OK,
          "second row");
    const std::uint8_t first[16] = {0x41};
    const std::uint8_t second[16] = {0x42};
    check(controller.select_content(first) == FLY_SESSION_V2_OK, "bind first");
    check(controller.confirm_local_pending() == FLY_SESSION_V2_OK,
          "local confirm of the bound fingerprint");
    std::uint8_t bound[32]{};
    {
        fly_session_snapshot_v2 snapshot{};
        controller.fill_snapshot(snapshot);
        std::memcpy(bound, snapshot.pending_config_id, 32);
        check(controller.apply_peer_pending_confirm(
                  bound, snapshot.pending_config_revision) == FLY_SESSION_V2_OK,
              "peer confirm of the same fingerprint");
        check(snapshot.pending_config_local_confirmed == 1, "local recorded");
    }
    {
        fly_session_snapshot_v2 snapshot{};
        controller.fill_snapshot(snapshot);
        check(snapshot.pending_config_peer_confirmed == 1, "peer recorded");
        check(controller.select_content(second) == FLY_SESSION_V2_OK,
              "rebinding to another ROM");
    }
    fly_session_snapshot_v2 after{};
    controller.fill_snapshot(after);
    check(std::memcmp(after.pending_config_id, bound, 32) != 0,
          "new fingerprint after the config change");
    check(after.pending_config_local_confirmed == 0 &&
              after.pending_config_peer_confirmed == 0,
          "both confirms are revoked when the bound config changes");
    check(after.dual_seats_confirmed == 0,
          "rebinding still must not mark seats confirmed");
}

void test_start_dual_requires_both_pending_confirms()
{
    std::puts("eng-06: START_DUAL is behind dual confirm");
    DualSessionController controller;
    seed_controller(controller, make_choice(0x51, "rom", 1));
    const std::uint8_t ref[16] = {0x51};
    check(controller.select_content(ref) == FLY_SESSION_V2_OK, "bind catalog row");

    flynes::session::dual::DualStartInputsV1 inputs{};
    inputs.quic_connection = 1;
    check(controller.start_dual(inputs) == FLY_SESSION_V2_INVALID_STATE,
          "SELECT alone must not start");
    check(controller.confirm_local_pending() == FLY_SESSION_V2_OK, "local confirm");
    check(controller.start_dual(inputs) == FLY_SESSION_V2_INVALID_STATE,
          "one-sided confirm must not start");

    fly_session_snapshot_v2 snapshot{};
    controller.fill_snapshot(snapshot);
    check(controller.apply_peer_pending_confirm(snapshot.pending_config_id,
                                                snapshot.pending_config_revision) ==
              FLY_SESSION_V2_OK,
          "peer confirm of the same fingerprint and revision");
    check(controller.start_dual(inputs) == FLY_SESSION_V2_UNAVAILABLE,
          "both confirms with no runtime is UNAVAILABLE, not a silent start");
}

void test_catalog_revision_is_not_the_fingerprint()
{
    std::puts("eng-05: catalog revision is local, not identity");
    auto left_choice = make_choice(0x61, "same-rom", 1);
    auto right_choice = make_choice(0x61, "same-rom", 1);
    left_choice.catalog_revision = 1;
    right_choice.catalog_revision = 9;
    DualSessionController left;
    DualSessionController right;
    seed_controller(left, left_choice);
    seed_controller(right, right_choice);
    const std::uint8_t ref[16] = {0x61};
    check(left.select_content(ref) == FLY_SESSION_V2_OK &&
              right.select_content(ref) == FLY_SESSION_V2_OK,
          "both sides select the same content identity");
    fly_session_snapshot_v2 a{};
    fly_session_snapshot_v2 b{};
    left.fill_snapshot(a);
    right.fill_snapshot(b);
    check(std::memcmp(a.pending_config_id, b.pending_config_id, 32) == 0,
          "local catalog_revision must not split the pending_config_id");
}

void test_core_profile_options_are_in_the_fingerprint()
{
    std::puts("eng-05: core/profile/options bind the fingerprint");
    auto left_choice = make_choice(0x71, "same-rom", 1);
    auto right_choice = make_choice(0x71, "same-rom", 1);
    left_choice.core_id[0] = 0xC1;
    right_choice.core_id[0] = 0xC2;
    DualSessionController left;
    DualSessionController right;
    seed_controller(left, left_choice);
    seed_controller(right, right_choice);
    const std::uint8_t ref[16] = {0x71};
    check(left.select_content(ref) == FLY_SESSION_V2_OK &&
              right.select_content(ref) == FLY_SESSION_V2_OK,
          "both sides select");
    fly_session_snapshot_v2 a{};
    fly_session_snapshot_v2 b{};
    left.fill_snapshot(a);
    right.fill_snapshot(b);
    check(std::memcmp(a.pending_config_id, b.pending_config_id, 32) != 0,
          "different core_id must not share a pending_config_id");

    right_choice.core_id[0] = 0xC1;
    right_choice.profile_id[0] = 0x01;
    DualSessionController right_profile;
    seed_controller(right_profile, right_choice);
    check(right_profile.select_content(ref) == FLY_SESSION_V2_OK, "profile variant");
    fly_session_snapshot_v2 c{};
    right_profile.fill_snapshot(c);
    check(std::memcmp(a.pending_config_id, c.pending_config_id, 32) != 0,
          "different profile_id must not share a pending_config_id");

    right_choice.profile_id[0] = 0;
    right_choice.options_id[0] = 0x0A;
    DualSessionController right_options;
    seed_controller(right_options, right_choice);
    check(right_options.select_content(ref) == FLY_SESSION_V2_OK, "options variant");
    fly_session_snapshot_v2 d{};
    right_options.fill_snapshot(d);
    check(std::memcmp(a.pending_config_id, d.pending_config_id, 32) != 0,
          "different options_id must not share a pending_config_id");
}

void test_unbound_start_conditions_cannot_confirm()
{
    std::puts("eng-05: unknown start conditions cannot be confirmed");
    auto choice = make_choice(0x91, "unbound", 1);
    std::memset(choice.core_id, 0, sizeof(choice.core_id));
    std::memset(choice.profile_id, 0, sizeof(choice.profile_id));
    std::memset(choice.options_id, 0, sizeof(choice.options_id));
    DualSessionController controller;
    seed_controller(controller, choice);
    const std::uint8_t ref[16] = {0x91};
    check(controller.select_content(ref) == FLY_SESSION_V2_OK,
          "a catalog row can still be selected");
    check(controller.confirm_local_pending() == FLY_SESSION_V2_INVALID_STATE,
          "zero core/profile/options cannot be confirmed");
    fly_session_snapshot_v2 snapshot{};
    controller.fill_snapshot(snapshot);
    check(controller.apply_peer_pending_confirm(snapshot.pending_config_id,
                                                snapshot.pending_config_revision) ==
              FLY_SESSION_V2_INVALID_STATE,
          "peer confirm of an unbound config is refused");
    check(snapshot.pending_config_local_confirmed == 0 &&
              snapshot.pending_config_peer_confirmed == 0,
          "unbound config leaves both confirms unset");
}

void test_rebind_to_a_rejects_stale_confirm()
{
    std::puts("eng-05: A then B then A rejects the old A confirm");
    DualSessionController controller;
    controller.begin_catalog();
    check(controller.publish_imported_choice(make_choice(0x81, "a", 1)) ==
              FLY_SESSION_V2_OK,
          "row A");
    check(controller.publish_imported_choice(make_choice(0x82, "b", 1)) ==
              FLY_SESSION_V2_OK,
          "row B");
    const std::uint8_t ref_a[16] = {0x81};
    const std::uint8_t ref_b[16] = {0x82};
    check(controller.select_content(ref_a) == FLY_SESSION_V2_OK, "bind A");
    check(controller.confirm_local_pending() == FLY_SESSION_V2_OK, "confirm A");
    fly_session_snapshot_v2 first{};
    controller.fill_snapshot(first);
    std::uint8_t old_id[32]{};
    std::memcpy(old_id, first.pending_config_id, 32);
    const std::uint64_t old_revision = first.pending_config_revision;
    check(controller.apply_peer_pending_confirm(old_id, old_revision) ==
              FLY_SESSION_V2_OK,
          "peer confirms A at revision 1");
    check(controller.select_content(ref_b) == FLY_SESSION_V2_OK, "bind B");
    check(controller.select_content(ref_a) == FLY_SESSION_V2_OK, "bind A again");
    fly_session_snapshot_v2 again{};
    controller.fill_snapshot(again);
    check(again.pending_config_revision != old_revision,
          "A→B→A must bump pending_config_revision");
    check(again.pending_config_local_confirmed == 0 &&
              again.pending_config_peer_confirmed == 0,
          "returning to A does not restore old confirms");
    check(controller.apply_peer_pending_confirm(old_id, old_revision) ==
              FLY_SESSION_V2_STALE,
          "the revision-1 A confirm is not valid for the new binding");
}

void test_v1_catalog_record_cannot_confirm()
{
    std::puts("eng-05: v1 catalog record has no start conditions");
    std::uint8_t ref[16]{};
    std::uint8_t content[32]{};
    ref[0] = 0xA1;
    content[0] = 0xB1;
    DualSessionController controller;
    seed_from_record(controller, encode_choice_record(1, ref, content, "v1-rom",
                                                      nullptr, nullptr, nullptr));
    check(controller.select_content(ref) == FLY_SESSION_V2_OK,
          "a v1 catalog row can still be selected");
    check(!controller.game_choices().empty() &&
              id_zero(controller.game_choices().front().core_id, 32) &&
              id_zero(controller.game_choices().front().profile_id, 32) &&
              id_zero(controller.game_choices().front().options_id, 32),
          "v1 parse leaves core/profile/options unbound");
    check(std::strcmp(controller.game_choices().front().reason_key,
                      "nearby_blocked_session_read") == 0,
          "unbound start conditions expose a real reason");
    check(controller.confirm_local_pending() == FLY_SESSION_V2_INVALID_STATE,
          "v1 catalog zeros cannot be confirmed");
}

void test_v2_catalog_record_binds_canonical_config()
{
    std::puts("eng-05: v2 catalog record binds canonical start conditions");
    const auto core = named_id("flynes-dual-core-id-v1", "1.53.2");
    const auto profile = applied_ntsc_2p_profile_id();
    const auto options = applied_deterministic_options_id();
    std::uint8_t ref[16]{};
    std::uint8_t content[32]{};
    ref[0] = 0xA2;
    std::memset(content, 0x44, sizeof(content));
    DualSessionController left;
    DualSessionController right;
    seed_from_record(left, encode_choice_record(2, ref, content, "left-name",
                                                core.data(), profile.data(),
                                                options.data()));
    seed_from_record(right, encode_choice_record(2, ref, content, "right-name",
                                                 core.data(), profile.data(),
                                                 options.data()));
    check(left.select_content(ref) == FLY_SESSION_V2_OK &&
              right.select_content(ref) == FLY_SESSION_V2_OK,
          "both ends select the production v2 row");
    check(std::memcmp(left.game_choices().front().core_id, core.data(), 32) == 0 &&
              std::memcmp(left.game_choices().front().profile_id, profile.data(),
                          32) == 0 &&
              std::memcmp(left.game_choices().front().options_id, options.data(),
                          32) == 0,
          "production parse copies catalog core/profile/options");
    check(left.confirm_local_pending() == FLY_SESSION_V2_OK,
          "canonical start conditions can be confirmed");
    fly_session_snapshot_v2 a{};
    fly_session_snapshot_v2 b{};
    left.fill_snapshot(a);
    right.fill_snapshot(b);
    check(std::memcmp(a.pending_config_id, b.pending_config_id, 32) == 0,
          "display name is not part of the shared config identity");
}

void test_unsupported_profile_from_catalog_cannot_confirm()
{
    std::puts("eng-05: unsupported profile cannot pretend verified");
    const auto core = named_id("flynes-dual-core-id-v1", "1.53.2");
    const auto options = applied_deterministic_options_id();
    std::array<std::uint8_t, 32> unknown_profile{};
    unknown_profile[0] = 0xEE;
    std::uint8_t ref[16]{};
    std::uint8_t content[32]{};
    ref[0] = 0xA3;
    content[0] = 0xB3;
    DualSessionController controller;
    seed_from_record(
        controller, encode_choice_record(2, ref, content, "bad-profile",
                                         core.data(), unknown_profile.data(),
                                         options.data()));
    check(!controller.game_choices().empty() &&
              controller.game_choices().front().selectable == 0,
          "an unsupported profile is not a verified catalog row");
    check(std::strcmp(controller.game_choices().front().reason_key,
                      "nearby_blocked_profile_verify") == 0,
          "unsupported profile exposes the profile-verify reason");
    check(controller.select_content(ref) == FLY_SESSION_V2_INVALID_ARGUMENT,
          "unsupported profile cannot become the pending config");
    check(controller.confirm_local_pending() == FLY_SESSION_V2_INVALID_STATE,
          "unsupported profile cannot be confirmed");
}

void test_shared_seating_not_local_seat()
{
    std::puts("eng-05: shared seating, not per-end local seat");
    const auto core = named_id("flynes-dual-core-id-v1", "1.53.2");
    const auto profile = applied_ntsc_2p_profile_id();
    const auto options = applied_deterministic_options_id();
    std::uint8_t ref[16]{};
    std::uint8_t content[32]{};
    ref[0] = 0xA4;
    std::memset(content, 0x55, sizeof(content));
    DualSessionController host;
    DualSessionController guest;
    seed_from_record(host, encode_choice_record(2, ref, content, "same",
                                                core.data(), profile.data(),
                                                options.data()));
    seed_from_record(guest, encode_choice_record(2, ref, content, "same",
                                                 core.data(), profile.data(),
                                                 options.data()));
    host.set_observed_local_seat(0);
    guest.set_observed_local_seat(1);
    check(host.select_content(ref) == FLY_SESSION_V2_OK &&
              guest.select_content(ref) == FLY_SESSION_V2_OK,
          "both roles select the same canonical config");
    fly_session_snapshot_v2 a{};
    fly_session_snapshot_v2 b{};
    host.fill_snapshot(a);
    guest.fill_snapshot(b);
    check(std::memcmp(a.pending_config_id, b.pending_config_id, 32) == 0,
          "initiator and joiner local seats must not split pending_config_id");
}

void test_divergent_select_histories_share_proposal_revision()
{
    std::puts("eng-06: different local select histories still share revision");
    DualSessionController left;
    DualSessionController right;
    left.begin_catalog();
    right.begin_catalog();
    check(left.publish_imported_choice(make_choice(0x51, "one", 1)) ==
              FLY_SESSION_V2_OK,
          "left row one");
    check(left.publish_imported_choice(make_choice(0x52, "two", 1)) ==
              FLY_SESSION_V2_OK,
          "left row two");
    check(right.publish_imported_choice(make_choice(0x51, "one", 1)) ==
              FLY_SESSION_V2_OK,
          "right row one");
    check(right.publish_imported_choice(make_choice(0x52, "two", 1)) ==
              FLY_SESSION_V2_OK,
          "right row two");
    const std::uint8_t first[16] = {0x51};
    const std::uint8_t second[16] = {0x52};
    check(left.select_content(first) == FLY_SESSION_V2_OK, "left binds one");
    check(left.select_content(second) == FLY_SESSION_V2_OK, "left rebinds two");
    check(right.select_content(second) == FLY_SESSION_V2_OK, "right binds two only");
    fly_session_snapshot_v2 a{};
    fly_session_snapshot_v2 b{};
    left.fill_snapshot(a);
    right.fill_snapshot(b);
    check(std::memcmp(a.pending_config_id, b.pending_config_id, 32) == 0,
          "both ends currently propose the same config identity");
    check(a.pending_config_revision == b.pending_config_revision &&
              a.pending_config_revision == 1,
          "the shared proposal revision is the per-id bind count, not a local counter");
    check(left.confirm_local_pending() == FLY_SESSION_V2_OK, "left local confirm");
    check(right.apply_peer_pending_confirm(a.pending_config_id,
                                           a.pending_config_revision) ==
              FLY_SESSION_V2_OK,
          "right accepts left's confirm at the common proposal revision");
}

void retain_noop(void*) {}
void release_noop(void*) {}

fly_session_result_v2 stub_load(void*, const fly_session_dual_content_ref_v2*)
{
    return FLY_SESSION_V2_OK;
}
fly_session_result_v2 stub_step(void*, const fly_session_dual_input_bundle_v2*,
                                fly_session_dual_frame_outcome_v2* out)
{
    if (out == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    *out = {};
    return FLY_SESSION_V2_OK;
}
fly_session_result_v2 stub_export(void*, std::uint8_t* out, std::size_t capacity,
                                  std::size_t* written, std::uint8_t hash[32])
{
    if (out == nullptr || written == nullptr || hash == nullptr || capacity < 32u)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    std::memset(out, 0x5A, 32u);
    *written = 32u;
    std::memset(hash, 0x5A, 32u);
    return FLY_SESSION_V2_OK;
}
fly_session_result_v2 stub_import(void*, const std::uint8_t*, std::size_t)
{
    return FLY_SESSION_V2_OK;
}
fly_session_result_v2 stub_digest(void*, std::uint64_t,
                                  fly_session_dual_state_digest_v2* out)
{
    if (out == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    *out = {};
    return FLY_SESSION_V2_OK;
}

fly_session_dual_runtime_port_v2 stub_runtime_port()
{
    fly_session_dual_runtime_port_v2 port{};
    port.struct_size = FLY_SESSION_DUAL_RUNTIME_PORT_V2_SIZE;
    port.abi_version = FLY_SESSION_ABI_VERSION_2;
    port.retain = retain_noop;
    port.release = release_noop;
    port.load = stub_load;
    port.step = stub_step;
    port.export_state = stub_export;
    port.import_state = stub_import;
    port.state_digest = stub_digest;
    return port;
}

std::array<std::uint8_t, 32> owner_from_public(const std::uint8_t public_key[65])
{
    return flynes::session::wire::domain_hash(
        "flynes-dual-owner-key-v1", public_key, 65u);
}

bool start_controller_ready(DualSessionController& controller,
                            fly_session_dual_runtime_port_v2* port,
                            DualStartInputsV1* inputs)
{
    auto choice = make_choice(0xC1, "ready-rom", 1);
    std::memset(choice.content_id, 0x44, sizeof(choice.content_id));
    const auto core = named_id("flynes-dual-core-id-v1", "1.53.2");
    const auto profile = applied_ntsc_2p_profile_id();
    const auto options = applied_deterministic_options_id();
    std::memcpy(choice.core_id, core.data(), 32u);
    std::memcpy(choice.profile_id, profile.data(), 32u);
    std::memcpy(choice.options_id, options.data(), 32u);
    seed_controller(controller, choice);
    const std::uint8_t ref[16] = {0xC1};
    if (controller.select_content(ref) != FLY_SESSION_V2_OK)
        return false;
    if (controller.confirm_local_pending() != FLY_SESSION_V2_OK)
        return false;
    fly_session_snapshot_v2 snapshot{};
    controller.fill_snapshot(snapshot);
    if (controller.apply_peer_pending_confirm(snapshot.pending_config_id,
                                              snapshot.pending_config_revision) !=
        FLY_SESSION_V2_OK)
        return false;
    *inputs = {};
    inputs->session_id[0] = 0x11;
    inputs->branch_id[0] = 0x22;
    inputs->local_role = flynes::session::wire::PairRoleV1::Initiator;
    std::memset(inputs->local_signing_public.data(), 0xA1, 65u);
    std::memset(inputs->peer_signing_public.data(), 0xB1, 65u);
    inputs->quic_connection = 1;
    inputs->runtime = port;
    if (controller.start_dual(*inputs) != FLY_SESSION_V2_OK)
        return false;
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.result = FLY_SESSION_V2_OK;
    flynes::session::ParsedProviderEvent parsed;
    parsed.resource = 7;
    parsed.value0 = 8;
    return controller.complete(DualSessionController::EffectKind::OpenStream, event,
                               parsed) == FLY_SESSION_V2_OK;
}

std::vector<std::uint8_t> ready_beacon_frame(const DualInputKeyV1& key,
                                             std::uint8_t seat,
                                             const std::array<std::uint8_t, 32>& owner)
{
    DualPortInputArrayV1 samples{};
    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port)
    {
        samples[port].sequence = static_cast<std::uint64_t>(port) + 1u;
        samples[port].mask = 0;
    }
    DualInputBundleV1 bundle{};
    if (canonical_input_build_v1(key, seat, owner, samples, &bundle) !=
        DualInputStatusV1::Ok)
        return {};
    DualInputWireContextV1 wire_context{};
    wire_context.authority_term = 1;
    wire_context.mode_generation = 1;
    wire_context.batch_sequence = 1;
    std::uint8_t object[kDualInputWireBytesV1] = {};
    if (encode_dual_input_bundle_v1(bundle, wire_context, object) !=
        DualInputWireStatusV1::Ok)
        return {};
    std::vector<std::uint8_t> framed(6u + kDualInputWireBytesV1, 0);
    std::size_t written = 0;
    if (flynes::session::wire::encode_app_frame(
            0xFF02u, object, kDualInputWireBytesV1, framed.data(), framed.size(),
            &written) != flynes::session::wire::Status::Ok ||
        written != framed.size())
        return {};
    return framed;
}

fly_session_result_v2 ingest_framed(DualSessionController& controller,
                                    const std::vector<std::uint8_t>& framed)
{
    if (framed.empty())
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    fly_session_bytes_v2 source{};
    source.data = framed.data();
    source.size = static_cast<std::uint32_t>(framed.size());
    flynes::session::ParsedProviderEvent parsed;
    if (fly_session_buffer_create_copy_v2(source, &parsed.buffer) !=
        FLY_SESSION_V2_OK)
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.result = FLY_SESSION_V2_OK;
    return controller.complete(DualSessionController::EffectKind::GrantRead, event,
                               parsed);
}

void test_wrong_session_ready_packet_does_not_start()
{
    std::puts("eng-06: wrong-session ready packet is not peer ready");
    DualSessionController controller;
    auto port = stub_runtime_port();
    DualStartInputsV1 inputs{};
    check(start_controller_ready(controller, &port, &inputs),
          "controller reaches local runtime-ready");
    DualInputKeyV1 key{};
    key.session_id = inputs.session_id;
    key.session_id[0] ^= 0xFFu;
    key.branch_id = inputs.branch_id;
    key.timeline_epoch = 1;
    key.seat_revision = 1;
    const auto framed =
        ready_beacon_frame(key, 1, owner_from_public(inputs.peer_signing_public.data()));
    const auto ingested = ingest_framed(controller, framed);
    check(ingested == FLY_SESSION_V2_STALE,
          "a legal input for another session is STALE");
    check(!controller.running() &&
              controller.game_state() != FLY_SESSION_GAME_RUNNING_V2,
          "wrong-session ready does not enter GAME_RUNNING");
}

void test_wrong_branch_ready_packet_does_not_start()
{
    std::puts("eng-06: wrong-branch ready packet is not peer ready");
    DualSessionController controller;
    auto port = stub_runtime_port();
    DualStartInputsV1 inputs{};
    check(start_controller_ready(controller, &port, &inputs),
          "controller reaches local runtime-ready");
    DualInputKeyV1 key{};
    key.session_id = inputs.session_id;
    key.branch_id = inputs.branch_id;
    key.branch_id[0] ^= 0xFFu;
    key.timeline_epoch = 1;
    key.seat_revision = 1;
    const auto framed =
        ready_beacon_frame(key, 1, owner_from_public(inputs.peer_signing_public.data()));
    check(ingest_framed(controller, framed) == FLY_SESSION_V2_STALE,
          "a legal input for another branch is STALE");
    check(!controller.running() &&
              controller.game_state() != FLY_SESSION_GAME_RUNNING_V2,
          "wrong-branch ready does not enter GAME_RUNNING");
}

void test_stale_epoch_ready_packet_does_not_start()
{
    std::puts("eng-06: stale-epoch ready packet is not peer ready");
    DualSessionController controller;
    auto port = stub_runtime_port();
    DualStartInputsV1 inputs{};
    check(start_controller_ready(controller, &port, &inputs),
          "controller reaches local runtime-ready");
    DualInputKeyV1 key{};
    key.session_id = inputs.session_id;
    key.branch_id = inputs.branch_id;
    key.timeline_epoch = 2;
    key.seat_revision = 1;
    const auto framed =
        ready_beacon_frame(key, 1, owner_from_public(inputs.peer_signing_public.data()));
    check(ingest_framed(controller, framed) == FLY_SESSION_V2_STALE,
          "a legal input from another timeline epoch is STALE");
    check(!controller.running() &&
              controller.game_state() != FLY_SESSION_GAME_RUNNING_V2,
          "stale-epoch ready does not enter GAME_RUNNING");
}

void test_wrong_seat_revision_ready_packet_does_not_start()
{
    std::puts("eng-06: wrong-seat-revision ready packet is not peer ready");
    DualSessionController controller;
    auto port = stub_runtime_port();
    DualStartInputsV1 inputs{};
    check(start_controller_ready(controller, &port, &inputs),
          "controller reaches local runtime-ready");
    DualInputKeyV1 key{};
    key.session_id = inputs.session_id;
    key.branch_id = inputs.branch_id;
    key.timeline_epoch = 1;
    key.seat_revision = 2;
    const auto framed =
        ready_beacon_frame(key, 1, owner_from_public(inputs.peer_signing_public.data()));
    check(ingest_framed(controller, framed) == FLY_SESSION_V2_STALE,
          "a legal input with another seat_revision is STALE");
    check(!controller.running() &&
              controller.game_state() != FLY_SESSION_GAME_RUNNING_V2,
          "wrong-seat-revision ready does not enter GAME_RUNNING");
}

void test_legal_ready_after_stale_rejection_still_starts()
{
    std::puts("eng-06: legal ready after a stale rejection still starts");
    DualSessionController controller;
    auto port = stub_runtime_port();
    DualStartInputsV1 inputs{};
    check(start_controller_ready(controller, &port, &inputs),
          "controller reaches local runtime-ready");
    DualInputKeyV1 stale{};
    stale.session_id = inputs.session_id;
    stale.branch_id = inputs.branch_id;
    stale.timeline_epoch = 1;
    stale.seat_revision = 99;
    const auto owner = owner_from_public(inputs.peer_signing_public.data());
    check(ingest_framed(controller, ready_beacon_frame(stale, 1, owner)) ==
              FLY_SESSION_V2_STALE,
          "the stale ready packet is rejected");
    DualInputKeyV1 legal{};
    legal.session_id = inputs.session_id;
    legal.branch_id = inputs.branch_id;
    legal.timeline_epoch = 1;
    legal.seat_revision = 1;
    check(ingest_framed(controller, ready_beacon_frame(legal, 1, owner)) ==
              FLY_SESSION_V2_OK,
          "a matching ready packet is still accepted after the stale rejection");
    check(controller.running() &&
              controller.game_state() == FLY_SESSION_GAME_RUNNING_V2,
          "legal ready after stale rejection enters GAME_RUNNING");
}

void test_legacy_profile_name_hash_is_not_verified()
{
    std::puts("eng-05: default-2p name hash is not the profile identity");
    const auto named_profile = named_id("flynes-dual-profile-id-v1", "default-2p");
    const auto named_options =
        named_id("flynes-dual-options-id-v1", "deterministic");
    const auto applied = applied_ntsc_2p_profile_id();
    const auto core = named_id("flynes-dual-core-id-v1", "1.53.2");
    check(std::memcmp(named_profile.data(), applied.data(), 32) != 0,
          "NTSC 2P RGB565 256x240 is not the default-2p name hash");
    std::uint8_t ref[16]{};
    std::uint8_t content[32]{};
    ref[0] = 0xA5;
    content[0] = 0xB5;
    DualSessionController controller;
    seed_from_record(controller,
                     encode_choice_record(2, ref, content, "named-profile",
                                          core.data(), named_profile.data(),
                                          named_options.data()));
    check(!controller.game_choices().empty() &&
              controller.game_choices().front().selectable == 0,
          "legacy default-2p profile id is not a verified catalog row");
    check(controller.select_content(ref) == FLY_SESSION_V2_INVALID_ARGUMENT,
          "legacy default-2p profile id cannot become the pending config");
}

void test_legacy_options_name_hash_is_not_verified()
{
    std::puts("eng-05: deterministic name hash is not the options identity");
    const auto named_profile = named_id("flynes-dual-profile-id-v1", "default-2p");
    const auto named_options =
        named_id("flynes-dual-options-id-v1", "deterministic");
    const auto applied = applied_deterministic_options_id();
    const auto core = named_id("flynes-dual-core-id-v1", "1.53.2");
    check(std::memcmp(named_options.data(), applied.data(), 32) != 0,
          "RAM0 48000Hz mono is not the deterministic name hash");
    std::uint8_t ref[16]{};
    std::uint8_t content[32]{};
    ref[0] = 0xA6;
    content[0] = 0xB6;
    DualSessionController controller;
    seed_from_record(controller,
                     encode_choice_record(2, ref, content, "named-options",
                                          core.data(), named_profile.data(),
                                          named_options.data()));
    check(!controller.game_choices().empty() &&
              controller.game_choices().front().selectable == 0,
          "legacy deterministic options id is not a verified catalog row");
    check(controller.select_content(ref) == FLY_SESSION_V2_INVALID_ARGUMENT,
          "legacy deterministic options id cannot become the pending config");
}

void test_required_core_profile_options_are_the_canonical_identity()
{
    std::puts("eng-05: required identity is favored NTSC 2P / RAM0 options");
    const auto core = named_id("flynes-dual-core-id-v1", "1.53.2");
    const auto profile = applied_ntsc_2p_profile_id();
    const auto options = applied_deterministic_options_id();
    std::uint8_t pal[20]{};
    store_u32le(pal + 0, 1u); /* NES_FAVORED_NES_PAL */
    store_u32le(pal + 4, 2u);
    store_u32le(pal + 8, 0u);
    store_u32le(pal + 12, 256u);
    store_u32le(pal + 16, 240u);
    const auto pal_profile = flynes::session::wire::domain_hash(
        "flynes-dual-profile-id-v1", pal, sizeof(pal));
    check(std::memcmp(profile.data(), pal_profile.data(), 32) != 0,
          "favored PAL and favored NTSC are distinct required configurations");
    std::uint8_t ref[16]{};
    std::uint8_t content[32]{};
    ref[0] = 0xA7;
    std::memset(content, 0x77, sizeof(content));
    DualSessionController ok;
    seed_from_record(ok, encode_choice_record(2, ref, content, "applied",
                                              core.data(), profile.data(),
                                              options.data()));
    check(ok.select_content(ref) == FLY_SESSION_V2_OK,
          "required favored NTSC 2P / RAM0 48000Hz is a canonical catalog row");
    DualSessionController pal_row;
    seed_from_record(pal_row,
                     encode_choice_record(2, ref, content, "pal", core.data(),
                                          pal_profile.data(), options.data()));
    check(!pal_row.game_choices().empty() &&
              pal_row.game_choices().front().selectable == 0,
          "favored PAL configuration is not the required DUAL identity");
    check(pal_row.select_content(ref) == FLY_SESSION_V2_INVALID_ARGUMENT,
          "favored PAL configuration cannot become pending; this is not actual ROM region");
}

void test_legacy_core_name_hash_is_not_verified()
{
    std::puts("eng-05: nestopiaue name hash is not the core identity");
    const auto legacy = named_id("flynes-dual-core-id-v1", "nestopiaue");
    const auto core = named_id("flynes-dual-core-id-v1", "1.53.2");
    const auto profile = applied_ntsc_2p_profile_id();
    const auto options = applied_deterministic_options_id();
    check(std::memcmp(legacy.data(), core.data(), 32) != 0,
          "NestopiaUE 1.53.2 is not the nestopiaue name hash");
    std::uint8_t ref[16]{};
    std::uint8_t content[32]{};
    ref[0] = 0xA4;
    content[0] = 0xB4;
    DualSessionController controller;
    seed_from_record(controller,
                     encode_choice_record(2, ref, content, "legacy-core",
                                          legacy.data(), profile.data(),
                                          options.data()));
    check(!controller.game_choices().empty() &&
              controller.game_choices().front().selectable == 0,
          "legacy nestopiaue core id is not a verified catalog row");
    check(controller.select_content(ref) == FLY_SESSION_V2_INVALID_ARGUMENT,
          "legacy nestopiaue core id cannot become the pending config");
}

} // namespace

int main()
{
    const auto identity = flynes::product::canonical_dual_start_identity_v1();
    const std::array<const char*, 3> golden{{
        "24a46456f6ab9119bd3e15ea4fa7e1ad695aed2e8544e2d2123bf44dd5e6179f",
        "d9971fcfaba3d4df8a35701cf125c2066d9eec812ae936faaa39fefc60c0847c",
        "466403662fc9d885cf5a30946e34e557c2f1668657a439618ec082fbdd762229"}};
    const std::array<std::array<std::uint8_t, 32>, 3> ids{{
        identity.core_id, identity.profile_id, identity.options_id}};
    for (std::size_t id = 0; id < ids.size(); ++id)
    {
        char hex[65]{};
        for (std::size_t byte = 0; byte < 32; ++byte)
            std::snprintf(hex + byte * 2, 3, "%02x", ids[id][byte]);
        check(std::strcmp(hex, golden[id]) == 0,
              "public required-start helper preserves the exact existing golden ID");
    }
    const std::uint8_t ref[16] = {0xA8};
    const std::uint8_t content[32] = {0xB8};
    DualSessionController public_ids;
    seed_from_record(public_ids, encode_choice_record(2, ref, content, "public-ids",
        identity.core_id.data(), identity.profile_id.data(), identity.options_id.data()));
    check(public_ids.select_content(ref) == FLY_SESSION_V2_OK,
          "controller selects the public helper's canonical required-start record");
    fly_session_snapshot_v2 public_snapshot{};
    public_ids.fill_snapshot(public_snapshot);
    check(!id_zero(public_snapshot.pending_config_id, 32),
          "public helper produces a bound pending config, not an unbound zero tail");
    check(public_ids.confirm_local_pending() == FLY_SESSION_V2_OK,
          "public helper's nonzero required-start IDs permit actual local confirmation");
    test_select_does_not_confirm_seats_or_pending();
    test_same_catalog_row_agrees_display_name_does_not_matter();
    test_different_content_or_unverified_cannot_bind();
    test_config_change_clears_both_confirms();
    test_start_dual_requires_both_pending_confirms();
    test_catalog_revision_is_not_the_fingerprint();
    test_core_profile_options_are_in_the_fingerprint();
    test_unbound_start_conditions_cannot_confirm();
    test_rebind_to_a_rejects_stale_confirm();
    test_v1_catalog_record_cannot_confirm();
    test_v2_catalog_record_binds_canonical_config();
    test_unsupported_profile_from_catalog_cannot_confirm();
    test_legacy_core_name_hash_is_not_verified();
    test_legacy_profile_name_hash_is_not_verified();
    test_legacy_options_name_hash_is_not_verified();
    test_required_core_profile_options_are_the_canonical_identity();
    test_wrong_session_ready_packet_does_not_start();
    test_wrong_branch_ready_packet_does_not_start();
    test_stale_epoch_ready_packet_does_not_start();
    test_wrong_seat_revision_ready_packet_does_not_start();
    test_legal_ready_after_stale_rejection_still_starts();
    test_shared_seating_not_local_seat();
    test_divergent_select_histories_share_proposal_revision();
    if (failures != 0)
    {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("flynes_game_config_consent passed");
    return 0;
}
