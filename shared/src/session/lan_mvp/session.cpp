#include "flynes/flynes_nearby_mvp.h"

#include "lan_mvp/invite.hpp"
#include "lan_mvp/lockstep.hpp"
#include "lan_mvp/wire.hpp"
#include "wire/sha256.hpp"
#include "flynes_quic_provider.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using flynes::session::lan_mvp::Invite;
namespace lockstep = flynes::session::lan_mvp::lockstep;
namespace wire = flynes::session::lan_mvp::wire;

namespace {
enum class Operation : std::uint32_t {
    Material = 1, Listen = 2, Accept = 3, Connect = 4,
    OpenStream = 5, AcceptStream = 6, Read = 7, Write = 8
};
enum class Role { None, Host, Guest };
constexpr std::chrono::seconds kInviteLife{120};
constexpr std::chrono::seconds kProgressTimeout{2};
constexpr std::size_t kInputDelay = 2;
constexpr std::size_t kInputWindow = 256;
constexpr std::uint64_t kDigestInterval = 60;

std::string endpoint(const Invite& invite) {
    std::string result;
    for (std::size_t i = 0; i < invite.ipv4.size(); ++i) {
        if (i != 0) result.push_back('.');
        result += std::to_string(invite.ipv4[i]);
    }
    result.push_back(':');
    result += std::to_string(invite.port);
    return result;
}

std::uint64_t big_endian_u64(const std::uint8_t* bytes) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value = (value << 8u) | bytes[i];
    return value;
}

std::uint32_t big_endian_u32(const std::uint8_t* bytes) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value = (value << 8u) | bytes[i];
    return value;
}

void append_u64(std::vector<std::uint8_t>* bytes, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8)
        bytes->push_back(static_cast<std::uint8_t>(value >> static_cast<unsigned>(shift)));
}

void append_u32(std::vector<std::uint8_t>* bytes, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8)
        bytes->push_back(static_cast<std::uint8_t>(value >> static_cast<unsigned>(shift)));
}

bool same_bytes(const std::uint8_t* a, const std::uint8_t* b, std::size_t size) {
    std::uint8_t difference = 0;
    for (std::size_t i = 0; i < size; ++i) difference |= a[i] ^ b[i];
    return difference == 0;
}

std::array<std::uint8_t, 32> make_config_hash(
        const std::array<std::uint8_t, 32>& rom_hash,
        const fly_runtime_source_timing_v1& timing) {
    static constexpr char kCompatibility[] = "flynes-lan-mvp-nestopia-v1";
    std::vector<std::uint8_t> bytes(rom_hash.begin(), rom_hash.end());
    bytes.insert(bytes.end(), kCompatibility, kCompatibility + sizeof(kCompatibility) - 1);
    append_u32(&bytes, timing.source_region);
    append_u32(&bytes, timing.frame_rate_numerator);
    append_u32(&bytes, timing.frame_rate_denominator);
    append_u32(&bytes, timing.sample_rate);
    return flynes::session::wire::sha256(bytes.data(), bytes.size());
}
} // namespace

struct Event {
    std::uint64_t operation = 0;
    std::int32_t result = 0;
    std::uint64_t resource = 0;
    std::vector<std::uint8_t> bytes;
};

struct fly_lan_mvp_session {
    std::atomic<unsigned> references{1};
    std::mutex mutex;
    fly_lan_mvp_diagnostic_sink diagnostic_sink = nullptr;
    void* diagnostic_context = nullptr;
    const std::chrono::steady_clock::time_point created = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point previous_pump = created;
    std::uint64_t diagnostic_sequence = 0;
    std::uint64_t read_bytes = 0, written_bytes = 0;
    std::uint32_t last_logged_state = ~0u;
    FlynesQuicProvider* provider = nullptr;
    std::vector<Event> events;
    std::unordered_map<std::uint64_t, Operation> operations;
    std::uint64_t next_operation = 1;
    Role role = Role::None;
    fly_lan_mvp_snapshot view{};
    Invite invite{};
    std::string qr;
    std::chrono::steady_clock::time_point deadline{};
    std::chrono::steady_clock::time_point last_progress{};
    std::uint64_t material = 0;
    std::uint64_t listener = 0;
    std::uint64_t connection = 0;
    std::uint64_t send_stream = 0;
    std::uint64_t recv_stream = 0;
    wire::Decoder decoder;
    bool read_inflight = false;
    bool write_inflight = false;
    std::deque<std::vector<std::uint8_t>> writes;
    fly_runtime_t* runtime = nullptr;
    std::array<std::uint8_t, 32> local_rom_hash{};
    std::array<std::uint8_t, 32> remote_rom_hash{};
    std::array<std::uint8_t, 32> local_config_hash{};
    std::array<std::uint8_t, 32> remote_config_hash{};
    bool local_configured = false;
    bool peer_configured = false;
    bool local_ready = false;
    bool peer_ready = false;
    std::unique_ptr<lockstep::Buffer> inputs;
    std::uint64_t next_submit_frame = kInputDelay;
    std::uint32_t last_submitted_buttons = 0;
    std::array<std::uint32_t, 2> last_applied_buttons{};
    std::uint64_t lobby_generation = 0;
    std::uint64_t pcm_produced = 0, pcm_consumed = 0;
    std::map<std::uint64_t, std::array<std::uint8_t, 32>> local_digests;
    std::map<std::uint64_t, std::array<std::uint8_t, 32>> remote_digests;

    ~fly_lan_mvp_session() {
        if (runtime != nullptr) fly_runtime_destroy(runtime);
    }

    void trace(const char* event, const std::string& detail = {}) {
        if (!diagnostic_sink) return;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - created).count();
        const std::string line = "seq=" + std::to_string(++diagnostic_sequence) +
            " age_ms=" + std::to_string(ms) + " role=" + std::to_string(static_cast<int>(role)) +
            " event=" + event + " state=" + std::to_string(view.state) +
            " reason=" + std::to_string(view.reason) + " frame=" + std::to_string(view.completed_frames) +
            " pending=" + std::to_string(operations.size()) + " callbacks=" + std::to_string(events.size()) +
            " writes=" + std::to_string(writes.size()) +
            " tx=" + std::to_string(written_bytes) + " rx=" + std::to_string(read_bytes) + " " + detail;
        diagnostic_sink(diagnostic_context, line.c_str());
    }

    void sync_view() {
        view.role = role == Role::Host ? FLY_LAN_MVP_ROLE_HOST_P1 :
                    role == Role::Guest ? FLY_LAN_MVP_ROLE_GUEST_P2 :
                                          FLY_LAN_MVP_ROLE_NONE;
        view.local_configured = local_configured ? 1u : 0u;
        view.peer_configured = peer_configured ? 1u : 0u;
        view.local_ready = local_ready ? 1u : 0u;
        view.peer_ready = peer_ready ? 1u : 0u;
        if (local_configured)
            std::memcpy(view.config_hash, local_config_hash.data(), local_config_hash.size());
    }

    void stop_runtime() {
        inputs.reset();
        if (runtime != nullptr) {
            fly_runtime_destroy(runtime);
            runtime = nullptr;
        }
    }

    void end(std::uint32_t reason) {
        if (view.state == FLY_LAN_MVP_ENDED) return;
        view.state = FLY_LAN_MVP_ENDED;
        view.reason = reason;
        trace("end", "op=" + std::to_string(view.transport_operation) +
            " result=" + std::to_string(view.transport_result));
        qr.clear();
        writes.clear();
        write_inflight = false;
        read_inflight = false;
        stop_runtime();
        for (const auto& pending : operations)
            (void)flynes_quic_provider_cancel(provider, pending.first);
        operations.clear();
        if (connection != 0)
            (void)flynes_quic_provider_close(provider, next_operation++, connection, 0);
        if (listener != 0)
            (void)flynes_quic_provider_close(provider, next_operation++, listener, 0);
    }

    template <typename Submit>
    bool submit(Operation kind, Submit call) {
        const auto operation = next_operation++;
        operations.emplace(operation, kind);
        if (view.state != FLY_LAN_MVP_RUNNING)
            trace("submit", "id=" + std::to_string(operation) +
                " op=" + std::to_string(static_cast<unsigned>(kind)));
        const auto result = call(operation);
        if (result == FLYNES_QUIC_ACCEPTED) return true;
        operations.erase(operation);
        view.transport_result = result;
        view.transport_operation = static_cast<std::uint32_t>(kind);
        end(FLY_LAN_MVP_REASON_CONNECTION);
        return false;
    }

    bool begin_read() {
        if (read_inflight || recv_stream == 0 || view.state == FLY_LAN_MVP_ENDED) return true;
        read_inflight = true;
        if (submit(Operation::Read, [this](std::uint64_t op) {
                return flynes_quic_provider_read(provider, op, recv_stream,
                                                  wire::kMaxBodySize + 2);
            })) return true;
        read_inflight = false;
        return false;
    }

    bool begin_next_write() {
        if (write_inflight || writes.empty() || send_stream == 0 ||
            view.state == FLY_LAN_MVP_ENDED) return true;
        write_inflight = true;
        const auto& bytes = writes.front();
        if (submit(Operation::Write, [this, &bytes](std::uint64_t op) {
                return flynes_quic_provider_write(provider, op, send_stream,
                                                   bytes.data(), bytes.size(), 0);
            })) return true;
        write_inflight = false;
        return false;
    }

    bool queue_message(wire::Kind kind, const std::vector<std::uint8_t>& payload) {
        std::vector<std::uint8_t> bytes;
        if (!wire::encode(kind, payload, &bytes)) {
            end(FLY_LAN_MVP_REASON_REJECTED);
            return false;
        }
        writes.push_back(std::move(bytes));
        if (kind != wire::Kind::Input && kind != wire::Kind::Digest)
            trace("message_tx", "kind=" + std::to_string(static_cast<unsigned>(kind)) +
                " size=" + std::to_string(payload.size()));
        return begin_next_write();
    }

    bool configs_match() const {
        return local_configured && peer_configured &&
               same_bytes(local_rom_hash.data(), remote_rom_hash.data(), local_rom_hash.size()) &&
               same_bytes(local_config_hash.data(), remote_config_hash.data(),
                          local_config_hash.size());
    }

    void compare_digest(std::uint64_t frame) {
        const auto local = local_digests.find(frame);
        const auto remote = remote_digests.find(frame);
        if (local == local_digests.end() || remote == remote_digests.end()) return;
        if (!same_bytes(local->second.data(), remote->second.data(), local->second.size())) {
            end(FLY_LAN_MVP_REASON_DESYNC);
            return;
        }
        local_digests.erase(local);
        remote_digests.erase(remote);
    }

    void publish_digest(std::uint64_t frame) {
        if ((frame + 1) % 300 == 0) trace("progress");
        fly_runtime_frame_digest_v1 digest{};
        digest.struct_size = FLY_RUNTIME_FRAME_DIGEST_V1_SIZE;
        digest.version = FLY_RUNTIME_FRAME_DIGEST_VERSION_1;
        if (fly_runtime_copy_frame_digest(runtime, 1, frame, &digest) != FLY_RESULT_OK) {
            end(FLY_LAN_MVP_REASON_CONFIG_MISMATCH);
            return;
        }
        std::array<std::uint8_t, 32> state{};
        std::memcpy(state.data(), digest.state_sha256, state.size());
        local_digests[frame] = state;
        view.last_digest_frame = frame;
        std::memcpy(view.last_state_digest, state.data(), state.size());
        std::vector<std::uint8_t> payload;
        payload.reserve(40);
        append_u64(&payload, frame);
        payload.insert(payload.end(), state.begin(), state.end());
        (void)queue_message(wire::Kind::Digest, payload);
        compare_digest(frame);
    }

    void advance() {
        if (view.state != FLY_LAN_MVP_RUNNING || view.paused || inputs == nullptr || runtime == nullptr) return;
        lockstep::Frame frame{};
        while (view.state == FLY_LAN_MVP_RUNNING && inputs->pop_ready(&frame)) {
            fly_frame_input_v1 input{};
            input.struct_size = FLY_FRAME_INPUT_V1_SIZE;
            input.version = FLY_FRAME_INPUT_VERSION_1;
            input.timeline_epoch = 1;
            input.frame_index = frame.index;
            if (role == Role::Host) {
                input.buttons[0] = frame.local_mask;
                input.buttons[1] = frame.remote_mask;
            } else {
                input.buttons[0] = frame.remote_mask;
                input.buttons[1] = frame.local_mask;
            }
            input.input_sequence[0] = frame.index + 1;
            input.input_sequence[1] = frame.index + 1;
            input.batch_sequence = frame.index + 1;
            if (input.buttons[0] != last_applied_buttons[0] ||
                input.buttons[1] != last_applied_buttons[1]) {
                trace("input_apply", "input_frame=" + std::to_string(frame.index) +
                    " p1=" + std::to_string(input.buttons[0]) +
                    " p2=" + std::to_string(input.buttons[1]));
                last_applied_buttons = {input.buttons[0], input.buttons[1]};
            }
            fly_frame_result_v1 result{};
            result.struct_size = FLY_FRAME_RESULT_V1_SIZE;
            result.version = FLY_FRAME_RESULT_VERSION_1;
            if (fly_runtime_step_frame(runtime, &input, &result) != FLY_RESULT_OK) {
                end(FLY_LAN_MVP_REASON_CONFIG_MISMATCH);
                return;
            }
            view.completed_frames = frame.index + 1;
            if (result.pcm_published) pcm_produced = result.audio_last_sample_sequence + 1;
            view.applied_buttons[0] = input.buttons[0];
            view.applied_buttons[1] = input.buttons[1];
            last_progress = std::chrono::steady_clock::now();
            if (view.completed_frames % kDigestInterval == 0)
                publish_digest(frame.index);
        }
    }

    void enter_running() {
        if (view.state == FLY_LAN_MVP_RUNNING || runtime == nullptr || !configs_match() ||
            !local_ready || !peer_ready) return;
        view.state = FLY_LAN_MVP_RUNNING;
        view.reason = FLY_LAN_MVP_REASON_NONE;
        view.completed_frames = 0;
        pcm_produced = pcm_consumed = 0;
        view.last_digest_frame = 0;
        std::memset(view.last_state_digest, 0, sizeof(view.last_state_digest));
        inputs = std::make_unique<lockstep::Buffer>(kInputDelay, kInputWindow);
        next_submit_frame = kInputDelay;
        local_digests.clear();
        remote_digests.clear();
        for (std::uint64_t frame = 0; frame < kInputDelay; ++frame) {
            (void)inputs->put_local(frame, 0);
            (void)inputs->put_remote(frame, 0);
        }
        last_progress = std::chrono::steady_clock::now();
        advance();
    }

    void maybe_start_host() {
        if (role != Role::Host || view.state == FLY_LAN_MVP_RUNNING ||
            !local_ready || !peer_ready || !configs_match()) return;
        const std::vector<std::uint8_t> payload(local_config_hash.begin(),
                                                local_config_hash.end());
        if (queue_message(wire::Kind::Start, payload)) enter_running();
    }

    void clear_game() {
        stop_runtime();
        local_configured = peer_configured = local_ready = peer_ready = false;
        local_rom_hash = {}; remote_rom_hash = {};
        local_config_hash = {}; remote_config_hash = {};
        local_digests.clear(); remote_digests.clear();
        view.completed_frames = view.last_digest_frame = 0;
        pcm_produced = pcm_consumed = 0;
        view.paused = 0;
        std::memset(view.peer_game_key, 0, sizeof(view.peer_game_key));
        view.applied_buttons[0] = view.applied_buttons[1] = 0;
        std::memset(view.config_hash, 0, sizeof(view.config_hash));
        std::memset(view.last_state_digest, 0, sizeof(view.last_state_digest));
        view.reason = FLY_LAN_MVP_REASON_NONE;
        last_submitted_buttons = 0; last_applied_buttons = {};
    }

    bool return_lobby() {
        if (view.state == FLY_LAN_MVP_RETURNING) return true;
        if (view.state != FLY_LAN_MVP_RUNNING && view.state != FLY_LAN_MVP_LOBBY &&
            view.state != FLY_LAN_MVP_CONFIGURING) return false;
        if (role == Role::Guest) {
            view.state = FLY_LAN_MVP_RETURNING;
            return queue_message(wire::Kind::LobbyRequest, {});
        }
        clear_game();
        view.state = FLY_LAN_MVP_RETURNING;
        std::vector<std::uint8_t> payload;
        append_u64(&payload, ++lobby_generation);
        return queue_message(wire::Kind::Lobby, payload);
    }

    void handle_message(const wire::Message& message) {
        if (message.kind != wire::Kind::Input && message.kind != wire::Kind::Digest)
            trace("message_rx", "kind=" + std::to_string(static_cast<unsigned>(message.kind)) +
                " size=" + std::to_string(message.payload.size()));
        if (message.kind == wire::Kind::LobbyRequest && role == Role::Host && message.payload.empty()) {
            (void)return_lobby();
            return;
        }
        if (message.kind == wire::Kind::Lobby && role == Role::Guest && message.payload.size() == 8) {
            clear_game();
            lobby_generation = big_endian_u64(message.payload.data());
            view.state = FLY_LAN_MVP_LOBBY;
            (void)queue_message(wire::Kind::LobbyAck, message.payload);
            return;
        }
        if (message.kind == wire::Kind::LobbyAck && role == Role::Host &&
            view.state == FLY_LAN_MVP_RETURNING && message.payload.size() == 8 &&
            big_endian_u64(message.payload.data()) == lobby_generation) {
            view.state = FLY_LAN_MVP_LOBBY;
            return;
        }
        // The bidirectional stream barrier drains the previous game's messages
        // before either side can send a new config or restart frame numbering.
        if (view.state == FLY_LAN_MVP_RETURNING) return;
        if (message.kind == wire::Kind::Pause && message.payload.size() == 1) {
            if (view.state != FLY_LAN_MVP_RUNNING) return;
            view.paused = message.payload[0] != 0;
            last_progress = std::chrono::steady_clock::now();
            if (role == Role::Host) (void)queue_message(wire::Kind::Pause, message.payload);
            if (!view.paused) advance();
            return;
        }
        if (message.kind == wire::Kind::Join) {
            if (role != Role::Host || view.state != FLY_LAN_MVP_INVITING ||
                message.payload.size() != invite.token.size() ||
                std::chrono::steady_clock::now() >= deadline ||
                !same_bytes(invite.token.data(), message.payload.data(), invite.token.size())) {
                end(std::chrono::steady_clock::now() >= deadline
                        ? FLY_LAN_MVP_REASON_EXPIRED : FLY_LAN_MVP_REASON_REJECTED);
                return;
            }
            std::array<std::uint8_t, 48> seed{};
            std::memcpy(seed.data(), invite.token.data(), 16);
            std::memcpy(seed.data() + 16, invite.spki_pin.data(), 32);
            const auto hash = flynes::session::wire::sha256(seed.data(), seed.size());
            std::memcpy(view.session_id, hash.data(), 16);
            qr.clear();
            const std::vector<std::uint8_t> payload(view.session_id, view.session_id + 16);
            if (queue_message(wire::Kind::Accept, payload)) view.state = FLY_LAN_MVP_LOBBY;
            return;
        }
        if (message.kind == wire::Kind::Accept) {
            if (role != Role::Guest || view.state != FLY_LAN_MVP_JOINING ||
                message.payload.size() != 16) {
                end(FLY_LAN_MVP_REASON_REJECTED);
                return;
            }
            std::memcpy(view.session_id, message.payload.data(), 16);
            view.state = FLY_LAN_MVP_LOBBY;
            return;
        }
        if (message.kind == wire::Kind::Config) {
            if ((view.state != FLY_LAN_MVP_LOBBY &&
                 view.state != FLY_LAN_MVP_CONFIGURING) || message.payload.size() < 64 ||
                 message.payload.size() >= 64 + sizeof(view.peer_game_key)) {
                end(FLY_LAN_MVP_REASON_REJECTED);
                return;
            }
            const bool changed = peer_configured &&
                (!same_bytes(remote_rom_hash.data(), message.payload.data(), 32) ||
                 !same_bytes(remote_config_hash.data(), message.payload.data() + 32, 32));
            std::memcpy(remote_rom_hash.data(), message.payload.data(), 32);
            std::memcpy(remote_config_hash.data(), message.payload.data() + 32, 32);
            std::memset(view.peer_game_key, 0, sizeof(view.peer_game_key));
            if (message.payload.size() > 64)
                std::memcpy(view.peer_game_key, message.payload.data() + 64, message.payload.size() - 64);
            peer_configured = true;
            if (changed) {
                local_ready = false;
                peer_ready = false;
            }
            view.state = FLY_LAN_MVP_CONFIGURING;
            if (local_configured &&
                !same_bytes(local_rom_hash.data(), remote_rom_hash.data(), 32)) {
                view.reason = FLY_LAN_MVP_REASON_ROM_MISMATCH;
                local_ready = peer_ready = false;
                return;
            }
            if (local_configured &&
                !same_bytes(local_config_hash.data(), remote_config_hash.data(), 32)) {
                view.reason = FLY_LAN_MVP_REASON_CONFIG_MISMATCH;
                local_ready = peer_ready = false;
                return;
            }
            maybe_start_host();
            return;
        }
        if (message.kind == wire::Kind::Ready) {
            if ((view.state != FLY_LAN_MVP_CONFIGURING &&
                 view.state != FLY_LAN_MVP_LOBBY) || message.payload.size() != 32 ||
                !peer_configured ||
                !same_bytes(remote_config_hash.data(), message.payload.data(), 32)) {
                end(FLY_LAN_MVP_REASON_CONFIG_MISMATCH);
                return;
            }
            peer_ready = true;
            maybe_start_host();
            return;
        }
        if (message.kind == wire::Kind::Start) {
            if (role != Role::Guest || view.state != FLY_LAN_MVP_CONFIGURING ||
                message.payload.size() != 32 || !local_ready || !peer_ready ||
                !same_bytes(local_config_hash.data(), message.payload.data(), 32) ||
                !configs_match()) {
                end(FLY_LAN_MVP_REASON_CONFIG_MISMATCH);
                return;
            }
            enter_running();
            return;
        }
        if (message.kind == wire::Kind::Input) {
            if (view.state != FLY_LAN_MVP_RUNNING || message.payload.size() != 12 ||
                inputs == nullptr) {
                end(FLY_LAN_MVP_REASON_REJECTED);
                return;
            }
            const auto result = inputs->put_remote(big_endian_u64(message.payload.data()),
                                                   big_endian_u32(message.payload.data() + 8));
            if (result == lockstep::Put::Conflict || result == lockstep::Put::OutOfWindow) {
                end(FLY_LAN_MVP_REASON_REJECTED);
                return;
            }
            advance();
            return;
        }
        if (message.kind == wire::Kind::Digest) {
            if (view.state != FLY_LAN_MVP_RUNNING || message.payload.size() != 40) {
                end(FLY_LAN_MVP_REASON_REJECTED);
                return;
            }
            std::array<std::uint8_t, 32> digest{};
            std::memcpy(digest.data(), message.payload.data() + 8, digest.size());
            const auto frame = big_endian_u64(message.payload.data());
            remote_digests[frame] = digest;
            compare_digest(frame);
            return;
        }
        if (message.kind == wire::Kind::End) {
            end(FLY_LAN_MVP_REASON_PEER_ENDED);
            return;
        }
        end(FLY_LAN_MVP_REASON_REJECTED);
    }

    void consume(Event event) {
        const auto found = operations.find(event.operation);
        if (found == operations.end() || view.state == FLY_LAN_MVP_ENDED) return;
        const auto kind = found->second;
        operations.erase(found);
        if (view.state != FLY_LAN_MVP_RUNNING || event.result != FLYNES_QUIC_OK)
            trace("complete", "id=" + std::to_string(event.operation) +
                " op=" + std::to_string(static_cast<unsigned>(kind)) +
                " result=" + std::to_string(event.result) + " size=" + std::to_string(event.bytes.size()));
        if (event.result != FLYNES_QUIC_OK) {
            // Only emit known provider categories; never arbitrary peer-controlled text.
            const std::string category(event.bytes.begin(), event.bytes.end());
            for (const char* safe : {"timeout", "closed", "tls", "reset", "io", "transport"})
                if (category == safe) trace("transport_error", "category=" + category);
            view.transport_result = event.result;
            view.transport_operation = static_cast<std::uint32_t>(kind);
            end(FLY_LAN_MVP_REASON_CONNECTION);
            return;
        }
        switch (kind) {
        case Operation::Material: {
            if (event.resource == 0 || event.bytes.empty()) {
                end(FLY_LAN_MVP_REASON_CONNECTION); return;
            }
            material = event.resource;
            invite.spki_pin = flynes::session::wire::sha256(event.bytes.data(), event.bytes.size());
            const auto address = endpoint(invite);
            (void)submit(Operation::Listen, [this, &address](std::uint64_t op) {
                return flynes_quic_provider_listen(provider, op,
                    reinterpret_cast<const std::uint8_t*>(address.data()), address.size(),
                    material, 30000);
            });
            break;
        }
        case Operation::Listen: {
            listener = event.resource;
            if (listener == 0 || event.bytes.empty()) {
                end(FLY_LAN_MVP_REASON_CONNECTION); return;
            }
            const std::string address(event.bytes.begin(), event.bytes.end());
            const auto colon = address.rfind(':');
            if (colon == std::string::npos) { end(FLY_LAN_MVP_REASON_CONNECTION); return; }
            unsigned port = 0;
            for (std::size_t i = colon + 1; i < address.size(); ++i) {
                if (address[i] < '0' || address[i] > '9') {
                    end(FLY_LAN_MVP_REASON_CONNECTION); return;
                }
                port = port * 10u + static_cast<unsigned>(address[i] - '0');
                if (port > 65535) { end(FLY_LAN_MVP_REASON_CONNECTION); return; }
            }
            if (port == 0) { end(FLY_LAN_MVP_REASON_CONNECTION); return; }
            invite.port = static_cast<std::uint16_t>(port);
            trace("listen_ready", "endpoint=" + endpoint(invite));
            if (!flynes::session::lan_mvp::format_invite(invite, &qr)) {
                end(FLY_LAN_MVP_REASON_CONNECTION); return;
            }
            deadline = std::chrono::steady_clock::now() + kInviteLife;
            (void)submit(Operation::Accept, [this](std::uint64_t op) {
                return flynes_quic_provider_accept(provider, op, listener);
            });
            break;
        }
        case Operation::Accept:
            connection = event.resource;
            if (connection == 0) { end(FLY_LAN_MVP_REASON_CONNECTION); return; }
            (void)submit(Operation::AcceptStream, [this](std::uint64_t op) {
                return flynes_quic_provider_accept_bidi(provider, op, connection);
            });
            break;
        case Operation::Connect:
            connection = event.resource;
            if (connection == 0) { end(FLY_LAN_MVP_REASON_CONNECTION); return; }
            (void)submit(Operation::OpenStream, [this](std::uint64_t op) {
                return flynes_quic_provider_open_bidi(provider, op, connection);
            });
            break;
        case Operation::OpenStream:
        case Operation::AcceptStream:
            if (event.resource == 0 || event.bytes.size() != 8) {
                end(FLY_LAN_MVP_REASON_CONNECTION); return;
            }
            send_stream = event.resource;
            recv_stream = big_endian_u64(event.bytes.data());
            if (role == Role::Guest) {
                const std::vector<std::uint8_t> payload(invite.token.begin(), invite.token.end());
                (void)queue_message(wire::Kind::Join, payload);
            }
            (void)begin_read();
            (void)begin_next_write();
            break;
        case Operation::Write:
            if (!writes.empty()) written_bytes += writes.front().size();
            write_inflight = false;
            if (!writes.empty()) writes.pop_front();
            (void)begin_next_write();
            break;
        case Operation::Read: {
            read_bytes += event.bytes.size();
            read_inflight = false;
            if (event.bytes.empty() || !decoder.push(event.bytes.data(), event.bytes.size())) {
                end(event.bytes.empty() ? FLY_LAN_MVP_REASON_CONNECTION
                                        : FLY_LAN_MVP_REASON_REJECTED);
                return;
            }
            wire::Message message{};
            while (view.state != FLY_LAN_MVP_ENDED && decoder.pop(&message))
                handle_message(message);
            if (!decoder.failed()) (void)begin_read();
            break;
        }
        }
    }

    void pump() {
        const auto now = std::chrono::steady_clock::now();
        const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(now - previous_pump).count();
        if (gap > 1000 && view.state != FLY_LAN_MVP_ENDED)
            trace("pump_gap", "gap_ms=" + std::to_string(gap));
        previous_pump = now;
        std::vector<Event> work;
        work.swap(events);
        for (auto& event : work) consume(std::move(event));
        if (role == Role::Host && view.state == FLY_LAN_MVP_INVITING && !qr.empty() &&
            std::chrono::steady_clock::now() >= deadline)
            end(FLY_LAN_MVP_REASON_EXPIRED);
        if (view.state == FLY_LAN_MVP_RUNNING && !view.paused &&
            std::chrono::steady_clock::now() - last_progress >= kProgressTimeout)
            end(FLY_LAN_MVP_REASON_STALL);
        sync_view();
        if (last_logged_state != view.state) {
            last_logged_state = view.state;
            trace("state");
        }
    }
};

namespace {
void retain(void* context) {
    auto* session = static_cast<fly_lan_mvp_session*>(context);
    session->references.fetch_add(1, std::memory_order_relaxed);
}
void release(void* context) {
    auto* session = static_cast<fly_lan_mvp_session*>(context);
    if (session->references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete session;
}
void completion(void* context, std::uint64_t operation, std::int32_t result,
                std::uint32_t, std::uint64_t resource, const std::uint8_t* bytes,
                std::size_t size) {
    auto* session = static_cast<fly_lan_mvp_session*>(context);
    Event event{};
    event.operation = operation;
    event.result = result;
    event.resource = resource;
    if (bytes != nullptr && size != 0) event.bytes.assign(bytes, bytes + size);
    std::lock_guard<std::mutex> lock(session->mutex);
    session->events.push_back(std::move(event));
    if (session->view.state != FLY_LAN_MVP_RUNNING || result != FLYNES_QUIC_OK)
        session->trace("callback", "id=" + std::to_string(operation) + " result=" + std::to_string(result));
}
} // namespace

extern "C" fly_lan_mvp_session* fly_lan_mvp_create(void) {
    auto* session = new (std::nothrow) fly_lan_mvp_session;
    if (session == nullptr) return nullptr;
    FlynesQuicCallbacks callbacks{};
    callbacks.struct_size = sizeof(callbacks);
    callbacks.abi_version = FLYNES_QUIC_PROVIDER_ABI_V1;
    callbacks.context = session;
    callbacks.retain = retain;
    callbacks.release = release;
    callbacks.completion = completion;
    session->provider = flynes_quic_provider_create(&callbacks);
    if (session->provider == nullptr) { release(session); return nullptr; }
    return session;
}

extern "C" void fly_lan_mvp_set_diagnostic_sink(fly_lan_mvp_session* session,
        fly_lan_mvp_diagnostic_sink sink, void* context) {
    if (!session) return;
    std::lock_guard<std::mutex> lock(session->mutex);
    session->diagnostic_sink = sink;
    session->diagnostic_context = context;
}

extern "C" int fly_lan_mvp_host(fly_lan_mvp_session* session, const char* host_ipv4,
                                  const std::uint8_t* token16) {
    if (session == nullptr || host_ipv4 == nullptr || token16 == nullptr) return 0;
    std::array<std::uint8_t, 4> ip{};
    if (!flynes::session::lan_mvp::parse_lan_ipv4(host_ipv4, &ip)) return 0;
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->view.state != FLY_LAN_MVP_IDLE) return 0;
    session->role = Role::Host;
    session->invite.ipv4 = ip;
    session->invite.port = 0;
    std::memcpy(session->invite.token.data(), token16, 16);
    session->view.state = FLY_LAN_MVP_INVITING;
    session->sync_view();
    session->trace("host", std::string("bind=") + host_ipv4);
    return session->submit(Operation::Material, [session](std::uint64_t op) {
        return flynes_quic_provider_generate_self_signed(session->provider, op);
    }) ? 1 : 0;
}

extern "C" int fly_lan_mvp_join(fly_lan_mvp_session* session, const char* local_ipv4,
                                  const char* qr_text, std::size_t qr_size) {
    if (session == nullptr || local_ipv4 == nullptr || qr_text == nullptr) return 0;
    Invite invite{};
    if (!flynes::session::lan_mvp::parse_invite(std::string_view(qr_text, qr_size), &invite))
        return 0;
    std::array<std::uint8_t, 4> local_ip{};
    if (!flynes::session::lan_mvp::parse_lan_ipv4(local_ipv4, &local_ip)) return 0;
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->view.state != FLY_LAN_MVP_IDLE) return 0;
    session->role = Role::Guest;
    session->invite = invite;
    session->view.state = FLY_LAN_MVP_JOINING;
    session->sync_view();
    const auto address = endpoint(invite);
    Invite local{};
    local.ipv4 = local_ip;
    const auto bind = endpoint(local);
    session->trace("join", "bind=" + bind + " peer=" + address);
    return session->submit(Operation::Connect, [session, &address, &bind](std::uint64_t op) {
        return flynes_quic_provider_connect(session->provider, op,
            reinterpret_cast<const std::uint8_t*>(bind.data()), bind.size(),
            reinterpret_cast<const std::uint8_t*>(address.data()), address.size(),
            session->invite.spki_pin.data(), session->invite.spki_pin.size(), 10000);
    }) ? 1 : 0;
}

extern "C" std::size_t fly_lan_mvp_copy_invite(fly_lan_mvp_session* session, char* out,
                                                  std::size_t capacity) {
    if (session == nullptr) return 0;
    std::lock_guard<std::mutex> lock(session->mutex);
    session->pump();
    if (session->qr.empty() || session->view.state != FLY_LAN_MVP_INVITING) return 0;
    const auto needed = session->qr.size() + 1;
    if (out != nullptr && capacity >= needed) std::memcpy(out, session->qr.c_str(), needed);
    return needed;
}

extern "C" int fly_lan_mvp_snapshot_read(fly_lan_mvp_session* session,
                                            fly_lan_mvp_snapshot* out) {
    if (session == nullptr || out == nullptr) return 0;
    std::lock_guard<std::mutex> lock(session->mutex);
    session->pump();
    *out = session->view;
    return 1;
}

extern "C" int fly_lan_mvp_select_rom(fly_lan_mvp_session* session,
                                         const std::uint8_t* bytes, std::size_t size) {
    return fly_lan_mvp_select_game(session, bytes, size, "");
}

extern "C" int fly_lan_mvp_select_game(fly_lan_mvp_session* session,
    const std::uint8_t* bytes, std::size_t size, const char* game_key) {
    if (session == nullptr || bytes == nullptr || size == 0) return 0;
    if (!game_key || std::strlen(game_key) >= sizeof(session->view.peer_game_key)) return 0;
    std::lock_guard<std::mutex> lock(session->mutex);
    session->pump();
    if (session->view.state != FLY_LAN_MVP_LOBBY &&
        session->view.state != FLY_LAN_MVP_CONFIGURING) return 0;
    fly_runtime_config config{};
    config.struct_size = FLY_RUNTIME_CONFIG_V1_SIZE;
    config.version = FLY_RUNTIME_CONFIG_VERSION_1;
    fly_runtime_t* candidate = nullptr;
    if (fly_runtime_create(&config, &candidate) != FLY_RESULT_OK || candidate == nullptr)
        return 0;
    const auto rom_hash = flynes::session::wire::sha256(bytes, size);
    if (fly_runtime_load_rom_fresh(candidate, bytes, size, rom_hash.data()) != FLY_RESULT_OK) {
        fly_runtime_destroy(candidate);
        session->view.reason = FLY_LAN_MVP_REASON_ROM_INVALID;
        return 0;
    }
    fly_runtime_source_timing_v1 timing{};
    timing.struct_size = FLY_RUNTIME_SOURCE_TIMING_V1_SIZE;
    timing.version = FLY_RUNTIME_SOURCE_TIMING_VERSION_1;
    if (fly_runtime_get_source_timing(candidate, &timing) != FLY_RESULT_OK) {
        fly_runtime_destroy(candidate);
        session->view.reason = FLY_LAN_MVP_REASON_ROM_INVALID;
        return 0;
    }
    const auto config_hash = make_config_hash(rom_hash, timing);
    if (session->peer_configured &&
        !same_bytes(rom_hash.data(), session->remote_rom_hash.data(), rom_hash.size())) {
        fly_runtime_destroy(candidate);
        session->view.reason = FLY_LAN_MVP_REASON_ROM_MISMATCH;
        return 0;
    }
    if (session->peer_configured &&
        !same_bytes(config_hash.data(), session->remote_config_hash.data(), config_hash.size())) {
        fly_runtime_destroy(candidate);
        session->view.reason = FLY_LAN_MVP_REASON_CONFIG_MISMATCH;
        return 0;
    }
    if (session->runtime != nullptr) fly_runtime_destroy(session->runtime);
    session->runtime = candidate;
    session->local_rom_hash = rom_hash;
    session->local_config_hash = config_hash;
    session->local_configured = true;
    session->local_ready = false;
    session->peer_ready = false;
    session->view.reason = FLY_LAN_MVP_REASON_NONE;
    session->view.state = FLY_LAN_MVP_CONFIGURING;
    std::vector<std::uint8_t> payload(rom_hash.begin(), rom_hash.end());
    payload.insert(payload.end(), config_hash.begin(), config_hash.end());
    payload.insert(payload.end(), game_key, game_key + std::strlen(game_key));
    session->sync_view();
    return session->queue_message(wire::Kind::Config, payload) ? 1 : 0;
}

extern "C" int fly_lan_mvp_confirm(fly_lan_mvp_session* session) {
    if (session == nullptr) return 0;
    std::lock_guard<std::mutex> lock(session->mutex);
    session->pump();
    if (session->view.state != FLY_LAN_MVP_CONFIGURING ||
        !session->local_configured || session->runtime == nullptr) return 0;
    if (session->local_ready) return 1;
    session->local_ready = true;
    const std::vector<std::uint8_t> payload(session->local_config_hash.begin(),
                                            session->local_config_hash.end());
    const bool sent = session->queue_message(wire::Kind::Ready, payload);
    session->maybe_start_host();
    session->sync_view();
    return sent ? 1 : 0;
}

extern "C" int fly_lan_mvp_submit_input(fly_lan_mvp_session* session,
                                          std::uint32_t buttons) {
    if (session == nullptr) return 0;
    std::lock_guard<std::mutex> lock(session->mutex);
    session->pump();
    if (session->view.state != FLY_LAN_MVP_RUNNING || session->view.paused || session->inputs == nullptr) return 0;
    const auto frame = session->next_submit_frame;
    // Network reordering capacity is not input latency. A fast producer must
    // wait at the agreed delay instead of filling the entire 256-frame window.
    if (frame >= session->view.completed_frames + kInputDelay) return 0;
    if (session->inputs->put_local(frame, buttons) != lockstep::Put::Accepted) return 0;
    std::vector<std::uint8_t> payload;
    payload.reserve(12);
    append_u64(&payload, frame);
    append_u32(&payload, buttons);
    if (!session->queue_message(wire::Kind::Input, payload)) return 0;
    ++session->next_submit_frame;
    if (buttons != session->last_submitted_buttons) {
        session->trace("input_submit", "input_frame=" + std::to_string(frame) +
            " buttons=" + std::to_string(buttons));
        session->last_submitted_buttons = buttons;
    }
    session->advance();
    return 1;
}

extern "C" int fly_lan_mvp_set_paused(fly_lan_mvp_session* session, int paused) {
    if (!session) return 0;
    std::lock_guard<std::mutex> lock(session->mutex);
    session->pump();
    if (session->view.state != FLY_LAN_MVP_RUNNING) return 0;
    if (session->role == Role::Host) {
        session->view.paused = paused != 0;
        session->last_progress = std::chrono::steady_clock::now();
        if (!paused) session->advance();
    }
    return session->queue_message(wire::Kind::Pause,
        {static_cast<std::uint8_t>(paused != 0)}) ? 1 : 0;
}
extern "C" int fly_lan_mvp_return_lobby(fly_lan_mvp_session* session) {
    if (!session) return 0;
    std::lock_guard<std::mutex> lock(session->mutex);
    session->pump();
    return session->return_lobby() ? 1 : 0;
}

extern "C" int fly_lan_mvp_copy_latest_frame(fly_lan_mvp_session* session,
                                                void* rgb565_out, std::size_t capacity,
                                                fly_latest_frame_v1* meta_out) {
    if (session == nullptr || rgb565_out == nullptr || meta_out == nullptr) return 0;
    std::lock_guard<std::mutex> lock(session->mutex);
    session->pump();
    return session->runtime != nullptr &&
           fly_runtime_copy_latest_frame(session->runtime, rgb565_out, capacity, meta_out) ==
               FLY_RESULT_OK ? 1 : 0;
}

extern "C" int fly_lan_mvp_pull_pcm(fly_lan_mvp_session* session,
                                      std::int16_t* samples_out,
                                      std::uint32_t sample_capacity,
                                      fly_pcm_block_v1* block_out) {
    if (session == nullptr || samples_out == nullptr || block_out == nullptr || sample_capacity == 0 ||
        block_out->struct_size < FLY_PCM_BLOCK_V1_SIZE || block_out->version != FLY_PCM_BLOCK_VERSION_1) return 0;
    std::lock_guard<std::mutex> lock(session->mutex);
    session->pump();
    if (session->runtime == nullptr) return 0;
    // The runtime's device-consumer API pads an empty read with silence. A polled
    // game loop must enqueue only newly produced samples, otherwise every wait
    // for a peer adds an entire silent block and grows playback latency.
    const auto available = session->pcm_produced - session->pcm_consumed;
    if (available == 0) {
        block_out->sample_count = 0;
        block_out->first_sample_sequence = session->pcm_consumed;
        block_out->media_time_ns = 0;
        return 1;
    }
    const auto capacity = static_cast<std::uint32_t>(std::min<std::uint64_t>(available, sample_capacity));
    if (fly_runtime_pull_pcm(session->runtime, samples_out, capacity, block_out) != FLY_RESULT_OK) return 0;
    session->pcm_consumed = block_out->first_sample_sequence + block_out->sample_count;
    return 1;
}

extern "C" void fly_lan_mvp_cancel(fly_lan_mvp_session* session) {
    if (session == nullptr) return;
    std::lock_guard<std::mutex> lock(session->mutex);
    session->end(FLY_LAN_MVP_REASON_CANCELLED);
}

extern "C" void fly_lan_mvp_destroy(fly_lan_mvp_session* session) {
    if (session == nullptr) return;
    fly_lan_mvp_cancel(session);
    flynes_quic_provider_release(session->provider);
    release(session);
}
