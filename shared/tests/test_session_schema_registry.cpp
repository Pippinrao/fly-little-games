#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void check(bool condition, const std::string& message)
{
    check(condition, message.c_str());
}

constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u, 0x3956C25Bu, 0x59F111F1u,
    0x923F82A4u, 0xAB1C5ED5u, 0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u, 0xE49B69C1u, 0xEFBE4786u,
    0x0FC19DC6u, 0x240CA1CCu, 0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u, 0xC6E00BF3u, 0xD5A79147u,
    0x06CA6351u, 0x14292967u, 0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u, 0xA2BFE8A1u, 0xA81A664Bu,
    0xC24B8B70u, 0xC76C51A3u, 0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u, 0x391C0CB3u, 0x4ED8AA4Au,
    0x5B9CCA4Fu, 0x682E6FF3u, 0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u,
};

std::uint32_t rotr(std::uint32_t value, unsigned int bits) noexcept
{
    return (value >> bits) | (value << (32u - bits));
}

void sha256_block(std::array<std::uint32_t, 8>& state, const std::uint8_t* block) noexcept
{
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16u; ++i)
    {
        words[i] = (static_cast<std::uint32_t>(block[i * 4u]) << 24u) |
                   (static_cast<std::uint32_t>(block[i * 4u + 1u]) << 16u) |
                   (static_cast<std::uint32_t>(block[i * 4u + 2u]) << 8u) |
                   static_cast<std::uint32_t>(block[i * 4u + 3u]);
    }
    for (std::size_t i = 16u; i < 64u; ++i)
    {
        const std::uint32_t s1 = rotr(words[i - 2u], 17u) ^ rotr(words[i - 2u], 19u) ^
                                 (words[i - 2u] >> 10u);
        const std::uint32_t s0 = rotr(words[i - 15u], 7u) ^ rotr(words[i - 15u], 18u) ^
                                 (words[i - 15u] >> 3u);
        words[i] = words[i - 16u] + s0 + words[i - 7u] + s1;
    }
    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];
    for (std::size_t i = 0; i < 64u; ++i)
    {
        const std::uint32_t s1 = rotr(e, 6u) ^ rotr(e, 11u) ^ rotr(e, 25u);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + ch + kSha256K[i] + words[i];
        const std::uint32_t s0 = rotr(a, 2u) ^ rotr(a, 13u) ^ rotr(a, 22u);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t size)
{
    std::array<std::uint32_t, 8> state = {
        0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
        0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u,
    };
    const std::size_t full = size / 64u;
    for (std::size_t i = 0; i < full; ++i)
        sha256_block(state, data + i * 64u);
    const std::size_t rem = size - full * 64u;
    std::array<std::uint8_t, 128> tail{};
    if (rem != 0u)
        std::memcpy(tail.data(), data + full * 64u, rem);
    tail[rem] = 0x80u;
    const std::size_t padded = rem < 56u ? 64u : 128u;
    const std::uint64_t bits = static_cast<std::uint64_t>(size) * 8u;
    for (std::size_t i = 0; i < 8u; ++i)
        tail[padded - 1u - i] = static_cast<std::uint8_t>(bits >> (i * 8u));
    sha256_block(state, tail.data());
    if (padded == 128u)
        sha256_block(state, tail.data() + 64u);
    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < 8u; ++i)
    {
        out[i * 4u] = static_cast<std::uint8_t>(state[i] >> 24u);
        out[i * 4u + 1u] = static_cast<std::uint8_t>(state[i] >> 16u);
        out[i * 4u + 2u] = static_cast<std::uint8_t>(state[i] >> 8u);
        out[i * 4u + 3u] = static_cast<std::uint8_t>(state[i]);
    }
    return out;
}

std::string to_hex_lower(const std::array<std::uint8_t, 32>& digest)
{
    static constexpr char hex[] = "0123456789abcdef";
    std::string out(64, '0');
    for (std::size_t i = 0; i < digest.size(); ++i)
    {
        out[i * 2u] = hex[digest[i] >> 4u];
        out[i * 2u + 1u] = hex[digest[i] & 0x0Fu];
    }
    return out;
}

bool read_all(const std::string& path, std::vector<std::uint8_t>* out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end < 0)
        return false;
    in.seekg(0, std::ios::beg);
    out->assign(static_cast<std::size_t>(end), 0);
    if (end != 0 && !in.read(reinterpret_cast<char*>(out->data()), end))
        return false;
    return true;
}

std::string as_text(const std::vector<std::uint8_t>& bytes)
{
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::string join_path(const char* root, const char* relative)
{
    std::string path(root);
    if (!path.empty() && path.back() != '/' && path.back() != '\\')
        path.push_back('/');
    path.append(relative);
    for (char& ch : path)
    {
        if (ch == '\\')
            ch = '/';
    }
    return path;
}

constexpr const char* kRequiredKinds[] = {
    "0x0001", "RUNTIME_CHECKPOINT_V1",
    "0x0002", "SESSION_CHECKPOINT_V1",
    "0x0003", "SRAM_BLOB_V1",
    "0x0004", "CANONICAL_INPUT_COMPONENT_V1",
    "0x0005", "CANONICAL_INPUT_COMPONENT_MANIFEST_V1",
    "0x0006", "CANONICAL_INPUT_COMPONENT_CHUNK_V1",
    "0x0007", "SRAM_JOURNAL_COMPONENT_V1",
    "0x0008", "INPUT_RESERVATION_COMPONENT_V1",
    "0x0009", "EPOCH_PRIME_COMPONENT_V1",
    "0x000a", "TRANSITION_OPERATION_PAYLOAD_V1",
    "0x000b", "TRANSITION_RGBA8888_FRAME_V1",
    "0x000c", "USER_SAVE_PAYLOAD_V1",
    "0x000d", "REJECTED_DEADLINE_RAW_V1",
    "0x000e", "QUARANTINE_RETAINED_BLOB_V1",
    "0x0101", "RECOVERY_POINT_V1",
    "0x0102", "AUTHORITY_RECOVERY_HEAD_V1",
    "0x0103", "REPLAYABLE_PROOF_V1",
    "0x0104", "ACTIVE_SESSION_MANIFEST_V1",
    "0x0105", "SESSION_CONFIG_MANIFEST_V1",
    "0x0106", "BARRIER_RECOVERY_REF_V1",
    "0x0107", "TRANSITION_PACKAGE_V1",
    "0x0108", "SOURCE_PROGRESS_REF_V1",
    "0x0109", "OFFLINE_PROGRESS_POINT_V1",
    "0x010a", "SESSION_START_PACKAGE_V1",
    "0x010b", "FORK_HANDOFF_PACKAGE_V1",
    "0x010c", "RECOVERY_PACKAGE_V1",
    "0x010d", "USER_SAVE_ORIGIN_MANIFEST_V1",
    "0x010e", "QUARANTINE_MANIFEST_V1",
    "0x010f", "END_CLOSURE_MANIFEST_V1",
    "0x0110", "PUBLISHED_SAVE_INDEX_V1",
    "0x0111", "OFFLINE_SAVE_MANIFEST_V1",
    "0x0112", "OPEN_INPUT_RESERVATION_SET_V1",
    "0x0201", "DUAL_RUN_FENCE_V1",
    "0x0202", "INPUT_SEQUENCE_LEDGER_V1",
    "0x0203", "INPUT_SEQUENCE_RESERVATION_V1",
    "0x0204", "SESSION_WAL_RECORD_V1",
    "0x0205", "RECONNECT_DEADLINE_RECORD_V1",
    "0x0206", "PREPREPARE_TERMINAL_EVIDENCE_V1",
    "0x0207", "TERMINAL_RECOVERY_EVIDENCE_V1",
    "0x0208", "OPAQUE_RECOVERY_FAILURE_EVIDENCE_V1",
    "0x0209", "IDENTITY_VERIFIER_REF_V1",
    "0x020a", "INPUT_RESERVATION_GRANT_V1",
    "0x020b", "INPUT_RESERVATION_ACK_V1",
    "0x020c", "INPUT_RESERVATION_FINALIZED_V1",
    "0x020d", "INPUT_RESERVATION_REQUEST_V1",
    "0x020e", "PRIME_AUTHORIZATION_V1",
    "0x020f", "EPOCH_INPUT_CLOSE_CERTIFICATE_V1",
    "0x0210", "SUSPEND_INTENT_V1",
    "0x0211", "INPUT_RANGE_AUTHORIZATION_V1",
    "0x0212", "SESSION_SIGNING_KEY_BINDING_V1",
    "0x0213", "PAIR_TRANSCRIPT_V1",
    "0x0301", "OFFLINE_RELEASE_CERTIFICATE_V1",
    "0x0302", "FORK_COMMIT_DECISION_V1",
    "0x0303", "SAVE_COMMIT_CERTIFICATE_V1",
    "0x0304", "SAVE_COMMIT_DECISION_V1",
    "0x0305", "SAVE_PUBLICATION_CERTIFICATE_V1",
    "0x0306", "END_PACKAGE_V1",
    "0x0307", "END_COMMIT_DECISION_V1",
    "0x0308", "END_COMMIT_CERTIFICATE_V1",
};

constexpr const char* kRequiredDomains[] = {
    "flynes-session-schema-registry-v1",
    "flynes-identity-verifier-ref-v1",
    "flynes-input-bundle-v1",
    "flynes-dual-run-fence-v1",
    "flynes-input-sequence-ledger-v1",
    "flynes-pair-transcript-object-v1",
    "flynes-end-closure-manifest-v1",
    "flynes-published-save-index-v1",
    "flynes-channel-bind-proof-hash-v1",
};

std::array<std::uint8_t, 32> registry_hash(const std::vector<std::uint8_t>& schema)
{
    static constexpr char domain[] = "flynes-session-schema-registry-v1";
    std::vector<std::uint8_t> preimage;
    preimage.insert(preimage.end(), domain, domain + (sizeof(domain) - 1u));
    const std::uint32_t len = static_cast<std::uint32_t>(schema.size());
    preimage.push_back(static_cast<std::uint8_t>(len >> 24u));
    preimage.push_back(static_cast<std::uint8_t>(len >> 16u));
    preimage.push_back(static_cast<std::uint8_t>(len >> 8u));
    preimage.push_back(static_cast<std::uint8_t>(len));
    preimage.insert(preimage.end(), schema.begin(), schema.end());
    return sha256(preimage.data(), preimage.size());
}

} // namespace

int main()
{
    const std::string schema_path =
        join_path(FLYNES_SESSION_SCHEMA_DIR, "flynes_session_v1.schema");
    const std::string hash_path =
        join_path(FLYNES_SESSION_SCHEMA_DIR, "schema_registry_hash.txt");

    std::vector<std::uint8_t> schema;
    check(read_all(schema_path, &schema),
          "shared/schema/flynes_session_v1.schema must exist");
    if (schema.empty() && failures != 0)
    {
        std::fprintf(stderr, "flynes_session_schema_registry: FAIL\n");
        return 1;
    }

    bool has_cr = false;
    for (std::uint8_t byte : schema)
    {
        if (byte == '\r')
            has_cr = true;
    }
    check(!has_cr, "schema file bytes must be LF-normalized");

    std::vector<std::uint8_t> hash_file;
    check(read_all(hash_path, &hash_file),
          "shared/schema/schema_registry_hash.txt must exist");
    std::string published = as_text(hash_file);
    while (!published.empty() && (published.back() == '\n' || published.back() == '\r' ||
                                  published.back() == ' '))
        published.pop_back();
    for (char& ch : published)
    {
        if (ch >= 'A' && ch <= 'F')
            ch = static_cast<char>(ch - 'A' + 'a');
    }

    const std::string recomputed = to_hex_lower(registry_hash(schema));
    check(published == recomputed,
          "schema_registry_hash must equal SHA256(domain||u32be(len)||exact_schema)");

    const char* copies[][2] = {
        {FLYNES_ANDROID_SESSION_SCHEMA_DIR, "Android JVM schema copy"},
        {FLYNES_IOS_SESSION_SCHEMA_DIR, "iOS ABI schema copy"},
        {FLYNES_HARMONY_SESSION_SCHEMA_DIR, "Harmony schema copy"},
    };
    for (const auto& copy : copies)
    {
        std::vector<std::uint8_t> other_schema;
        std::vector<std::uint8_t> other_hash;
        const std::string other_schema_path =
            join_path(copy[0], "flynes_session_v1.schema");
        const std::string other_hash_path =
            join_path(copy[0], "schema_registry_hash.txt");
        check(read_all(other_schema_path, &other_schema),
              std::string(copy[1]) + " flynes_session_v1.schema must exist");
        check(read_all(other_hash_path, &other_hash),
              std::string(copy[1]) + " schema_registry_hash.txt must exist");
        check(other_schema == schema,
              std::string(copy[1]) + " schema must be byte-identical");
        check(other_hash == hash_file,
              std::string(copy[1]) + " hash file must be byte-identical");
    }

    const std::string text = as_text(schema);
    check(text.find("\"schema_id\":\"flynes_session_v1\"") != std::string::npos,
          "schema must declare flynes_session_v1");
    check(text.find("\"wire_endian\":\"network\"") != std::string::npos,
          "schema must freeze network endian");
    check(text.find("0x0000") != std::string::npos &&
              text.find("\"illegal\"") != std::string::npos,
          "schema must mark 0x0000 illegal");
    check(text.find("flynes_session.h") == std::string::npos,
          "schema artifact must not depend on flynes_session.h");

    for (std::size_t i = 0; i + 1u < sizeof(kRequiredKinds) / sizeof(kRequiredKinds[0]); i += 2)
    {
        check(text.find(kRequiredKinds[i]) != std::string::npos,
              std::string("schema missing kind ") + kRequiredKinds[i]);
        check(text.find(kRequiredKinds[i + 1u]) != std::string::npos,
              std::string("schema missing name ") + kRequiredKinds[i + 1u]);
    }
    for (const char* domain : kRequiredDomains)
    {
        check(text.find(domain) != std::string::npos,
              std::string("schema missing hash domain ") + domain);
    }

    if (failures != 0)
    {
        std::fprintf(stderr, "flynes_session_schema_registry: FAIL (%d)\n", failures);
        return 1;
    }
    std::puts("flynes_session_schema_registry: PASS");
    return 0;
}
