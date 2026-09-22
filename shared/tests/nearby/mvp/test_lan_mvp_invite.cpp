#include "lan_mvp/invite.hpp"

#include <iostream>
#include <string>

using flynes::session::lan_mvp::Invite;
using flynes::session::lan_mvp::format_invite;
using flynes::session::lan_mvp::parse_invite;

namespace {
int failures = 0;
void check(bool good, const char* reason) {
    if (!good) { std::cerr << "FAIL: " << reason << '\n'; ++failures; }
}
}

int main() {
    Invite source{};
    source.ipv4 = {{192, 168, 4, 21}};
    source.port = 42881;
    for (std::size_t i = 0; i < source.spki_pin.size(); ++i)
        source.spki_pin[i] = static_cast<std::uint8_t>(i);
    for (std::size_t i = 0; i < source.token.size(); ++i)
        source.token[i] = static_cast<std::uint8_t>(0xf0u + i);

    std::string qr;
    check(format_invite(source, &qr), "valid invite formats");
    Invite decoded{};
    check(parse_invite(qr, &decoded), "formatted invite parses");
    check(decoded.ipv4 == source.ipv4 && decoded.port == source.port &&
              decoded.spki_pin == source.spki_pin && decoded.token == source.token,
          "round trip retains endpoint, exact pin and single-use token");
    check(!parse_invite("flynes-lan-v2:192.168.4.21:42881:" +
                            std::string(64, '0') + ":" + std::string(32, '0'), &decoded),
          "unknown protocol version rejected");
    check(!parse_invite("flynes-lan-v1:192.168.4.21:42881:bad:token", &decoded),
          "damaged payload rejected");
    check(!parse_invite("flynes-lan-v1:127.0.0.1:42881:" +
                            std::string(64, '0') + ":" + std::string(32, '0'), &decoded),
          "loopback endpoint rejected");
    check(!parse_invite("flynes-lan-v1:192.168.4.21:0:" +
                            std::string(64, '0') + ":" + std::string(32, '0'), &decoded),
          "port zero rejected");
    if (failures != 0) return 1;
    std::cout << "PASS lan MVP invite\n";
    return 0;
}
