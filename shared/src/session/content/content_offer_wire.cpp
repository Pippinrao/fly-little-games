#include "content_offer_wire.hpp"

#include <cstring>

namespace flynes::session::content {
namespace {

void put_u16be(std::uint8_t* p, std::uint16_t value) noexcept
{
    p[0] = static_cast<std::uint8_t>(value >> 8u);
    p[1] = static_cast<std::uint8_t>(value);
}

void put_u32be(std::uint8_t* p, std::uint32_t value) noexcept
{
    p[0] = static_cast<std::uint8_t>(value >> 24u);
    p[1] = static_cast<std::uint8_t>(value >> 16u);
    p[2] = static_cast<std::uint8_t>(value >> 8u);
    p[3] = static_cast<std::uint8_t>(value);
}

std::uint16_t load_u16be(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8u) |
                                      p[1]);
}

std::uint32_t load_u32be(const std::uint8_t* p) noexcept
{
    return (static_cast<std::uint32_t>(p[0]) << 24u) |
           (static_cast<std::uint32_t>(p[1]) << 16u) |
           (static_cast<std::uint32_t>(p[2]) << 8u) |
           static_cast<std::uint32_t>(p[3]);
}

bool wrap_record(std::uint8_t type, const std::uint8_t* body, std::size_t body_size,
                 std::vector<std::uint8_t>* out) noexcept
{
    if (out == nullptr || (body == nullptr && body_size != 0))
        return false;
    try
    {
        out->assign(4u + 1u + body_size, 0);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    put_u32be(out->data(), static_cast<std::uint32_t>(1u + body_size));
    (*out)[4] = type;
    if (body_size != 0)
        std::memcpy(out->data() + 5, body, body_size);
    return true;
}

} // namespace

bool encode_content_offer_v1(const ContentOfferDeclV1& decl,
                             std::vector<std::uint8_t>* out) noexcept
{
    std::uint8_t body[kContentOfferBodyBytesV1] = {};
    put_u16be(body, 1u);
    std::memcpy(body + 4, decl.offer_id.data(), 16);
    std::memcpy(body + 20, decl.content_id.data(), 32);
    put_u32be(body + 52, decl.declared_length);
    std::memcpy(body + 56, decl.payload_sha256.data(), 32);
    return wrap_record(kContentOfferTypeV1, body, sizeof(body), out);
}

bool decode_content_offer_v1(const std::uint8_t* body, std::size_t size,
                             ContentOfferDeclV1* out) noexcept
{
    if (body == nullptr || out == nullptr || size != kContentOfferBodyBytesV1)
        return false;
    if (load_u16be(body) != 1u)
        return false;
    if (body[2] != 0 || body[3] != 0)
        return false;
    std::memcpy(out->offer_id.data(), body + 4, 16);
    std::memcpy(out->content_id.data(), body + 20, 32);
    out->declared_length = load_u32be(body + 52);
    std::memcpy(out->payload_sha256.data(), body + 56, 32);
    return true;
}

bool encode_content_chunk_v1(const std::array<std::uint8_t, 16>& offer_id,
                             std::uint32_t offset, const std::uint8_t* data,
                             std::size_t size,
                             std::vector<std::uint8_t>* out) noexcept
{
    if (size > kContentChunkMaxPayloadV1)
        return false;
    if (data == nullptr && size != 0)
        return false;
    std::vector<std::uint8_t> body;
    try
    {
        body.assign(kContentChunkHeaderBytesV1 + size, 0);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    put_u16be(body.data(), 1u);
    std::memcpy(body.data() + 4, offer_id.data(), 16);
    put_u32be(body.data() + 20, offset);
    put_u32be(body.data() + 24, static_cast<std::uint32_t>(size));
    if (size != 0)
        std::memcpy(body.data() + kContentChunkHeaderBytesV1, data, size);
    return wrap_record(kContentChunkTypeV1, body.data(), body.size(), out);
}

bool decode_content_chunk_v1(const std::uint8_t* body, std::size_t size,
                             std::array<std::uint8_t, 16>* offer_id,
                             std::uint32_t* offset, const std::uint8_t** data,
                             std::size_t* payload_size) noexcept
{
    if (body == nullptr || offer_id == nullptr || offset == nullptr ||
        data == nullptr || payload_size == nullptr)
        return false;
    if (size < kContentChunkHeaderBytesV1)
        return false;
    if (load_u16be(body) != 1u || body[2] != 0 || body[3] != 0)
        return false;
    const auto declared = load_u32be(body + 24);
    if (size != kContentChunkHeaderBytesV1 + declared)
        return false;
    std::memcpy(offer_id->data(), body + 4, 16);
    *offset = load_u32be(body + 20);
    *payload_size = declared;
    *data = declared == 0 ? nullptr : body + kContentChunkHeaderBytesV1;
    return true;
}

} // namespace flynes::session::content
