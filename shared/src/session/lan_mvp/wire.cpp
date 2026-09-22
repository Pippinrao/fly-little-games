#include "lan_mvp/wire.hpp"

#include <algorithm>
#include <limits>

namespace flynes::session::lan_mvp::wire {

bool encode(Kind kind,
            const std::vector<std::uint8_t>& payload,
            std::vector<std::uint8_t>* output) {
    if (output == nullptr || payload.size() + 1 > kMaxBodySize ||
        payload.size() + 1 > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }
    const auto body_size = static_cast<std::uint16_t>(payload.size() + 1);
    output->clear();
    output->reserve(static_cast<std::size_t>(body_size) + 2);
    output->push_back(static_cast<std::uint8_t>(body_size >> 8u));
    output->push_back(static_cast<std::uint8_t>(body_size & 0xffu));
    output->push_back(static_cast<std::uint8_t>(kind));
    output->insert(output->end(), payload.begin(), payload.end());
    return true;
}

bool encode(Kind kind,
            std::initializer_list<std::uint8_t> payload,
            std::vector<std::uint8_t>* output) {
    return encode(kind, std::vector<std::uint8_t>(payload), output);
}

bool Decoder::validate_front() {
    if (failed_ || bytes_.size() < 2) return !failed_;
    const auto body_size = (static_cast<std::size_t>(bytes_[0]) << 8u) |
                           static_cast<std::size_t>(bytes_[1]);
    if (body_size == 0 || body_size > kMaxBodySize) {
        failed_ = true;
        bytes_.clear();
        return false;
    }
    return true;
}

bool Decoder::push(const std::uint8_t* bytes, std::size_t size) {
    if (failed_ || (bytes == nullptr && size != 0)) return false;
    bytes_.insert(bytes_.end(), bytes, bytes + size);
    return validate_front();
}

bool Decoder::pop(Message* message) {
    if (message == nullptr || !validate_front() || bytes_.size() < 2) return false;
    const auto body_size = (static_cast<std::size_t>(bytes_[0]) << 8u) |
                           static_cast<std::size_t>(bytes_[1]);
    if (bytes_.size() < body_size + 2) return false;
    message->kind = static_cast<Kind>(bytes_[2]);
    message->payload.assign(bytes_.begin() + 3,
                            bytes_.begin() + static_cast<std::ptrdiff_t>(body_size + 2));
    bytes_.erase(bytes_.begin(),
                 bytes_.begin() + static_cast<std::ptrdiff_t>(body_size + 2));
    return validate_front();
}

} // namespace flynes::session::lan_mvp::wire
