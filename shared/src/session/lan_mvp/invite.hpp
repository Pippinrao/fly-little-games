#ifndef FLYNES_SESSION_LAN_MVP_INVITE_HPP
#define FLYNES_SESSION_LAN_MVP_INVITE_HPP

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace flynes::session::lan_mvp {

struct Invite {
    std::array<std::uint8_t, 4> ipv4{};
    std::uint16_t port = 0;
    std::array<std::uint8_t, 32> spki_pin{};
    std::array<std::uint8_t, 16> token{};
};

// QR text is an exact, versioned, offline payload. No logging of this value.
bool format_invite(const Invite& invite, std::string* out);
bool parse_invite(std::string_view text, Invite* out);
bool parse_lan_ipv4(std::string_view text, std::array<std::uint8_t, 4>* out);

} // namespace flynes::session::lan_mvp

#endif
