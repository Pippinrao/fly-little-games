#include "flynes/flynes_nearby_mvp.h"
#include "flynes/flynes_runtime.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

namespace {
int failures = 0;
void check(bool good, const char* label) {
    if (!good) { std::cerr << "FAIL: " << label << '\n'; ++failures; }
}

std::string local_ipv4() {
    const char* specified = std::getenv("FLYNES_TEST_LAN_IPV4");
    if (specified != nullptr && *specified != '\0') return specified;
#ifdef _WIN32
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return {};
#endif
    char hostname[256]{};
    if (gethostname(hostname, sizeof(hostname)) != 0) return {};
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(hostname, nullptr, &hints, &addresses) != 0) return {};
    std::string chosen;
    for (auto* item = addresses; item != nullptr; item = item->ai_next) {
        const auto* address = reinterpret_cast<const sockaddr_in*>(item->ai_addr);
        const auto* bytes = reinterpret_cast<const uint8_t*>(&address->sin_addr);
        if (bytes[0] != 10 && bytes[0] != 192 &&
            !(bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31)) continue;
        char text[INET_ADDRSTRLEN]{};
        if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)) != nullptr) {
            chosen = text;
            break;
        }
    }
    freeaddrinfo(addresses);
    return chosen;
}

bool wait_state(fly_lan_mvp_session* session, uint32_t target, int milliseconds) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(milliseconds);
    fly_lan_mvp_snapshot last{};
    do {
        fly_lan_mvp_snapshot snapshot{};
        if (!fly_lan_mvp_snapshot_read(session, &snapshot)) return false;
        last = snapshot;
        if (snapshot.state == target) return true;
        if (snapshot.state == FLY_LAN_MVP_ENDED) {
            if (target != FLY_LAN_MVP_ENDED)
                std::cerr << "ended before target=" << target
                          << " reason=" << snapshot.reason << '\n';
            return target == FLY_LAN_MVP_ENDED;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    std::cerr << "wait_state target=" << target << " actual=" << last.state
              << " reason=" << last.reason << '\n';
    return false;
}

bool wait_pair(fly_lan_mvp_session* host, fly_lan_mvp_session* guest,
               int milliseconds) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(milliseconds);
    fly_lan_mvp_snapshot a{}, b{};
    do {
        (void)fly_lan_mvp_snapshot_read(host, &a);
        (void)fly_lan_mvp_snapshot_read(guest, &b);
        if (a.state == FLY_LAN_MVP_LOBBY && b.state == FLY_LAN_MVP_LOBBY)
            return true;
        if (a.state == FLY_LAN_MVP_ENDED || b.state == FLY_LAN_MVP_ENDED) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    std::cerr << "pair states host=" << a.state << '/' << a.reason
              << " guest=" << b.state << '/' << b.reason << '\n';
    return false;
}

bool wait_running(fly_lan_mvp_session* host, fly_lan_mvp_session* guest,
                  int milliseconds) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(milliseconds);
    do {
        fly_lan_mvp_snapshot a{}, b{};
        (void)fly_lan_mvp_snapshot_read(host, &a);
        (void)fly_lan_mvp_snapshot_read(guest, &b);
        if (a.state == FLY_LAN_MVP_RUNNING && b.state == FLY_LAN_MVP_RUNNING) return true;
        if (a.state == FLY_LAN_MVP_ENDED || b.state == FLY_LAN_MVP_ENDED) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

bool wait_completed(fly_lan_mvp_session* host, fly_lan_mvp_session* guest,
                    std::uint64_t count, int milliseconds) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(milliseconds);
    do {
        fly_lan_mvp_snapshot a{}, b{};
        (void)fly_lan_mvp_snapshot_read(host, &a);
        (void)fly_lan_mvp_snapshot_read(guest, &b);
        if (a.completed_frames >= count && b.completed_frames >= count) return true;
        if (a.state == FLY_LAN_MVP_ENDED || b.state == FLY_LAN_MVP_ENDED) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

std::vector<std::uint8_t> read_rom(const char* path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>());
}

std::vector<std::vector<std::uint8_t>> read_distinct_roms() {
    std::vector<std::vector<std::uint8_t>> result;
    for (const auto& item : std::filesystem::directory_iterator(FLYNES_CONTENT_ROM_DIR)) {
        if (!item.is_regular_file() || item.path().extension() != ".nes") continue;
        auto bytes = read_rom(item.path().string().c_str());
        if (!bytes.empty()) result.push_back(std::move(bytes));
        if (result.size() == 2) break;
    }
    return result;
}

std::string wait_invite(fly_lan_mvp_session* host) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        fly_lan_mvp_snapshot snapshot{};
        (void)fly_lan_mvp_snapshot_read(host, &snapshot);
        const auto needed = fly_lan_mvp_copy_invite(host, nullptr, 0);
        if (needed != 0) {
            std::string qr(needed, '\0');
            check(fly_lan_mvp_copy_invite(host, qr.data(), qr.size()) == needed,
                  "QR copied as one exact value");
            qr.resize(needed - 1);
            return qr;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    return {};
}

bool wait_host_rejected(fly_lan_mvp_session* host, fly_lan_mvp_session* guest) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        fly_lan_mvp_snapshot a{}, b{};
        (void)fly_lan_mvp_snapshot_read(host, &a);
        (void)fly_lan_mvp_snapshot_read(guest, &b);
        if (a.state == FLY_LAN_MVP_ENDED) return a.reason == FLY_LAN_MVP_REASON_REJECTED;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
}

int main(int argc, char** argv) {
    if (argc == 3) {
        std::ifstream input(argv[2], std::ios::binary);
        const std::string qr((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
        auto* guest = fly_lan_mvp_create();
        if (!guest || qr.empty() ||
            fly_lan_mvp_join(guest, argv[1], qr.data(), qr.size()) != 1) {
            if (guest) fly_lan_mvp_destroy(guest);
            std::cerr << "PROBE start failed\n";
            return 2;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
        fly_lan_mvp_snapshot snapshot{};
        do {
            (void)fly_lan_mvp_snapshot_read(guest, &snapshot);
            if (snapshot.state == FLY_LAN_MVP_LOBBY ||
                snapshot.state == FLY_LAN_MVP_ENDED) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } while (std::chrono::steady_clock::now() < deadline);
        std::cout << "PROBE state=" << snapshot.state << " reason=" << snapshot.reason
                  << " transportResult=" << snapshot.transport_result
                  << " transportOperation=" << snapshot.transport_operation << '\n';
        const bool connected = snapshot.state == FLY_LAN_MVP_LOBBY;
        fly_lan_mvp_destroy(guest);
        return connected ? 0 : 3;
    }
    const std::string mode = argc == 2 ? argv[1] : "";
    const std::string ip = local_ipv4();
    if (ip.empty()) {
        std::cerr << "SKIP: no private IPv4 interface for real-network P1 test\n";
        return 77;
    }
    const std::array<uint8_t, 16> token{{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}};
    auto* host = fly_lan_mvp_create();
    auto* guest = fly_lan_mvp_create();
    check(host != nullptr && guest != nullptr, "session owners created");
    if (!host || !guest) return 1;
    std::string diagnostics;
    const auto capture = [](void* context, const char* line) {
        *static_cast<std::string*>(context) += std::string(line) + '\n';
    };
    fly_lan_mvp_set_diagnostic_sink(host, capture, &diagnostics);
    check(fly_lan_mvp_host(host, ip.c_str(), token.data()) == 1, "host begins listening");
    std::string qr = wait_invite(host);
    check(!qr.empty(), "reachable host publishes QR after real bind");
    check(diagnostics.find("event=listen_ready") != std::string::npos,
          "diagnostics identify real bound listener");
    check(diagnostics.find("event=submit") != std::string::npos,
          "diagnostics identify submitted transport operation");
    check(diagnostics.find("0102030405060708090a0b0c0d0e0f10") == std::string::npos &&
          diagnostics.find("flynes-lan-v1:") == std::string::npos,
          "diagnostics do not expose token or full invitation");
    if (!qr.empty()) {
        if (mode == "--expires") {
            check(wait_state(host, FLY_LAN_MVP_ENDED, 125000),
                  "invitation reaches its advertised expiry");
            fly_lan_mvp_snapshot expired{};
            (void)fly_lan_mvp_snapshot_read(host, &expired);
            check(expired.reason == FLY_LAN_MVP_REASON_EXPIRED,
                  "expired invitation reports EXPIRED");
            check(fly_lan_mvp_copy_invite(host, nullptr, 0) == 0,
                  "expired invitation is no longer published");
            check(fly_lan_mvp_join(guest, ip.c_str(), qr.data(), qr.size()) == 1,
                  "old invitation can be submitted for fail-closed verification");
            check(wait_state(guest, FLY_LAN_MVP_ENDED, 12000),
                  "old invitation cannot establish a lobby after expiry");
            fly_lan_mvp_destroy(guest);
            fly_lan_mvp_destroy(host);
            if (failures) return 1;
            std::cout << "PASS lan MVP invitation expiry\n";
            return 0;
        }
        if (mode == "--delayed-join") {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(32);
            while (std::chrono::steady_clock::now() < until) {
                fly_lan_mvp_snapshot waiting{};
                (void)fly_lan_mvp_snapshot_read(host, &waiting);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            fly_lan_mvp_snapshot waiting{};
            (void)fly_lan_mvp_snapshot_read(host, &waiting);
            check(waiting.state == FLY_LAN_MVP_INVITING,
                  "120-second invitation still accepts a guest after 32 seconds");
            check(wait_invite(host) == qr, "waiting does not replace or discard the invitation");
        }
        check(fly_lan_mvp_join(guest, ip.c_str(), qr.data(), qr.size()) == 1,
              "guest starts QUIC with QR pin");
        check(wait_pair(host, guest, 10000), "encrypted JOIN and ACCEPT reach one lobby");
        fly_lan_mvp_snapshot a{}, b{};
        (void)fly_lan_mvp_snapshot_read(host, &a);
        (void)fly_lan_mvp_snapshot_read(guest, &b);
        check(std::memcmp(a.session_id, b.session_id, 16) == 0,
              "both peers share one session id");
        if (mode == "--invalid-rom") {
            const std::array<std::uint8_t, 8> invalid{{0, 1, 2, 3, 4, 5, 6, 7}};
            check(fly_lan_mvp_select_rom(guest, invalid.data(), invalid.size()) == 0,
                  "corrupt ROM is rejected");
            (void)fly_lan_mvp_snapshot_read(guest, &b);
            check(b.state != FLY_LAN_MVP_RUNNING &&
                  b.reason == FLY_LAN_MVP_REASON_ROM_INVALID,
                  "corrupt ROM reports ROM_INVALID without starting");
            fly_lan_mvp_destroy(guest);
            fly_lan_mvp_destroy(host);
            return failures == 0 ? 0 : 1;
        }
        if (mode == "--rom-mismatch") {
            const auto roms = read_distinct_roms();
            check(roms.size() == 2, "two manifest content ROMs are readable");
            if (roms.size() == 2) {
                check(fly_lan_mvp_select_rom(host, roms[0].data(), roms[0].size()) == 1,
                      "host selects first valid ROM");
                const auto config_until = std::chrono::steady_clock::now() +
                                          std::chrono::seconds(2);
                do {
                    (void)fly_lan_mvp_snapshot_read(host, &a);
                    (void)fly_lan_mvp_snapshot_read(guest, &b);
                    if (b.peer_configured != 0) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                } while (std::chrono::steady_clock::now() < config_until);
                check(b.peer_configured != 0, "guest receives host configuration");
                check(fly_lan_mvp_select_rom(guest, roms[1].data(), roms[1].size()) == 0,
                      "different valid ROM cannot be confirmed");
                (void)fly_lan_mvp_snapshot_read(guest, &b);
                check(b.state == FLY_LAN_MVP_CONFIGURING &&
                      b.reason == FLY_LAN_MVP_REASON_ROM_MISMATCH,
                      "different content reports ROM_MISMATCH without disconnecting");
                check(fly_lan_mvp_select_rom(guest, roms[0].data(), roms[0].size()) == 1,
                      "same connection can correct a mismatched local selection");
            }
            fly_lan_mvp_destroy(guest);
            fly_lan_mvp_destroy(host);
            return failures == 0 ? 0 : 1;
        }
        if (mode == "--ready" || mode == "--stall") {
            const auto rom = read_rom(FLYNES_RUNTIME_ROM_FIXTURE);
            check(!rom.empty(), "real ROM fixture is readable");
            check(fly_lan_mvp_select_rom(host, rom.data(), rom.size()) == 1,
                  "host selects configuration");
            check(fly_lan_mvp_select_rom(guest, rom.data(), rom.size()) == 1,
                  "guest selects matching configuration");
            check(fly_lan_mvp_confirm(host) == 1, "first host confirmation is accepted");
            check(fly_lan_mvp_confirm(host) == 1, "duplicate host confirmation is idempotent");
            const auto one_ready_until = std::chrono::steady_clock::now() +
                                         std::chrono::milliseconds(250);
            while (std::chrono::steady_clock::now() < one_ready_until) {
                (void)fly_lan_mvp_snapshot_read(host, &a);
                (void)fly_lan_mvp_snapshot_read(guest, &b);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            check(a.state != FLY_LAN_MVP_RUNNING && b.state != FLY_LAN_MVP_RUNNING,
                  "one confirmation cannot start either runtime");
            if (mode == "--ready") {
                check(fly_lan_mvp_select_rom(host, rom.data(), rom.size()) == 1,
                      "reselecting configuration invalidates local READY");
                (void)fly_lan_mvp_snapshot_read(host, &a);
                check(a.local_ready == 0, "reselection clears the prior confirmation");
                check(fly_lan_mvp_confirm(host) == 1,
                      "host reconfirms after reselection");
            }
            check(fly_lan_mvp_confirm(guest) == 1, "guest confirmation is accepted");
            check(wait_running(host, guest, 5000), "both confirmations start exactly one round");
            check(fly_lan_mvp_confirm(host) == 0 && fly_lan_mvp_confirm(guest) == 0,
                  "late confirmation cannot restart a running round");
            if (mode == "--stall") {
                check(wait_completed(host, guest, 2, 1000),
                      "agreed delay frames start before stall");
                std::this_thread::sleep_for(std::chrono::milliseconds(2100));
                (void)fly_lan_mvp_snapshot_read(host, &a);
                (void)fly_lan_mvp_snapshot_read(guest, &b);
                check(a.state == FLY_LAN_MVP_ENDED && b.state == FLY_LAN_MVP_ENDED,
                      "missing required input ends both runtimes");
                check(a.reason == FLY_LAN_MVP_REASON_STALL &&
                      b.reason == FLY_LAN_MVP_REASON_STALL,
                      "two-second progress timeout reports STALL");
            }
            fly_lan_mvp_destroy(guest);
            fly_lan_mvp_destroy(host);
            return failures == 0 ? 0 : 1;
        }
        if (mode == "--play") {
            const auto rom = read_rom(FLYNES_RUNTIME_ROM_FIXTURE);
            check(!rom.empty(), "real ROM fixture is readable");
            check(fly_lan_mvp_select_rom(host, rom.data(), rom.size()) == 1,
                  "host loads a fresh real ROM");
            check(fly_lan_mvp_select_rom(guest, rom.data(), rom.size()) == 1,
                  "guest loads the same real ROM");
            check(fly_lan_mvp_confirm(host) == 1, "host confirms configuration");
            const auto one_ready_until = std::chrono::steady_clock::now() +
                                         std::chrono::milliseconds(300);
            while (std::chrono::steady_clock::now() < one_ready_until) {
                (void)fly_lan_mvp_snapshot_read(host, &a);
                (void)fly_lan_mvp_snapshot_read(guest, &b);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            check(a.state != FLY_LAN_MVP_RUNNING && b.state != FLY_LAN_MVP_RUNNING,
                  "one confirmation cannot start the game");
            check(fly_lan_mvp_confirm(guest) == 1, "guest confirms configuration");
            check(wait_running(host, guest, 5000), "both confirmations start one round");
            check(wait_completed(host, guest, 2, 1000),
                  "two-frame delay starts with agreed zero input");

            (void)fly_lan_mvp_snapshot_read(host, &a);
            const auto before = a.completed_frames;
            check(fly_lan_mvp_submit_input(host, 0x01u) == 1,
                  "P1 submits its next distinct input");
            const auto one_input_until = std::chrono::steady_clock::now() +
                                         std::chrono::milliseconds(200);
            while (std::chrono::steady_clock::now() < one_input_until) {
                (void)fly_lan_mvp_snapshot_read(host, &a);
                (void)fly_lan_mvp_snapshot_read(guest, &b);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            check(a.completed_frames == before && b.completed_frames == before,
                  "one player's input cannot advance either runtime");
            check(fly_lan_mvp_submit_input(guest, 0x80u) == 1,
                  "P2 submits a different input");
            check(wait_completed(host, guest, before + 1, 2000),
                  "both inputs advance exactly one frame");
            (void)fly_lan_mvp_snapshot_read(host, &a);
            (void)fly_lan_mvp_snapshot_read(guest, &b);
            check(a.applied_buttons[0] == 0x01u && a.applied_buttons[1] == 0x80u &&
                  b.applied_buttons[0] == 0x01u && b.applied_buttons[1] == 0x80u,
                  "completed core frame reports both seats, including remote input");

            for (std::uint32_t index = 0; index < 62; ++index) {
                check(fly_lan_mvp_submit_input(host, (index & 1u) == 0 ? 0x02u : 0u) == 1,
                      "P1 sequence is accepted");
                check(fly_lan_mvp_submit_input(guest, (index & 1u) == 0 ? 0x40u : 0u) == 1,
                      "P2 sequence is accepted");
                check(wait_completed(host, guest, before + 2 + index, 2000),
                      "lockstep frame completes on both peers");
            }
            (void)fly_lan_mvp_snapshot_read(host, &a);
            (void)fly_lan_mvp_snapshot_read(guest, &b);
            check(a.completed_frames == b.completed_frames,
                  "both runtimes expose the same completed frame count");
            check(a.last_digest_frame == b.last_digest_frame &&
                  std::memcmp(a.last_state_digest, b.last_state_digest, 32) == 0,
                  "periodic real-core state digests match");

            std::vector<std::uint8_t> host_frame(FLY_RUNTIME_RGB565_BYTES);
            std::vector<std::uint8_t> guest_frame(FLY_RUNTIME_RGB565_BYTES);
            fly_latest_frame_v1 host_meta{};
            host_meta.struct_size = FLY_LATEST_FRAME_V1_SIZE;
            host_meta.version = FLY_LATEST_FRAME_VERSION_1;
            fly_latest_frame_v1 guest_meta = host_meta;
            check(fly_lan_mvp_copy_latest_frame(host, host_frame.data(), host_frame.size(),
                                               &host_meta) == 1,
                  "host exposes the real core frame");
            check(fly_lan_mvp_copy_latest_frame(guest, guest_frame.data(), guest_frame.size(),
                                               &guest_meta) == 1,
                  "guest exposes the real core frame");
            check(host_meta.frame_index == guest_meta.frame_index && host_frame == guest_frame,
                  "both peers render the same completed frame");

            std::array<std::int16_t, 2048> host_pcm{};
            std::array<std::int16_t, 2048> guest_pcm{};
            fly_pcm_block_v1 host_block{};
            host_block.struct_size = FLY_PCM_BLOCK_V1_SIZE;
            host_block.version = FLY_PCM_BLOCK_VERSION_1;
            fly_pcm_block_v1 guest_block = host_block;
            check(fly_lan_mvp_pull_pcm(host, host_pcm.data(),
                                      static_cast<std::uint32_t>(host_pcm.size()), &host_block) == 1,
                  "host PCM reaches the shared consumer API");
            check(fly_lan_mvp_pull_pcm(guest, guest_pcm.data(),
                                      static_cast<std::uint32_t>(guest_pcm.size()), &guest_block) == 1,
                  "guest PCM reaches the shared consumer API");
            check(host_block.sample_count > 0 && guest_block.sample_count > 0,
                  "both peers produce PCM samples");
            check(std::equal(host_pcm.begin(), host_pcm.begin() + host_block.sample_count,
                             guest_pcm.begin()),
                  "both peers produce the same PCM prefix");
            for (int drain = 0; drain < 128 && host_block.sample_count != 0; ++drain)
                (void)fly_lan_mvp_pull_pcm(host, host_pcm.data(),
                    static_cast<std::uint32_t>(host_pcm.size()), &host_block);
            check(host_block.sample_count == 0,
                  "polling without new core frames must not enqueue synthetic silence");

            check(fly_lan_mvp_submit_input(host, 0) == 1 &&
                  fly_lan_mvp_submit_input(host, 0) == 1,
                  "fast peer may queue the agreed two input frames");
            check(fly_lan_mvp_submit_input(host, 0) == 0,
                  "fast peer cannot queue seconds of stale input behind a slower peer");

            const auto connected_id = std::vector<std::uint8_t>(a.session_id, a.session_id + 16);
            check(fly_lan_mvp_set_paused(host, 1) == 1, "host pauses the current game");
            (void)fly_lan_mvp_snapshot_read(host, &a);
            const auto paused_frame = a.completed_frames;
            const auto pause_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(2200);
            while (std::chrono::steady_clock::now() < pause_until) {
                (void)fly_lan_mvp_snapshot_read(host, &a);
                (void)fly_lan_mvp_snapshot_read(guest, &b);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            check(a.state == FLY_LAN_MVP_RUNNING && b.state == FLY_LAN_MVP_RUNNING &&
                  a.paused && b.paused && a.completed_frames == paused_frame,
                  "pause freezes the game without disconnecting or timing out");
            check(fly_lan_mvp_set_paused(host, 0) == 1, "host resumes the same game");
            check(fly_lan_mvp_return_lobby(guest) == 1, "either player can return both apps to lobby");
            check(wait_pair(host, guest, 3000), "return lobby drains old inputs on the same connection");
            (void)fly_lan_mvp_snapshot_read(host, &a);
            check(std::equal(connected_id.begin(), connected_id.end(), a.session_id),
                  "returning to lobby keeps the connected session identity");
            auto other_roms = read_distinct_roms();
            for (const auto& next_rom : other_roms) {
                if (next_rom == rom) continue;
                check(fly_lan_mvp_select_rom(host, next_rom.data(), next_rom.size()) == 1 &&
                      fly_lan_mvp_select_rom(guest, next_rom.data(), next_rom.size()) == 1,
                      "same connected peers can select a different local ROM");
                check(fly_lan_mvp_confirm(host) == 1 && fly_lan_mvp_confirm(guest) == 1 &&
                      wait_running(host, guest, 3000), "second game starts without pairing again");
                break;
            }

            fly_lan_mvp_destroy(guest);
            fly_lan_mvp_destroy(host);
            if (failures) return 1;
            std::cout << "PASS lan MVP real ROM lockstep\n";
            return 0;
        }
        if (mode == "--idle") {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(7);
            while (std::chrono::steady_clock::now() < until) {
                (void)fly_lan_mvp_snapshot_read(host, &a);
                (void)fly_lan_mvp_snapshot_read(guest, &b);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            check(a.state == FLY_LAN_MVP_LOBBY && b.state == FLY_LAN_MVP_LOBBY,
                  "connected lobby survives the transport idle window");
        }
    }
    fly_lan_mvp_cancel(host);
    check(wait_state(host, FLY_LAN_MVP_ENDED, 1000), "host cancellation ends session");
    check(wait_state(guest, FLY_LAN_MVP_ENDED, 2500),
          "host cancellation closes guest connection");
    fly_lan_mvp_destroy(guest);
    fly_lan_mvp_destroy(host);

    auto* pin_host = fly_lan_mvp_create();
    auto* pin_guest = fly_lan_mvp_create();
    check(fly_lan_mvp_host(pin_host, ip.c_str(), token.data()) == 1,
          "pin rejection host starts");
    std::string bad_pin = wait_invite(pin_host);
    check(!bad_pin.empty(), "pin rejection invite available");
    if (!bad_pin.empty()) {
        const auto token_separator = bad_pin.rfind(':');
        const auto pin_separator = bad_pin.rfind(':', token_separator - 1);
        bad_pin[pin_separator + 1] = bad_pin[pin_separator + 1] == '0' ? '1' : '0';
        check(fly_lan_mvp_join(pin_guest, ip.c_str(), bad_pin.data(), bad_pin.size()) == 1,
              "guest attempts with wrong pin");
        check(wait_state(pin_guest, FLY_LAN_MVP_ENDED, 10000),
              "wrong pin cannot enter lobby");
        fly_lan_mvp_snapshot pin_state{};
        (void)fly_lan_mvp_snapshot_read(pin_guest, &pin_state);
        check(pin_state.reason == FLY_LAN_MVP_REASON_CONNECTION,
              "wrong pin fails TLS connection");
    }
    fly_lan_mvp_destroy(pin_guest);
    fly_lan_mvp_destroy(pin_host);

    auto* token_host = fly_lan_mvp_create();
    auto* token_guest = fly_lan_mvp_create();
    check(fly_lan_mvp_host(token_host, ip.c_str(), token.data()) == 1,
          "token rejection host starts");
    std::string bad_token = wait_invite(token_host);
    check(!bad_token.empty(), "token rejection invite available");
    if (!bad_token.empty()) {
        bad_token.back() = bad_token.back() == '0' ? '1' : '0';
        check(fly_lan_mvp_join(token_guest, ip.c_str(), bad_token.data(), bad_token.size()) == 1,
              "guest attempts with wrong token");
        check(wait_host_rejected(token_host, token_guest),
              "wrong token rejected inside encrypted JOIN");
    }
    fly_lan_mvp_destroy(token_guest);
    fly_lan_mvp_destroy(token_host);
    if (failures) return 1;
    std::cout << "PASS lan MVP real QUIC session\n";
    return 0;
}
