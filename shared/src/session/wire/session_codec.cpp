#include "session_codec.hpp"
#include "p256_point.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace flynes::session::wire {
namespace {

std::uint16_t be16(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

std::uint64_t be64(const std::uint8_t* p) noexcept
{
    std::uint64_t value = 0u;
    for (int i = 0; i < 8; ++i)
        value = (value << 8u) | p[i];
    return value;
}

bool zeros(const std::uint8_t* p, std::size_t n) noexcept
{
    for (std::size_t i = 0; i < n; ++i)
    {
        if (p[i] != 0u)
            return false;
    }
    return true;
}

bool canonical_p256_low_s(const std::uint8_t signature[64]) noexcept
{
    static constexpr std::array<std::uint8_t, 32> order{{
        0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
        0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,
        0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51}};
    static constexpr std::array<std::uint8_t, 32> half_order{{
        0x7f,0xff,0xff,0xff,0x80,0x00,0x00,0x00,
        0x7f,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        0xde,0x73,0x7d,0x56,0xd3,0x8b,0xcf,0x42,
        0x79,0xdc,0xe5,0x61,0x7e,0x31,0x92,0xa8}};
    return !zeros(signature, 32) &&
           std::memcmp(signature, order.data(), 32) < 0 &&
           !zeros(signature + 32, 32) &&
           std::memcmp(signature + 32, half_order.data(), 32) <= 0;
}

Status validate_identity_verifier_ref(const std::uint8_t* bytes) noexcept
{
    if (be16(bytes) != 1u)
        return Status::InvalidField;
    if (!zeros(bytes + 2, 6) || !zeros(bytes + 105, 7))
        return Status::NonzeroReserved;
    if (!validate_p256_uncompressed_point(bytes + 40))
        return Status::InvalidField;
    std::array<std::uint8_t, 32> expected{};
    const auto key_id = domain_hash(
        "flynes-identity-key-id-v1", bytes + 40, 65);
    std::copy(key_id.begin(), key_id.end(), expected.begin());
    return std::memcmp(bytes + 8, expected.data(), expected.size()) == 0
        ? Status::Ok : Status::InvalidField;
}

Status validate_session_signing_binding(const std::uint8_t* bytes) noexcept
{
    if (zeros(bytes + 8, 32) || zeros(bytes + 40, 16))
        return Status::InvalidField;
    if (!zeros(bytes + 57, 7) || !zeros(bytes + 241, 7))
        return Status::NonzeroReserved;
    const auto identity = validate_identity_verifier_ref(bytes + 64);
    if (identity != Status::Ok) return identity;
    if (!validate_p256_uncompressed_point(bytes + 176) ||
        std::memcmp(bytes + 104, bytes + 176, 65) == 0 ||
        !canonical_p256_low_s(bytes + 248))
        return Status::InvalidField;
    return Status::Ok;
}

Status length_status(std::size_t size, std::size_t expected) noexcept
{
    if (size == expected)
        return Status::Ok;
    if (size < expected)
        return Status::Truncated;
    return Status::Trailing;
}

void write_hash(const char* domain, const std::uint8_t* bytes, std::size_t size,
                std::uint8_t out[32])
{
    const auto digest = domain && domain[0] != '\0'
                            ? domain_hash(domain, bytes, size)
                            : sha256(bytes, size);
    std::memcpy(out, digest.data(), 32u);
}

Status require_version_reserved(const std::uint8_t* bytes, std::size_t size) noexcept
{
    if (size < 8u)
        return Status::Truncated;
    if (be16(bytes) != 1u)
        return Status::InvalidField;
    if (!zeros(bytes + 2, 6u))
        return Status::NonzeroReserved;
    return Status::Ok;
}

Status check_fixed(const std::uint8_t* bytes, std::size_t size, std::size_t expected,
                   const char* domain, std::uint8_t hash_out[32],
                   const std::size_t* enum_offsets, const std::uint8_t* enum_max,
                   std::size_t enum_count)
{
    const Status ls = length_status(size, expected);
    if (ls != Status::Ok)
        return ls;
    const Status vs = require_version_reserved(bytes, size);
    if (vs != Status::Ok)
        return vs;
    for (std::size_t i = 0; i < enum_count; ++i)
    {
        const std::size_t off = enum_offsets[i];
        if (off >= size)
            return Status::InvalidField;
        if (bytes[off] < 1u || bytes[off] > enum_max[i])
            return Status::UnknownEnum;
    }
    write_hash(domain, bytes, size, hash_out);
    return Status::Ok;
}

Status check_pending_config_confirm(const std::uint8_t* bytes, std::size_t size,
                                    std::uint8_t hash_out[32])
{
    const Status ls = length_status(size, 44u);
    if (ls != Status::Ok)
        return ls;
    if (be16(bytes) != 1u)
        return Status::InvalidField;
    if (!zeros(bytes + 2, 2u))
        return Status::NonzeroReserved;
    if (zeros(bytes + 4, 32u))
        return Status::InvalidField;
    if (be64(bytes + 36) == 0u)
        return Status::InvalidField;
    write_hash("flynes-pending-config-confirm-v1", bytes, size, hash_out);
    return Status::Ok;
}

Status check_frame_cursor(const std::uint8_t* bytes, std::size_t size,
                          std::uint8_t hash_out[32])
{
    const Status ls = length_status(size, 16u);
    if (ls != Status::Ok)
        return ls;
    if (bytes[0] > 1u)
        return Status::UnknownEnum;
    if (!zeros(bytes + 1, 7u))
        return Status::NonzeroReserved;
    if (bytes[0] == 0u)
    {
        for (std::size_t i = 8; i < 16u; ++i)
        {
            if (bytes[i] != 0u)
                return Status::InvalidField;
        }
    }
    write_hash("", bytes, size, hash_out);
    return Status::Ok;
}

Status check_evidence(const std::uint8_t* bytes, std::size_t size, std::uint8_t hash_out[32])
{
    if (size == 0u)
        return Status::Truncated;
    if (bytes[0] > 1u)
        return Status::UnknownEnum;
    const std::size_t expected = bytes[0] == 0u ? 8u : 64u;
    const Status ls = length_status(size, expected);
    if (ls != Status::Ok)
        return ls;
    if (!zeros(bytes + 1, 7u))
        return Status::NonzeroReserved;
    if (bytes[0] == 1u)
    {
        const Status cursor = check_frame_cursor(bytes + 16, 16u, hash_out);
        if (cursor != Status::Ok)
            return cursor;
    }
    write_hash("", bytes, size, hash_out);
    return Status::Ok;
}

Status check_bundle(const std::uint8_t* bytes, std::size_t size, std::uint8_t hash_out[32])
{
    const Status ls = length_status(size, 154u);
    if (ls != Status::Ok)
        return ls;
    if (be16(bytes) != 1u)
        return Status::InvalidField;
    if (!zeros(bytes + 83, 7u))
        return Status::NonzeroReserved;
    const std::uint8_t mask = bytes[82];
    if ((mask & 0xF0u) != 0u)
        return Status::InvalidField;
    for (int port = 0; port < 4; ++port)
    {
        const std::uint8_t* slot = bytes + 90 + port * 16;
        const std::uint8_t source = slot[0];
        if (source < 1u || source > 7u)
            return Status::UnknownEnum;
        const std::uint8_t reason = slot[1];
        if (reason > 4u)
            return Status::UnknownEnum;
        if (slot[2] != 0u || slot[3] != 0u)
            return Status::NonzeroReserved;
        const bool predicted = source == 5u;
        if (((mask >> port) & 1u) != (predicted ? 1u : 0u))
            return Status::InvalidField;
        const std::uint32_t buttons = (static_cast<std::uint32_t>(slot[4]) << 24u) |
                                      (static_cast<std::uint32_t>(slot[5]) << 16u) |
                                      (static_cast<std::uint32_t>(slot[6]) << 8u) |
                                      slot[7];
        if ((buttons & 0xFFFFFF00u) != 0u)
            return Status::InvalidField;
        if ((buttons & 0x30u) == 0x30u || (buttons & 0xC0u) == 0xC0u)
            return Status::InvalidField;
    }
    std::uint8_t preimage_prefix[1 + 22 + 4];
    preimage_prefix[0] = 0x10u;
    std::memcpy(preimage_prefix + 1, "flynes-input-bundle-v1", 22u);
    preimage_prefix[23] = static_cast<std::uint8_t>(size >> 24u);
    preimage_prefix[24] = static_cast<std::uint8_t>(size >> 16u);
    preimage_prefix[25] = static_cast<std::uint8_t>(size >> 8u);
    preimage_prefix[26] = static_cast<std::uint8_t>(size);
    std::vector<std::uint8_t> pre(27u + size);
    std::memcpy(pre.data(), preimage_prefix, 27u);
    std::memcpy(pre.data() + 27u, bytes, size);
    const auto digest = sha256(pre.data(), pre.size());
    std::memcpy(hash_out, digest.data(), 32u);
    return Status::Ok;
}

Status check_channel_bind(const std::uint8_t* bytes, std::size_t size, std::uint8_t hash_out[32])
{
    const Status ls = length_status(size, 136u);
    if (ls != Status::Ok)
        return ls;
    const Status vs = require_version_reserved(bytes, size);
    if (vs != Status::Ok)
        return vs;
    const std::uint8_t kind = bytes[8];
    if (kind < 1u || kind > 3u)
        return Status::UnknownEnum;
    if (!zeros(bytes + 9, 7u))
        return Status::NonzeroReserved;
    if (kind == 1u)
    {
        if (!zeros(bytes + 64, 32u))
            return Status::InvalidField;
        if (!zeros(bytes + 96, 8u))
            return Status::InvalidField;
        std::uint8_t pre[22 + 32];
        std::memcpy(pre, "flynes-initial-bind-v1", 22u);
        std::memcpy(pre + 22, bytes + 16, 32u);
        const auto nonce = sha256(pre, sizeof(pre));
        if (std::memcmp(bytes + 120, nonce.data(), 16u) != 0)
            return Status::InvalidField;
    }
    write_hash("", bytes, size, hash_out);
    return Status::Ok;
}

Status check_end_package(const std::uint8_t* bytes, std::size_t size, std::uint8_t hash_out[32])
{
    if (size < 4u)
        return Status::Truncated;
    if (be16(bytes) != 1u)
        return Status::InvalidField;
    const std::uint16_t count = be16(bytes + 2);
    std::size_t offset = 4u;
    std::uint16_t prev = 0u;
    for (std::uint16_t i = 0; i < count; ++i)
    {
        if (offset + 6u > size)
            return Status::Truncated;
        const std::uint16_t tag = be16(bytes + offset);
        const std::uint32_t length = (static_cast<std::uint32_t>(bytes[offset + 2]) << 24u) |
                                     (static_cast<std::uint32_t>(bytes[offset + 3]) << 16u) |
                                     (static_cast<std::uint32_t>(bytes[offset + 4]) << 8u) |
                                     bytes[offset + 5];
        if (tag < 1u || tag > 23u)
            return Status::UnknownCriticalTag;
        if (tag <= prev)
            return Status::InvalidField;
        prev = tag;
        offset += 6u;
        if (offset + length > size)
            return Status::Truncated;
        offset += length;
    }
    if (offset < size)
        return Status::Trailing;
    write_hash("flynes-end-package-v1", bytes, size, hash_out);
    return Status::Ok;
}

Status check_counted(const std::uint8_t* bytes, std::size_t size, std::size_t header,
                     std::size_t entry, std::size_t count_off, std::size_t max_count,
                     const char* domain, std::uint8_t hash_out[32])
{
    if (size < header)
        return Status::Truncated;
    const Status vs = require_version_reserved(bytes, size);
    if (vs != Status::Ok)
        return vs;
    const std::uint32_t count = (static_cast<std::uint32_t>(bytes[count_off]) << 24u) |
                                (static_cast<std::uint32_t>(bytes[count_off + 1u]) << 16u) |
                                (static_cast<std::uint32_t>(bytes[count_off + 2u]) << 8u) |
                                bytes[count_off + 3u];
    if (count > max_count)
        return Status::InvalidField;
    const std::size_t expected = header + entry * static_cast<std::size_t>(count);
    const Status ls = length_status(size, expected);
    if (ls != Status::Ok)
        return ls;
    write_hash(domain, bytes, size, hash_out);
    return Status::Ok;
}

} // namespace

Status encode_frame_cursor(std::uint8_t kind, std::uint64_t index, std::uint8_t out[16],
                           std::size_t* written)
{
    if (out == nullptr || written == nullptr)
        return Status::InvalidField;
    if (kind > 1u)
        return Status::UnknownEnum;
    if (kind == 0u && index != 0u)
        return Status::InvalidField;
    std::memset(out, 0, 16u);
    out[0] = kind;
    for (int i = 0; i < 8; ++i)
        out[15 - i] = static_cast<std::uint8_t>(index >> (i * 8));
    *written = 16u;
    return Status::Ok;
}

Status check(const char* type_name, const std::uint8_t* bytes, std::size_t size,
             std::uint8_t hash_out[32])
{
    if (type_name == nullptr || (bytes == nullptr && size != 0u) || hash_out == nullptr)
        return Status::InvalidField;
    const std::string name(type_name);

    if (name == "FrameCursorV1")
        return check_frame_cursor(bytes, size, hash_out);
    if (name == "EvidenceCursorV1")
        return check_evidence(bytes, size, hash_out);
    if (name == "CanonicalInputBundleV1")
        return check_bundle(bytes, size, hash_out);
    if (name == "ChannelBindV1")
        return check_channel_bind(bytes, size, hash_out);
    if (name == "ChannelResumeSummaryV1")
    {
        const std::size_t off[] = {88};
        const std::uint8_t mx[] = {2};
        return check_fixed(bytes, size, 176u, "flynes-channel-resume-summary-v1", hash_out, off, mx, 1);
    }
    if (name == "0x0306")
        return check_end_package(bytes, size, hash_out);
    if (name == "0x010f")
        return check_counted(bytes, size, 220u, 36u, 216u, 32u,
                             "flynes-end-closure-manifest-v1", hash_out);
    if (name == "0x0110")
        return check_counted(bytes, size, 80u, 40u, 72u, 1024u,
                             "flynes-published-save-index-v1", hash_out);
    if (name == "0x0112")
        return check_counted(bytes, size, 104u, 184u, 96u, 8u,
                             "flynes-open-input-reservation-set-v1", hash_out);
    if (name == "0x000b")
    {
        const Status ls = length_status(size, 245760u);
        if (ls != Status::Ok)
            return ls;
        write_hash("", bytes, size, hash_out);
        return Status::Ok;
    }
    if (name == "0x0218")
        return check_pending_config_confirm(bytes, size, hash_out);

    struct FixedKind
    {
        const char* name;
        std::size_t length;
        const char* domain;
        std::uint16_t enum_off;
        std::uint8_t enum_max;
        bool has_enum;
    };
    static const FixedKind kFixed[] = {
        {"0x0201", 232u, "flynes-dual-run-fence-v1", 81u, 2u, true},
        {"0x0202", 120u, "flynes-input-sequence-ledger-v1", 0u, 0u, false},
        {"0x0203", 144u, "flynes-input-sequence-reservation-v1", 25u, 4u, true},
        {"0x0205", 144u, "flynes-reconnect-deadline-record-v1", 0u, 0u, false},
        {"0x0206", 272u, "flynes-preprepare-terminal-evidence-v1", 264u, 4u, true},
        {"0x0207", 344u, "flynes-terminal-recovery-evidence-v1", 328u, 8u, true},
        {"0x0208", 192u, "flynes-opaque-recovery-failure-evidence-v1", 176u, 8u, true},
        {"0x0209", 112u, "flynes-identity-verifier-ref-v1", 0u, 0u, false},
        {"0x020a", 312u, "flynes-input-sequence-reserve-grant-v1", 0u, 0u, false},
        {"0x020b", 192u, "flynes-input-sequence-reserve-ack-v1", 0u, 0u, false},
        {"0x020c", 248u, "flynes-input-sequence-reserve-finalized-v1", 0u, 0u, false},
        {"0x020d", 184u, "flynes-input-sequence-reserve-req-v1", 25u, 4u, true},
        {"0x020e", 240u, "flynes-prime-authorization-v1", 8u, 4u, true},
        {"0x020f", 296u, "flynes-epoch-input-close-certificate-v1", 65u, 3u, true},
        {"0x0210", 240u, "flynes-suspend-intent-v1", 0u, 0u, false},
        {"0x0211", 336u, "flynes-input-range-authorization-v1", 8u, 3u, true},
        {"0x0212", 312u, "flynes-session-signing-key-binding-hash-v1", 56u, 2u, true},
        {"0x0213", 880u, "flynes-pair-transcript-object-v1", 8u, 2u, true},
        // Link control plane. The enum byte is the phase discriminant at offset
        // 10 in both messages (INITIAL=1 / RECONCILE=2); the ready_phase
        // discriminant at offset 11 is checked by the link_ready codec, which is
        // the authority for this object, exactly as validate_session_signing_
        // binding is for 0x0212.
        {"0x0216", 488u, "flynes-link-hello-object-v1", 10u, 2u, true},
        {"0x0217", 432u, "flynes-link-ready-object-v1", 10u, 2u, true},
        {"0x0301", 312u, "flynes-offline-release-certificate-hash-v1", 0u, 0u, false},
        {"0x0303", 392u, "flynes-save-commit-certificate-hash-v1", 0u, 0u, false},
        {"0x0304", 208u, "flynes-save-commit-decision-v1", 24u, 2u, true},
        {"0x0305", 400u, "flynes-save-publication-certificate-hash-v1", 296u, 2u, true},
        {"0x0307", 360u, "flynes-end-commit-decision-v1", 352u, 3u, true},
        {"0x0308", 392u, "flynes-end-commit-certificate-hash-v1", 0u, 0u, false},
    };
    for (const FixedKind& kind : kFixed)
    {
        if (name == kind.name)
        {
            Status fixed = Status::Ok;
            if (kind.has_enum)
            {
                const std::size_t off[] = {kind.enum_off};
                const std::uint8_t mx[] = {kind.enum_max};
                fixed = check_fixed(bytes, size, kind.length, kind.domain,
                                    hash_out, off, mx, 1);
            }
            else
                fixed = check_fixed(bytes, size, kind.length, kind.domain,
                                    hash_out, nullptr, nullptr, 0);
            if (fixed != Status::Ok)
                return fixed;
            if (name == "0x0212")
                return validate_session_signing_binding(bytes);
            if (name != "0x0209") return Status::Ok;
            if (bytes[40] != 0x04u)
                return Status::InvalidField;
            std::uint8_t key_pre[4 + 21 + 4 + 65];
            std::memcpy(key_pre, "flynes-identity-key-id-v1", 25u);
            key_pre[25] = 0u;
            key_pre[26] = 0u;
            key_pre[27] = 0u;
            key_pre[28] = 65u;
            std::memcpy(key_pre + 29, bytes + 40, 65u);
            const auto key_id = sha256(key_pre, 29u + 65u);
            if (std::memcmp(bytes + 8, key_id.data(), 32u) != 0)
                return Status::InvalidField;
            return Status::Ok;
        }
    }
    return Status::UnknownKind;
}

} // namespace flynes::session::wire
