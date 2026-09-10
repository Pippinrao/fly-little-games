#include "session_codec.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
using socket_t = SOCKET;
constexpr socket_t kInvalid = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t kInvalid = -1;
#endif

namespace fs = std::filesystem;
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

bool read_all(const fs::path& path, std::vector<std::uint8_t>* out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (end < 0)
        return false;
    in.seekg(0, std::ios::beg);
    out->assign(static_cast<std::size_t>(end), 0);
    if (end != 0 && !in.read(reinterpret_cast<char*>(out->data()), end))
        return false;
    return true;
}

std::string read_text(const fs::path& path)
{
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();
    return line;
}

bool close_socket(socket_t sock)
{
#if defined(_WIN32)
    return closesocket(sock) == 0;
#else
    return ::close(sock) == 0;
#endif
}

bool send_all(socket_t sock, const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const char*>(data);
    std::size_t sent = 0;
    while (sent < size)
    {
#if defined(_WIN32)
        const int n = ::send(sock, bytes + sent, static_cast<int>(size - sent), 0);
#else
        const auto n = ::send(sock, bytes + sent, size - sent, 0);
#endif
        if (n <= 0)
            return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool recv_all(socket_t sock, void* data, std::size_t size)
{
    auto* bytes = static_cast<char*>(data);
    std::size_t got = 0;
    while (got < size)
    {
#if defined(_WIN32)
        const int n = ::recv(sock, bytes + got, static_cast<int>(size - got), 0);
#else
        const auto n = ::recv(sock, bytes + got, size - got, 0);
#endif
        if (n <= 0)
            return false;
        got += static_cast<std::size_t>(n);
    }
    return true;
}

bool send_u32be(socket_t sock, std::uint32_t value)
{
    const std::uint8_t bytes[4] = {
        static_cast<std::uint8_t>(value >> 24u),
        static_cast<std::uint8_t>(value >> 16u),
        static_cast<std::uint8_t>(value >> 8u),
        static_cast<std::uint8_t>(value),
    };
    return send_all(sock, bytes, 4u);
}

bool recv_u32be(socket_t sock, std::uint32_t* value)
{
    std::uint8_t bytes[4]{};
    if (!recv_all(sock, bytes, 4u))
        return false;
    *value = (static_cast<std::uint32_t>(bytes[0]) << 24u) |
             (static_cast<std::uint32_t>(bytes[1]) << 16u) |
             (static_cast<std::uint32_t>(bytes[2]) << 8u) |
             bytes[3];
    return true;
}

bool send_case(socket_t sock, const std::string& type, const std::vector<std::uint8_t>& bytes)
{
    if (!send_u32be(sock, static_cast<std::uint32_t>(type.size())))
        return false;
    if (!type.empty() && !send_all(sock, type.data(), type.size()))
        return false;
    if (!send_u32be(sock, static_cast<std::uint32_t>(bytes.size())))
        return false;
    return bytes.empty() || send_all(sock, bytes.data(), bytes.size());
}

Status remote_check(socket_t sock, const std::string& type, const std::vector<std::uint8_t>& bytes,
                    std::uint8_t hash[32])
{
    if (!send_case(sock, type, bytes))
        return Status::InvalidField;
    std::int32_t raw = 0;
    if (!recv_all(sock, &raw, sizeof(raw)))
        return Status::InvalidField;
    if (!recv_all(sock, hash, 32u))
        return Status::InvalidField;
    return static_cast<Status>(raw);
}

int serve_peer(socket_t sock)
{
    for (;;)
    {
        std::uint32_t type_len = 0;
        if (!recv_u32be(sock, &type_len))
            return 1;
        if (type_len == 0xFFFFFFFFu)
            return 0;
        if (type_len > 256u)
            return 1;
        std::string type(type_len, '\0');
        if (type_len != 0u && !recv_all(sock, type.data(), type_len))
            return 1;
        std::uint32_t data_len = 0;
        if (!recv_u32be(sock, &data_len))
            return 1;
        if (data_len > 300000u)
            return 1;
        std::vector<std::uint8_t> bytes(data_len);
        if (data_len != 0u && !recv_all(sock, bytes.data(), data_len))
            return 1;
        std::uint8_t hash[32]{};
        const Status status = flynes::session::wire::check(
            type.c_str(), bytes.empty() ? nullptr : bytes.data(), bytes.size(), hash);
        const std::int32_t raw = static_cast<std::int32_t>(status);
        if (!send_all(sock, &raw, sizeof(raw)) || !send_all(sock, hash, 32u))
            return 1;
    }
}

socket_t listen_loopback(unsigned short* port_out)
{
    socket_t server = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == kInvalid)
        return kInvalid;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);
    if (::bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        close_socket(server);
        return kInvalid;
    }
    if (::listen(server, 1) != 0)
    {
        close_socket(server);
        return kInvalid;
    }
    sockaddr_in got{};
#if defined(_WIN32)
    int len = sizeof(got);
#else
    socklen_t len = sizeof(got);
#endif
    if (::getsockname(server, reinterpret_cast<sockaddr*>(&got), &len) != 0)
    {
        close_socket(server);
        return kInvalid;
    }
    *port_out = ntohs(got.sin_port);
    return server;
}

socket_t connect_loopback(unsigned short port)
{
    socket_t client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == kInvalid)
        return kInvalid;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        close_socket(client);
        return kInvalid;
    }
    return client;
}

struct GoldenCase
{
    std::string type;
    std::vector<std::uint8_t> legal;
    bool negative_only = false;
    bool skip_enum = false;
    bool skip_reserved = false;
    std::vector<std::uint8_t> truncate;
    std::vector<std::uint8_t> trailing;
    std::vector<std::uint8_t> unknown_enum;
    std::vector<std::uint8_t> nonzero_reserved;
};

std::vector<GoldenCase> load_goldens()
{
    std::vector<GoldenCase> out;
    const fs::path root(FLYNES_SESSION_GOLDEN_DIR);
    for (const auto& entry : fs::directory_iterator(root))
    {
        if (!entry.is_directory())
            continue;
        GoldenCase item;
        item.type = read_text(entry.path() / "type.txt");
        item.negative_only = fs::exists(entry.path() / "negative_only.txt");
        item.skip_enum = fs::exists(entry.path() / "skip_enum.txt");
        item.skip_reserved = fs::exists(entry.path() / "skip_reserved.txt");
        read_all(entry.path() / "legal.bin", &item.legal);
        read_all(entry.path() / "truncate.bin", &item.truncate);
        read_all(entry.path() / "trailing.bin", &item.trailing);
        read_all(entry.path() / "unknown_enum.bin", &item.unknown_enum);
        read_all(entry.path() / "nonzero_reserved.bin", &item.nonzero_reserved);
        out.push_back(std::move(item));
    }
    return out;
}

void exercise_socket(socket_t sock, const std::vector<GoldenCase>& cases, const char* label)
{
    for (const GoldenCase& item : cases)
    {
        std::uint8_t hash[32]{};
        if (item.negative_only)
        {
            check(remote_check(sock, item.type, item.legal, hash) == Status::UnknownCriticalTag,
                  std::string(label) + " unknown-critical " + item.type);
            continue;
        }
        check(remote_check(sock, item.type, item.legal, hash) == Status::Ok,
              std::string(label) + " legal " + item.type);
        check(remote_check(sock, item.type, item.truncate, hash) == Status::Truncated,
              std::string(label) + " truncate " + item.type);
        check(remote_check(sock, item.type, item.trailing, hash) == Status::Trailing,
              std::string(label) + " trailing " + item.type);
        if (!item.skip_enum)
            check(remote_check(sock, item.type, item.unknown_enum, hash) == Status::UnknownEnum,
                  std::string(label) + " enum " + item.type);
        if (!item.skip_reserved)
            check(remote_check(sock, item.type, item.nonzero_reserved, hash) ==
                      Status::NonzeroReserved,
                  std::string(label) + " reserved " + item.type);
    }
    check(send_u32be(sock, 0xFFFFFFFFu), std::string(label) + " close");
}

void run_fuzz(const std::vector<GoldenCase>& cases)
{
    int mutations = 0;
    for (const GoldenCase& item : cases)
    {
        std::uint8_t hash[32]{};
        if (item.negative_only)
        {
            check(flynes::session::wire::check(item.type.c_str(), item.legal.data(),
                                               item.legal.size(), hash) == Status::UnknownCriticalTag,
                  "fuzz negative " + item.type);
            continue;
        }
        check(flynes::session::wire::check(item.type.c_str(), item.legal.data(), item.legal.size(),
                                           hash) == Status::Ok,
              "fuzz legal " + item.type);
        check(flynes::session::wire::check(item.type.c_str(),
                                           item.truncate.empty() ? nullptr : item.truncate.data(),
                                           item.truncate.size(), hash) == Status::Truncated,
              "fuzz truncate " + item.type);
        std::vector<std::uint8_t> flipped = item.legal;
        if (!flipped.empty() && item.type != "0x000b")
        {
            flipped[0] = static_cast<std::uint8_t>(flipped[0] ^ 0xFFu);
            const Status flipped_status = flynes::session::wire::check(
                item.type.c_str(), flipped.data(), flipped.size(), hash);
            check(flipped_status != Status::Ok, "fuzz bitflip " + item.type);
            ++mutations;
        }
    }
    check(mutations >= 20, "bounded fuzz covered structured kinds");
}

#if defined(_WIN32)
bool spawn_peer(const char* self, unsigned short port, PROCESS_INFORMATION* info)
{
    std::string cmd = std::string("\"") + self + "\" --peer " + std::to_string(port);
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    ZeroMemory(info, sizeof(*info));
    return CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                          &startup, info) != 0;
}
#endif

int init_net()
{
#if defined(_WIN32)
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data);
#else
    return 0;
#endif
}

void shutdown_net()
{
#if defined(_WIN32)
    WSACleanup();
#endif
}

} // namespace

int main(int argc, char** argv)
{
    if (init_net() != 0)
    {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    if (argc >= 3 && std::string(argv[1]) == "--peer")
    {
        const unsigned short port =
            static_cast<unsigned short>(std::stoi(argv[2]));
        socket_t client = connect_loopback(port);
        int rc = 1;
        if (client != kInvalid)
        {
            rc = serve_peer(client);
            close_socket(client);
        }
        shutdown_net();
        return rc;
    }

    const std::vector<GoldenCase> cases = load_goldens();
    check(!cases.empty(), "goldens loaded");
    run_fuzz(cases);

    unsigned short port = 0;
    socket_t server = listen_loopback(&port);
    check(server != kInvalid, "listen 127.0.0.1");
    std::thread peer([server]() {
        socket_t accepted = ::accept(server, nullptr, nullptr);
        if (accepted != kInvalid)
        {
            serve_peer(accepted);
            close_socket(accepted);
        }
    });
    socket_t client = connect_loopback(port);
    check(client != kInvalid, "thread client connect");
    if (client != kInvalid)
    {
        exercise_socket(client, cases, "thread");
        close_socket(client);
    }
    peer.join();
    close_socket(server);

#if defined(_WIN32)
    unsigned short proc_port = 0;
    socket_t proc_server = listen_loopback(&proc_port);
    check(proc_server != kInvalid, "process listen");
    PROCESS_INFORMATION info{};
    char self[MAX_PATH]{};
    const DWORD n = GetModuleFileNameA(nullptr, self, MAX_PATH);
    check(n > 0 && n < MAX_PATH, "self path");
    check(spawn_peer(self, proc_port, &info), "CreateProcess peer");
    socket_t accepted = ::accept(proc_server, nullptr, nullptr);
    check(accepted != kInvalid, "accept process peer");
    if (accepted != kInvalid)
    {
        exercise_socket(accepted, cases, "process");
        close_socket(accepted);
    }
    WaitForSingleObject(info.hProcess, 30000);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    close_socket(proc_server);
#endif

    shutdown_net();
    if (failures != 0)
    {
        std::cerr << "flynes_session_codec_loopback: FAIL (" << failures << ")\n";
        return 1;
    }
    std::cout << "flynes_session_codec_loopback: PASS\n";
    return 0;
}
