#include "invite.hpp"

#include <utility>

namespace flynes::session::lan_mvp {
namespace {

constexpr std::string_view kPrefix = "flynes-lan-v1:";
constexpr char kHex[] = "0123456789abcdef";

bool parse_decimal(std::string_view text, unsigned maximum, unsigned* out) {
    if (text.empty() || (text.size() > 1 && text.front() == '0')) return false;
    unsigned value = 0;
    for (char digit : text) {
        if (digit < '0' || digit > '9') return false;
        value = value * 10u + static_cast<unsigned>(digit - '0');
        if (value > maximum) return false;
    }
    *out = value;
    return true;
}

bool allowed_address(const std::array<std::uint8_t, 4>& ip) {
    return ip[0] != 0 && ip[0] != 127 && ip[0] < 224 &&
           !(ip[0] == 169 && ip[1] == 254) && ip[3] != 0 && ip[3] != 255;
}

bool parse_ip(std::string_view text, std::array<std::uint8_t, 4>* out) {
    std::array<std::uint8_t, 4> ip{};
    for (std::size_t i = 0; i < ip.size(); ++i) {
        const auto dot = text.find('.');
        if ((i < 3 && dot == std::string_view::npos) ||
            (i == 3 && dot != std::string_view::npos)) return false;
        const auto part = i == 3 ? text : text.substr(0, dot);
        unsigned octet = 0;
        if (!parse_decimal(part, 255, &octet)) return false;
        ip[i] = static_cast<std::uint8_t>(octet);
        if (i < 3) text.remove_prefix(dot + 1);
    }
    if (!allowed_address(ip)) return false;
    *out = ip;
    return true;
}

int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

template <std::size_t N>
bool parse_hex(std::string_view text, std::array<std::uint8_t, N>* out) {
    if (text.size() != N * 2) return false;
    for (std::size_t i = 0; i < N; ++i) {
        const int high = hex_value(text[i * 2]);
        const int low = hex_value(text[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        (*out)[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

template <std::size_t N>
void append_hex(const std::array<std::uint8_t, N>& bytes, std::string* out) {
    for (const auto byte : bytes) {
        out->push_back(kHex[byte >> 4]);
        out->push_back(kHex[byte & 15u]);
    }
}

bool take_field(std::string_view* remaining, std::string_view* field) {
    const auto separator = remaining->find(':');
    if (separator == std::string_view::npos) return false;
    *field = remaining->substr(0, separator);
    remaining->remove_prefix(separator + 1);
    return true;
}

} // namespace

bool parse_lan_ipv4(std::string_view text, std::array<std::uint8_t, 4>* out) {
    return out != nullptr && parse_ip(text, out);
}

bool format_invite(const Invite& invite, std::string* out) {
    if (out == nullptr || !allowed_address(invite.ipv4) || invite.port == 0) return false;
    std::string result(kPrefix);
    for (std::size_t i = 0; i < invite.ipv4.size(); ++i) {
        if (i != 0) result.push_back('.');
        result += std::to_string(invite.ipv4[i]);
    }
    result.push_back(':');
    result += std::to_string(invite.port);
    result.push_back(':');
    append_hex(invite.spki_pin, &result);
    result.push_back(':');
    append_hex(invite.token, &result);
    *out = std::move(result);
    return true;
}

bool parse_invite(std::string_view text, Invite* out) {
    if (out == nullptr || text.substr(0, kPrefix.size()) != kPrefix) return false;
    text.remove_prefix(kPrefix.size());
    std::string_view ip_text;
    std::string_view port_text;
    std::string_view pin_text;
    if (!take_field(&text, &ip_text) || !take_field(&text, &port_text) ||
        !take_field(&text, &pin_text)) return false;
    Invite parsed{};
    unsigned port = 0;
    if (!parse_ip(ip_text, &parsed.ipv4) || !parse_decimal(port_text, 65535, &port) ||
        port == 0 || !parse_hex(pin_text, &parsed.spki_pin) ||
        !parse_hex(text, &parsed.token)) return false;
    parsed.port = static_cast<std::uint16_t>(port);
    *out = parsed;
    return true;
}

} // namespace flynes::session::lan_mvp
