#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace flynes::session::lan_mvp::wire {

inline constexpr std::size_t kMaxBodySize = 1024;

enum class Kind : std::uint8_t {
    Join = 1,
    Accept = 2,
    Config = 3,
    Ready = 4,
    Start = 5,
    Input = 6,
    Digest = 7,
    End = 8,
    LobbyRequest = 9,
    Lobby = 10,
    LobbyAck = 11,
    Pause = 12,
};

struct Message {
    Kind kind = Kind::Join;
    std::vector<std::uint8_t> payload;
};

bool encode(Kind kind,
            const std::vector<std::uint8_t>& payload,
            std::vector<std::uint8_t>* output);
bool encode(Kind kind,
            std::initializer_list<std::uint8_t> payload,
            std::vector<std::uint8_t>* output);

class Decoder {
public:
    bool push(const std::uint8_t* bytes, std::size_t size);
    bool pop(Message* message);
    bool failed() const { return failed_; }

private:
    bool validate_front();

    std::vector<std::uint8_t> bytes_;
    bool failed_ = false;
};

} // namespace flynes::session::lan_mvp::wire
