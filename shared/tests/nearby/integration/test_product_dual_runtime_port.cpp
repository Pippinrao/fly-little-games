#include <flynes/product/dual_runtime_port.hpp>
#include <nes/nes.h>
#include "wire/sha256.hpp"

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <new>

#if defined(FLYNES_TEST_WRAP_ALLOC)
// Faults are confined to this Linux test executable, using the real public APIs.
static unsigned fail_calloc_at = 0;
static bool fail_next_new = false;
static bool fail_after_rom_load = false;
extern "C" int __real_nes_load_rom(nes_t*, const std::uint8_t*, std::size_t, nes_rom_info*);
extern "C" int __wrap_nes_load_rom(nes_t* core, const std::uint8_t* bytes, std::size_t size, nes_rom_info* info)
{
    const int result = __real_nes_load_rom(core, bytes, size, info);
    if (fail_after_rom_load) { fail_after_rom_load = false; throw std::bad_alloc(); }
    return result;
}
extern "C" void* __real_calloc(std::size_t, std::size_t);
extern "C" void* __wrap_calloc(std::size_t count, std::size_t size)
{
    if (fail_calloc_at != 0 && --fail_calloc_at == 0) return nullptr;
    return __real_calloc(count, size);
}
extern "C" void* __real__Znwm(std::size_t);
extern "C" void* __wrap__Znwm(std::size_t size)
{
    if (fail_next_new) { fail_next_new = false; throw std::bad_alloc(); }
    return __real__Znwm(size);
}
#endif

namespace {
using flynes::product::AuthorizedRom;
using flynes::product::ProductDualRuntimePort;
using flynes::session::wire::sha256;

void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

struct Runtime final
{
    fly_runtime_t* value = nullptr;
    explicit Runtime(unsigned sample_rate = 48000)
    {
        fly_runtime_config config{FLY_RUNTIME_CONFIG_V1_SIZE,
                                  FLY_RUNTIME_CONFIG_VERSION_1, sample_rate, 0};
        check(fly_runtime_create(&config, &value) == FLY_RESULT_OK, "runtime create");
    }
    ~Runtime() { fly_runtime_destroy(value); }
};

std::shared_ptr<const std::vector<std::uint8_t>> rom(const char* path = FLYNES_RUNTIME_ROM_FIXTURE)
{
    std::ifstream input(path, std::ios::binary);
    check(static_cast<bool>(input), "legal fixture opens");
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    check(!bytes->empty(), "legal fixture nonempty");
    return bytes;
}

struct Fixture final
{
    Runtime runtime;
    std::shared_ptr<const std::vector<std::uint8_t>> bytes = rom();
    fly_session_dual_content_ref_v2 content{};
    unsigned calls = 0;
    bool allowed = true;
    ProductDualRuntimePort adapter;
    fly_session_dual_runtime_port_v2 table;
    explicit Fixture(unsigned sample_rate = 48000) : runtime(sample_rate), adapter(runtime.value, [this](const auto& ref) {
        ++calls;
        check(std::memcmp(&ref, &content, sizeof(ref)) == 0,
              "resolver receives the complete content reference");
        return AuthorizedRom{allowed ? FLY_SESSION_V2_OK : FLY_SESSION_V2_PERMISSION_DENIED,
                             allowed ? bytes : nullptr};
    }), table(adapter.port())
    {
        content.session_id[0] = 11;
        content.branch_id[0] = 22;
        content.timeline_epoch = 7;
        auto hash = sha256(bytes->data(), bytes->size());
        std::memcpy(content.content_hash, hash.data(), hash.size());
    }
    void load() { check(table.load(table.context, &content) == FLY_SESSION_V2_OK,
                        "authorized load reaches borrowed runtime"); }
    fly_session_dual_input_bundle_v2 input(std::uint64_t frame = 0) const
    {
        fly_session_dual_input_bundle_v2 value{};
        value.struct_size = FLY_SESSION_DUAL_INPUT_BUNDLE_V2_SIZE;
        value.abi_version = FLY_SESSION_ABI_VERSION_2;
        std::memcpy(value.session_id, content.session_id, 16);
        std::memcpy(value.branch_id, content.branch_id, 16);
        value.timeline_epoch = content.timeline_epoch;
        value.frame_index = frame;
        value.seat_revision = 1;
        for (unsigned p = 0; p < 4; ++p) {
            value.ports[p].mask = 1u << p;
            value.ports[p].input_sequence = frame * 4 + p + 1;
        }
        return value;
    }
    void step(std::uint64_t frame = 0)
    {
        auto value = input(frame);
        fly_session_dual_frame_outcome_v2 out{};
        check(table.step(table.context, &value, &out) == FLY_SESSION_V2_OK,
              "canonical step succeeds");
        check(out.frame_index == frame && out.honoured_port_mask == 3 && out.reserved_zero == 0,
              "outcome reports actual frame and frozen P1/P2 support");
        for (unsigned p = 0; p < 4; ++p)
            check(out.applied_input_sequence[p] == value.ports[p].input_sequence,
                  "all four sequences passed unchanged");
    }
    fly_session_dual_state_digest_v2 digest(std::uint64_t frame = 0)
    {
        fly_session_dual_state_digest_v2 out{};
        check(table.state_digest(table.context, frame, &out) == FLY_SESSION_V2_OK,
              "committed exact frame digest succeeds");
        return out;
    }
};

void borrowed_runtime_and_pcm()
{
    Fixture f;
    f.load(); f.step();
    auto digest = f.digest();
    for (int repeat = 0; repeat < 3; ++repeat) {
        const auto again = f.digest();
        check(std::memcmp(&digest, &again, sizeof(digest)) == 0, "digest is repeatable");
    }
    fly_runtime_frame_digest_v1 direct{};
    direct.struct_size = FLY_RUNTIME_FRAME_DIGEST_V1_SIZE;
    direct.version = FLY_RUNTIME_FRAME_DIGEST_VERSION_1;
    check(fly_runtime_copy_frame_digest(f.runtime.value, 7, 0, &direct) == FLY_RESULT_OK,
          "external owner sees the same runtime frame");
    check(std::memcmp(digest.state, direct.state_sha256, 32) == 0 &&
          std::memcmp(digest.frame, direct.frame_sha256, 32) == 0 &&
          std::memcmp(digest.pcm, direct.pcm_sha256, 32) == 0, "three hashes copied exactly");
    std::vector<std::uint8_t> pixels(FLY_RUNTIME_RGB565_BYTES);
    fly_latest_frame_v1 meta{};
    meta.struct_size = FLY_LATEST_FRAME_V1_SIZE; meta.version = FLY_LATEST_FRAME_VERSION_1;
    check(fly_runtime_copy_latest_frame(f.runtime.value, pixels.data(), pixels.size(), &meta) == FLY_RESULT_OK &&
          meta.frame_index == 0 && meta.frame_sequence == 1, "original view observes advanced video");
    std::int16_t samples[4096]{};
    fly_pcm_block_v1 pcm{};
    pcm.struct_size = FLY_PCM_BLOCK_V1_SIZE; pcm.version = FLY_PCM_BLOCK_VERSION_1;
    check(fly_runtime_pull_pcm(f.runtime.value, samples, 4096, &pcm) == FLY_RESULT_OK &&
          pcm.sample_count > 0 && pcm.sample_count < 4096, "view receives all queued frame PCM after digests");
    std::vector<std::uint8_t> le;
    for (unsigned n = 0; n < pcm.sample_count; ++n) {
        const auto sample = static_cast<std::uint16_t>(samples[n]);
        le.push_back(static_cast<std::uint8_t>(sample));
        le.push_back(static_cast<std::uint8_t>(sample >> 8));
    }
    const auto pcm_hash = sha256(le.data(), le.size());
    check(std::memcmp(digest.pcm, pcm_hash.data(), 32) == 0, "adapter consumes none of the frame PCM");
    const auto after_pull = f.digest();
    check(std::memcmp(&digest, &after_pull, sizeof(digest)) == 0, "PCM consumer does not alter digest");
}

void validation_is_nonmutating()
{
    Fixture f; f.load(); f.step();
    auto original = f.digest();
    for (unsigned variant = 0; variant < 15; ++variant) {
        auto input = f.input(1);
        switch (variant) {
        case 0: input.session_id[0]++; break;
        case 1: input.branch_id[0]++; break;
        case 2: input.timeline_epoch++; break;
        case 3: input.struct_size--; break;
        case 4: input.abi_version++; break;
        case 5: input.reserved_zero[1] = 1; break;
        case 6: input.ports[2].reserved_zero = 1; break;
        case 7: input.ports[3].mask = 256; break;
        case 8: input.predicted_port_mask = 16; break;
        case 9: input.logical_seat = 4; break;
        case 10: input.seat_revision = 0; break;
        case 11: input.ports[0].input_sequence = 0; break;
        case 12: input.ports[2].input_sequence = input.ports[1].input_sequence; break;
        case 13: input.ports[0].mask = 0x30; break;
        default: input.frame_index = 3; break;
        }
        fly_session_dual_frame_outcome_v2 out;
        std::memset(&out, 0xA5, sizeof(out)); const auto before = out;
        check(f.table.step(f.table.context, &input, &out) < 0, "invalid input rejected");
        check(std::memcmp(&out, &before, sizeof(out)) == 0, "failed step leaves output untouched");
        const auto after = f.digest();
        check(std::memcmp(&original, &after, sizeof(after)) == 0, "invalid input never advances");
    }
    fly_session_dual_state_digest_v2 out;
    std::memset(&out, 0xB6, sizeof(out)); const auto before = out;
    check(f.table.state_digest(f.table.context, 1, &out) == FLY_SESSION_V2_INVALID_STATE &&
          std::memcmp(&out, &before, sizeof(out)) == 0, "wrong frame leaves digest output untouched");
    check(f.table.state_digest(f.table.context, 0, nullptr) == FLY_SESSION_V2_INVALID_ARGUMENT,
          "null digest rejected");
    auto input = f.input(1); input.predicted_port_mask = 2;
    fly_session_dual_frame_outcome_v2 outcome{};
    check(f.table.step(f.table.context, &input, &outcome) == FLY_SESSION_V2_OK, "prediction may execute");
    check(f.table.state_digest(f.table.context, 1, &out) == FLY_SESSION_V2_INVALID_STATE &&
          std::memcmp(&out, &before, sizeof(out)) == 0, "prediction is never a committed digest");
    f.step(2); f.digest(2);
}

void authorization_and_load_failure()
{
    Fixture f; f.load(); f.step();
    const auto before = f.digest();
    f.allowed = false;
    check(f.table.load(f.table.context, &f.content) == FLY_SESSION_V2_PERMISSION_DENIED,
          "source authorization denial preserved");
    f.allowed = true; f.content.content_hash[0] ^= 1;
    check(f.table.load(f.table.context, &f.content) == FLY_SESSION_V2_INVALID_ARGUMENT,
          "wrong ROM hash rejected");
    const auto after = f.digest();
    check(std::memcmp(&before, &after, sizeof(after)) == 0, "denied or wrong-hash load never touches runtime");
    f.bytes = std::make_shared<const std::vector<std::uint8_t>>(32, 0x5A);
    const auto bad_hash = sha256(f.bytes->data(), f.bytes->size());
    std::memcpy(f.content.content_hash, bad_hash.data(), 32);
    check(f.table.load(f.table.context, &f.content) < 0, "authorized corrupt ROM fails real load");
    fly_session_dual_state_digest_v2 digest{};
    check(f.table.state_digest(f.table.context, 0, &digest) == FLY_SESSION_V2_INVALID_STATE,
          "failed real load invalidates last step and binding");
    auto input = f.input(1); fly_session_dual_frame_outcome_v2 outcome{};
    check(f.table.step(f.table.context, &input, &outcome) == FLY_SESSION_V2_INVALID_STATE,
          "failed real load cannot leave a ready provider");
}

void retained_context_outlives_wrapper()
{
    Runtime runtime;
    auto marker = std::make_shared<int>(17); std::weak_ptr<int> weak = marker;
    fly_session_dual_runtime_port_v2 port{};
    {
        ProductDualRuntimePort adapter(runtime.value, [keep = marker](const auto&) {
            return AuthorizedRom{*keep == 17 ? FLY_SESSION_V2_PERMISSION_DENIED : FLY_SESSION_V2_UNAVAILABLE, nullptr};
        });
        marker.reset(); port = adapter.port();
        check(!weak.expired(), "context owns resolver");
        port.retain(port.context); port.retain(port.context);
    }
    check(!weak.expired(), "retained stable context survives wrapper destruction");
    fly_session_dual_content_ref_v2 ref{}; ref.session_id[0] = 1; ref.timeline_epoch = 1;
    check(port.load(port.context, &ref) == FLY_SESSION_V2_PERMISSION_DENIED,
          "retained callbacks still callable");
    port.release(port.context); check(!weak.expired(), "first release keeps second retain alive");
    port.release(port.context); check(weak.expired(), "final release destroys resolver exactly once");
    const auto bytes = rom();
    check(fly_runtime_load_rom(runtime.value, bytes->data(), bytes->size(), nullptr) == FLY_RESULT_OK,
          "adapter never destroys owner runtime");
}

std::vector<std::uint8_t> exported(Fixture& f)
{
    std::vector<std::uint8_t> bytes(1024 * 1024);
    std::size_t written = 0;
    std::array<std::uint8_t, 32> hash{};
    check(f.table.export_state(f.table.context, bytes.data(), bytes.size(), &written, hash.data()) ==
          FLY_SESSION_V2_OK, "export bound checkpoint succeeds");
    check(written > 96 && written <= bytes.size(), "checkpoint envelope has opaque payload");
    bytes.resize(written);
    check(sha256(bytes.data(), bytes.size()) == hash, "export hash covers complete checkpoint envelope");
    return bytes;
}

void checkpoint_cross_instance_and_restore()
{
    Fixture source, receiver, control;
    source.load(); receiver.load(); control.load();
    for (unsigned frame = 0; frame < 5; ++frame) {
        source.step(frame); control.step(frame);
    }
    auto saved = exported(source);
    check(receiver.table.import_state(receiver.table.context, saved.data(), saved.size()) == FLY_SESSION_V2_OK,
          "another instance accepts same-session checkpoint");
    fly_session_dual_state_digest_v2 sentinel;
    std::memset(&sentinel, 0xAC, sizeof(sentinel)); const auto before = sentinel;
    check(receiver.table.state_digest(receiver.table.context, 4, &sentinel) == FLY_SESSION_V2_INVALID_STATE &&
          std::memcmp(&sentinel, &before, sizeof(sentinel)) == 0, "import never fabricates last-step PCM");
    receiver.step(5); source.step(5); control.step(5);
    auto expected = control.digest(5);
    auto actual = receiver.digest(5);
    const auto source_actual = source.digest(5);
    if (std::memcmp(&expected, &actual, sizeof(actual)) != 0)
        std::fprintf(stderr, "restore frame5 state=%d video=%d pcm=%d source-control-state=%d\n",
                     std::memcmp(expected.state, actual.state, 32),
                     std::memcmp(expected.frame, actual.frame, 32),
                     std::memcmp(expected.pcm, actual.pcm, 32),
                     std::memcmp(expected.state, source_actual.state, 32));
    check(std::memcmp(&expected, &actual, sizeof(actual)) == 0, "cross-instance restore advances identically to control");
    source.step(6);
    check(source.table.import_state(source.table.context, saved.data(), saved.size()) == FLY_SESSION_V2_OK,
          "same-session save/load succeeds");
    check(source.table.state_digest(source.table.context, 4, &sentinel) == FLY_SESSION_V2_INVALID_STATE,
          "same-instance restore also invalidates last step");
    source.step(5); actual = source.digest(5);
    check(std::memcmp(&expected, &actual, sizeof(actual)) == 0, "save/load then step matches uninterrupted control");
}

void checkpoint_binding_and_failures()
{
    Fixture f; f.load();
    std::array<std::uint8_t, 256> out{}; out.fill(0xDD);
    const auto before_out = out;
    std::size_t written = 17;
    std::array<std::uint8_t, 32> hash{}; hash.fill(0xEE);
    const auto before_hash = hash;
    check(f.table.export_state(f.table.context, out.data(), out.size(), &written, hash.data()) == FLY_SESSION_V2_INVALID_STATE,
          "cannot export before first successful step");
    f.step();
    check(f.table.export_state(f.table.context, out.data(), 1, &written, hash.data()) == FLY_SESSION_V2_BUFFER_TOO_SMALL &&
          written == 17 && out == before_out && hash == before_hash, "small export never partially writes caller outputs");
    const auto saved = exported(f);
    const auto before = f.digest();
    // These offsets belong to the documented product envelope, never to the
    // runtime checkpoint payload. Runtime bytes remain opaque in this suite.
    constexpr std::size_t mutations[] = {0, 8, 12, 16, 32, 48, 80, 88};
    for (const auto offset : mutations) {
        auto bad = saved; bad[offset] ^= 1;
        check(f.table.import_state(f.table.context, bad.data(), bad.size()) < 0,
              "malformed or foreign checkpoint envelope rejected");
        const auto after = f.digest();
        check(std::memcmp(&before, &after, sizeof(after)) == 0, "bad envelope leaves runtime unchanged");
    }
    auto extra = saved; extra.push_back(0);
    check(f.table.import_state(f.table.context, extra.data(), extra.size()) < 0, "trailing checkpoint data rejected");
    check(f.table.import_state(f.table.context, saved.data(), 95) < 0, "truncated envelope rejected");
    check(f.table.import_state(f.table.context, nullptr, saved.size()) == FLY_SESSION_V2_INVALID_ARGUMENT,
          "null checkpoint rejected");

    Fixture foreign; foreign.content.timeline_epoch = 99; foreign.load(); foreign.step();
    auto wrong_inner_epoch = exported(foreign);
    std::copy_n(saved.begin(), 88, wrong_inner_epoch.begin());
    check(f.table.import_state(f.table.context, wrong_inner_epoch.data(), wrong_inner_epoch.size()) ==
          FLY_SESSION_V2_INVALID_STATE, "matching envelope cannot hide mismatched opaque payload epoch");
    const auto after_inner = f.digest();
    check(std::memcmp(&before, &after_inner, sizeof(before)) == 0, "inner epoch rejection occurs before runtime mutation");

    const auto old_input = f.input(1);
    f.content.session_id[0]++;
    f.load();
    check(f.table.import_state(f.table.context, saved.data(), saved.size()) == FLY_SESSION_V2_STALE,
          "new session rejects old checkpoint even with same ROM and epoch");
    fly_session_dual_frame_outcome_v2 outcome{};
    check(f.table.step(f.table.context, &old_input, &outcome) == FLY_SESSION_V2_STALE,
          "new successful load replaces the complete binding");
    f.step();
}

void four_ports_match_direct_runtime()
{
    Fixture f;
    Runtime direct;
    f.load();
    check(fly_runtime_load_rom(direct.value, f.bytes->data(), f.bytes->size(), f.content.content_hash) ==
          FLY_RESULT_OK, "control ROM load");
    for (unsigned frame = 0; frame < 12; ++frame) {
        const auto bundle = f.input(frame);
        f.step(frame);
        fly_frame_input_v1 input{};
        input.struct_size = FLY_FRAME_INPUT_V1_SIZE; input.version = FLY_FRAME_INPUT_VERSION_1;
        input.timeline_epoch = bundle.timeline_epoch; input.frame_index = frame;
        for (unsigned p = 0; p < 4; ++p) {
            input.buttons[p] = bundle.ports[p].mask;
            input.input_sequence[p] = bundle.ports[p].input_sequence;
        }
        fly_frame_result_v1 result{};
        result.struct_size = FLY_FRAME_RESULT_V1_SIZE; result.version = FLY_FRAME_RESULT_VERSION_1;
        check(fly_runtime_step_frame(direct.value, &input, &result) == FLY_RESULT_OK, "control step");
        fly_runtime_frame_digest_v1 expected{};
        expected.struct_size = FLY_RUNTIME_FRAME_DIGEST_V1_SIZE;
        expected.version = FLY_RUNTIME_FRAME_DIGEST_VERSION_1;
        check(fly_runtime_copy_frame_digest(direct.value, 7, frame, &expected) == FLY_RESULT_OK, "control digest");
        const auto actual = f.digest(frame);
        if (std::memcmp(actual.state, expected.state_sha256, 32) != 0 ||
            std::memcmp(actual.frame, expected.frame_sha256, 32) != 0 ||
            std::memcmp(actual.pcm, expected.pcm_sha256, 32) != 0)
            std::fprintf(stderr, "mapping frame=%u state=%d video=%d pcm=%d\n", frame,
                         std::memcmp(actual.state, expected.state_sha256, 32),
                         std::memcmp(actual.frame, expected.frame_sha256, 32),
                         std::memcmp(actual.pcm, expected.pcm_sha256, 32));
        check(std::memcmp(actual.state, expected.state_sha256, 32) == 0 &&
              std::memcmp(actual.frame, expected.frame_sha256, 32) == 0 &&
              std::memcmp(actual.pcm, expected.pcm_sha256, 32) == 0, "adapter input mapping matches direct runtime execution");
    }
}

void checkpoint_wrong_rom_preserves_observation()
{
    Fixture f, foreign;
    foreign.bytes = rom(FLYNES_RUNTIME_OTHER_ROM_FIXTURE);
    const auto hash = sha256(foreign.bytes->data(), foreign.bytes->size());
    std::memcpy(foreign.content.content_hash, hash.data(), 32);
    f.load(); foreign.load(); f.step(); foreign.step();
    const auto before = f.digest();
    const auto own = exported(f);
    auto wrong_rom = exported(foreign);
    std::copy_n(own.begin(), 88, wrong_rom.begin());
    check(f.table.import_state(f.table.context, wrong_rom.data(), wrong_rom.size()) < 0,
          "same-epoch opaque checkpoint for another ROM is rejected");
    fly_session_dual_state_digest_v2 after{};
    const auto digest_result = f.table.state_digest(f.table.context, 0, &after);
    check(digest_result == FLY_SESSION_V2_OK && std::memcmp(&before, &after, sizeof(after)) == 0,
          "wrong-ROM payload rejection preserves existing frame observation");
}

std::shared_ptr<const std::vector<std::uint8_t>> battery_rom(std::uint8_t token)
{
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(16 + 16384 + 8192, 0);
    auto& b = *bytes;
    b[0] = 'N'; b[1] = 'E'; b[2] = 'S'; b[3] = 0x1a;
    b[4] = 1; b[5] = 1; b[6] = 2;
    // Read initial SRAM into RAM[0], write token, then repeatedly sample P1 A.
    const std::uint8_t code[] = {0x78,0xd8,0xa2,0xff,0x9a,0xad,0x00,0x60,0x85,0x00,
        0xa9,token,0x8d,0x00,0x60,0xa9,1,0x8d,0x16,0x40,0xa9,0,0x8d,0x16,0x40,
        0xad,0x16,0x40,0x29,1,0x85,1,0x4c,0x0f,0x80};
    std::copy(std::begin(code), std::end(code), b.begin() + 16);
    for (unsigned v = 0x3ffa; v < 0x4000; v += 2) { b[16 + v] = 0; b[17 + v] = 0x80; }
    return bytes;
}

void select_rom(Fixture& f, std::shared_ptr<const std::vector<std::uint8_t>> bytes)
{
    f.bytes = std::move(bytes);
    const auto hash = sha256(f.bytes->data(), f.bytes->size());
    std::copy(hash.begin(), hash.end(), f.content.content_hash);
}

void equal_rounds(std::shared_ptr<const std::vector<std::uint8_t>> first,
                  std::shared_ptr<const std::vector<std::uint8_t>> target,
                  unsigned sample_rate = 48000)
{
    Fixture reused(sample_rate);
    select_rom(reused, first); reused.load();
    for (unsigned frame = 0; frame < 7; ++frame) reused.step(frame);
    select_rom(reused, target); reused.load();
    Fixture fresh(sample_rate); select_rom(fresh, target); fresh.load();
    for (unsigned frame = 0; frame < 12; ++frame) {
        reused.step(frame); fresh.step(frame);
        const auto a = reused.digest(frame), b = fresh.digest(frame);
        check(std::memcmp(a.state, b.state, 32) == 0, "fresh round full canonical state equals new runtime, including frame zero");
        std::vector<std::uint8_t> av(FLY_RUNTIME_RGB565_BYTES), bv(av.size());
        fly_latest_frame_v1 video{}; video.struct_size = sizeof(video); video.version = 1;
        check(fly_runtime_copy_latest_frame(reused.runtime.value, av.data(), av.size(), &video) == FLY_RESULT_OK &&
              fly_runtime_copy_latest_frame(fresh.runtime.value, bv.data(), bv.size(), &video) == FLY_RESULT_OK,
              "fresh round video copies");
        check(av == bv && std::memcmp(a.frame, b.frame, 32) == 0, "fresh round entire video equals new runtime");
        std::vector<std::int16_t> ap(2048), bp(2048);
        fly_pcm_block_v1 pcm{}; pcm.struct_size = sizeof(pcm); pcm.version = 1;
        check(fly_runtime_pull_pcm(reused.runtime.value, ap.data(), static_cast<std::uint32_t>(ap.size()), &pcm) == FLY_RESULT_OK,
              "fresh round reused PCM");
        ap.resize(pcm.sample_count);
        check(fly_runtime_pull_pcm(fresh.runtime.value, bp.data(), static_cast<std::uint32_t>(bp.size()), &pcm) == FLY_RESULT_OK,
              "fresh round new PCM");
        bp.resize(pcm.sample_count);
        check(ap == bp && std::memcmp(a.pcm, b.pcm, 32) == 0, "fresh round entire PCM equals new runtime");
    }
}
void fresh_same_rom() { const auto b = rom(); equal_rounds(b, b); }
void fresh_different_rom() { equal_rounds(rom(FLYNES_RUNTIME_OTHER_ROM_FIXTURE), rom()); }
void fresh_battery_isolation() { equal_rounds(battery_rom(0x5a), battery_rom(0xa5)); }
void fresh_sample_rates() { for (unsigned rate : {44100u, 96000u}) equal_rounds(rom(), rom(), rate); }

void fresh_validation_and_recovery()
{
    Fixture f; f.load(); f.step();
    check(fly_runtime_capture_rollback(f.runtime.value, 0) == FLY_RESULT_OK, "capture previous-round rollback");
    const auto before = f.digest();
    std::array<std::uint8_t, 32> bad_hash{};
    check(fly_runtime_load_rom_fresh(nullptr, f.bytes->data(), f.bytes->size(), nullptr) == FLY_RESULT_INVALID_ARGUMENT,
          "fresh null runtime rejected");
    check(fly_runtime_load_rom_fresh(f.runtime.value, nullptr, f.bytes->size(), nullptr) == FLY_RESULT_INVALID_ARGUMENT &&
          fly_runtime_load_rom_fresh(f.runtime.value, f.bytes->data(), 0, nullptr) == FLY_RESULT_INVALID_ARGUMENT &&
          fly_runtime_load_rom_fresh(f.runtime.value, f.bytes->data(), f.bytes->size(), bad_hash.data()) == FLY_RESULT_INVALID_ARGUMENT,
          "fresh invalid arguments/hash rejected");
    const auto after = f.digest();
    check(std::memcmp(&before, &after, sizeof(before)) == 0, "fresh validation preserves previous observation");
    const std::uint8_t invalid[] = {1,2,3,4};
    check(fly_runtime_load_rom_fresh(f.runtime.value, invalid, sizeof(invalid), nullptr) == FLY_RESULT_INVALID_ARGUMENT,
          "fresh accepted corrupt ROM fails");
    fly_runtime_frame_digest_v1 digest{}; digest.struct_size = sizeof(digest); digest.version = 1;
    check(fly_runtime_copy_frame_digest(f.runtime.value, 7, 0, &digest) == FLY_RESULT_INVALID_STATE,
          "failed fresh load invalidates runtime digest");
    fly_latest_frame_v1 video{}; video.struct_size = sizeof(video); video.version = 1;
    std::vector<std::uint8_t> pixels(FLY_RUNTIME_RGB565_BYTES);
    check(fly_runtime_copy_latest_frame(f.runtime.value, pixels.data(), pixels.size(), &video) == FLY_RESULT_INVALID_STATE,
          "failed fresh load invalidates published frame");
    check(fly_runtime_restore_rollback(f.runtime.value, 0) == FLY_RESULT_INVALID_STATE,
          "failed fresh load discards previous-round rollback");
    std::array<std::int16_t, 2048> silence{}; silence.fill(123);
    fly_pcm_block_v1 pcm{}; pcm.struct_size = sizeof(pcm); pcm.version = 1;
    check(fly_runtime_pull_pcm(f.runtime.value, silence.data(), static_cast<std::uint32_t>(silence.size()), &pcm) == FLY_RESULT_OK &&
          pcm.first_sample_sequence == 0 && pcm.media_time_ns == 0 &&
          std::all_of(silence.begin(), silence.end(), [](auto sample) { return sample == 0; }),
          "failed fresh load empties previous-round PCM and resets producer sequence");
    f.load(); f.step();
    check(fly_runtime_copy_latest_frame(f.runtime.value, pixels.data(), pixels.size(), &video) == FLY_RESULT_OK &&
          video.frame_sequence == 1 && video.frame_index == 0, "recovered round starts publication sequence at one");
    const auto input = f.input();
    for (unsigned port = 0; port < 4; ++port)
        check(video.applied_input_sequence[port] == input.ports[port].input_sequence,
              "recovered round publishes only current-round input sequences");
    Fixture control; control.load(); control.step();
    const auto recovered = f.digest(), expected = control.digest();
    check(std::memcmp(&recovered, &expected, sizeof(expected)) == 0, "valid load after failure restores fresh observation");
}

struct Core final {
    nes_t* value;
    Core() : value(nes_create(nullptr)) { check(value != nullptr, "core create"); }
    ~Core() { nes_destroy(value); }
    void load(const std::vector<std::uint8_t>& bytes) { check(nes_load_rom(value, bytes.data(), bytes.size(), nullptr) >= 0, "core ROM load"); }
    void step(std::uint32_t input = 0) {
        nes_set_input(value, 0, input);
        std::array<std::int16_t, 2048> pcm{};
        std::uint32_t frames = 0, count = 0;
        check(nes_run_frames(value, 1, pcm.data(), static_cast<std::uint32_t>(pcm.size()), &frames, &count) >= 0 && frames == 1, "core step");
    }
    std::uint8_t ram(unsigned index) {
        const std::uint8_t* bytes = nullptr; std::size_t size = 0;
        check(nes_get_cpu_ram(value, &bytes, &size) >= 0 && index < size, "public CPU RAM");
        return bytes[index];
    }
};
void destroy_routes_old_battery_to_old_context()
{
    Core old; old.load(*battery_rom(0x5a)); old.step();
    Core replacement;
    nes_destroy(old.value); old.value = nullptr;
    replacement.load(*battery_rom(0xa5)); replacement.step();
    check(replacement.ram(0) == 0, "destroying old core must not seed replacement SRAM");
}
void failed_candidate_preserves_peer_pad_dispatch()
{
    Core peer; peer.load(*battery_rom(0x5a)); peer.step(1);
    check(peer.ram(1) == 1, "peer consumes pressed A before candidate");
    {
        Core candidate;
        const std::uint8_t invalid[] = {1,2,3,4};
        check(nes_load_rom(candidate.value, invalid, sizeof(invalid), nullptr) < 0, "candidate rejects invalid ROM");
    }
    peer.step(0);
    check(peer.ram(1) == 0, "peer consumes released A after failed candidate destruction");
    peer.step(1);
    check(peer.ram(1) == 1, "peer consumes pressed A after failed candidate destruction");
}

void ordinary_core_preserves_battery_on_power_reset_and_reload()
{
    Core core; const auto bytes = battery_rom(0x5a);
    core.load(*bytes); core.step();
    check(core.ram(0) == 0, "ordinary initial battery is empty");
    check(nes_power(core.value, 0) >= 0 && nes_power(core.value, 1) >= 0, "ordinary power cycle");
    core.step(); check(core.ram(0) == 0x5a, "ordinary power cycle retains battery cache");
    check(nes_reset(core.value, 1) >= 0, "ordinary hard reset");
    core.step(); check(core.ram(0) == 0x5a, "ordinary hard reset retains battery");
    core.load(*bytes); core.step();
    check(core.ram(0) == 0x5a, "ordinary ROM reload retains existing battery semantics");
}

struct Callbacks {
    unsigned logs = 0, loads = 0, saves = 0;
    std::uint8_t saved = 0;
    static void log(void* pointer, const char*, std::uint32_t) { ++static_cast<Callbacks*>(pointer)->logs; }
    static int file(void* pointer, int action, int, std::uint8_t*, std::size_t, std::size_t* length,
                    const std::uint8_t* data, std::size_t size) {
        auto& self = *static_cast<Callbacks*>(pointer);
        if (action == NES_IO_LOAD_BATTERY) { ++self.loads; if (length) *length = 0; }
        if (action == NES_IO_SAVE_BATTERY) { ++self.saves; if (size) self.saved = data[0]; }
        return 0;
    }
    void attach(Core& core) {
        check(nes_set_log_callback(core.value, log, this) >= 0 &&
              nes_set_file_io_callback(core.value, file, this) >= 0, "attach callback observations");
    }
};

void peer_file_and_log_routing()
{
    Callbacks a, b;
    Core old; a.attach(old); old.load(*battery_rom(0x5a)); old.step();
    Core peer; b.attach(peer);
    nes_destroy(old.value); old.value = nullptr;
    check(a.saves == 1 && a.saved == 0x5a && b.saves == 0, "old destroy sends battery only to old callback");
    { Core candidate; }
    const auto a_logs = a.logs;
    peer.load(*battery_rom(0xa5)); peer.step(1);
    check(b.logs > 0 && a.logs == a_logs, "peer ROM load logs route to peer after latest context destroyed");
    check(b.loads == 1 && peer.ram(0) == 0, "peer FILE load callback remains installed and cache remains fresh");
    check(nes_unload(peer.value) >= 0 && b.saves == 1 && b.saved == 0xa5, "peer FILE save callback remains installed");
}

#if defined(FLYNES_TEST_WRAP_ALLOC)
void post_load_exception_retires_replacement()
{
    Fixture f; f.load(); f.step();
    Core peer; peer.load(*battery_rom(0x5a)); peer.step(1);
    fail_after_rom_load = true;
    const auto result = fly_runtime_load_rom_fresh(f.runtime.value, f.bytes->data(), f.bytes->size(), nullptr);
    fail_after_rom_load = false;
    check(result == FLY_RESULT_OUT_OF_MEMORY, "post-load exception translated at C ABI boundary");
    check(fly_runtime_load_rom(f.runtime.value, f.bytes->data(), f.bytes->size(), nullptr) == FLY_RESULT_INVALID_STATE,
          "post-load exception destroys replacement and leaves no internal core");
    peer.step(0); check(peer.ram(1) == 0, "peer releases A after loaded candidate exception cleanup");
    peer.step(1); check(peer.ram(1) == 1, "peer presses A after loaded candidate exception cleanup");
    f.load(); f.step();
    Fixture control; control.load(); control.step();
    const auto recovered = f.digest(), expected = control.digest();
    check(std::memcmp(&recovered, &expected, sizeof(expected)) == 0, "retry after post-load exception is fresh");
}

void failed_initialization_preserves_peer_and_recovers()
{
    // Context constructor, first framebuffer, and second framebuffer failures.
    for (unsigned fault = 0; fault < 3; ++fault) {
        Fixture f; f.load(); f.step();
        Core peer; peer.load(*battery_rom(0x5a)); peer.step(1);
        if (fault == 0) fail_next_new = true;
        else fail_calloc_at = fault;
        const auto result = fly_runtime_load_rom_fresh(f.runtime.value, f.bytes->data(), f.bytes->size(), nullptr);
        fail_next_new = false; fail_calloc_at = 0;
        check(result == FLY_RESULT_INTERNAL_ERROR, "fresh allocation/init failure reported without C ABI exception");
        check(fly_runtime_load_rom(f.runtime.value, f.bytes->data(), f.bytes->size(), nullptr) == FLY_RESULT_INVALID_STATE,
              "legacy loader safely rejects missing internal core");
        fly_runtime_frame_digest_v1 digest{}; digest.struct_size = sizeof(digest); digest.version = 1;
        check(fly_runtime_copy_frame_digest(f.runtime.value, 7, 0, &digest) == FLY_RESULT_INVALID_STATE,
              "failed initialization invalidates digest");
        peer.step(0); check(peer.ram(1) == 0, "peer releases A across initialization failure");
        peer.step(1); check(peer.ram(1) == 1, "peer presses A across initialization failure");
        f.load(); f.step();
        Fixture control; control.load(); control.step();
        const auto recovered = f.digest(), expected = control.digest();
        check(std::memcmp(&recovered, &expected, sizeof(expected)) == 0, "fresh retry recovers after initialization failure");
    }
}
#endif
} // namespace

int main(int argc, char** argv)
{
    unsigned failures = 0;
    unsigned executed = 0;
    const std::pair<const char*, void(*)()> tests[] = {
        {"fresh_same_rom", fresh_same_rom},
        {"fresh_different_rom", fresh_different_rom},
        {"fresh_battery_isolation", fresh_battery_isolation},
        {"fresh_sample_rates", fresh_sample_rates},
        {"fresh_validation_and_recovery", fresh_validation_and_recovery},
        {"destroy_routes_old_battery_to_old_context", destroy_routes_old_battery_to_old_context},
        {"failed_candidate_preserves_peer_pad_dispatch", failed_candidate_preserves_peer_pad_dispatch},
        {"peer_file_and_log_routing", peer_file_and_log_routing},
        {"ordinary_core_preserves_battery_on_power_reset_and_reload", ordinary_core_preserves_battery_on_power_reset_and_reload},
#if defined(FLYNES_TEST_WRAP_ALLOC)
        {"post_load_exception_retires_replacement", post_load_exception_retires_replacement},
        {"failed_initialization_preserves_peer_and_recovers", failed_initialization_preserves_peer_and_recovers},
#endif
        {"borrowed_runtime_and_pcm", borrowed_runtime_and_pcm},
        {"validation_is_nonmutating", validation_is_nonmutating},
        {"authorization_and_load_failure", authorization_and_load_failure},
        {"retained_context_outlives_wrapper", retained_context_outlives_wrapper},
        {"checkpoint_cross_instance_and_restore", checkpoint_cross_instance_and_restore},
        {"checkpoint_binding_and_failures", checkpoint_binding_and_failures},
        {"four_ports_match_direct_runtime", four_ports_match_direct_runtime},
        {"checkpoint_wrong_rom_preserves_observation", checkpoint_wrong_rom_preserves_observation},
    };
    for (const auto& test : tests) {
        if (argc == 2 && std::string(argv[1]).find(test.first) == std::string::npos) continue;
        ++executed;
        try { test.second(); std::printf("PASS %s\n", test.first); }
        catch (const std::exception& error) { ++failures; std::fprintf(stderr, "FAIL %s: %s\n", test.first, error.what()); }
    }
    if (executed == 0) { std::fputs("No matching tests\n", stderr); return 2; }
    return failures == 0 ? 0 : 1;
}
