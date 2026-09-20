#pragma once
#include <flynes/flynes_session.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace flynes::android::nearby {
// Internal Android engine adapter, not a generic ABI provider. The engine only
// cancels its current unconsumed producer (see crypto ownership regression).
class CryptoPort final {
public:
    struct Context;
    enum class Operation { Random, Hkdf, Seal, Open, Verify, Hmac };
    struct Secret {
        virtual ~Secret() = default;
        virtual void close() noexcept = 0; // worker only; must not export bytes
    };
    struct Request {
        // Valid for execute's duration; its Job owns the retained context.
        // Backend may use this for worker-only adoption, never an owner pointer.
        Context* context = nullptr;
        Operation operation = Operation::Random;
        std::uint32_t count = 0;
        // Random: purpose; HKDF: salt/info; AEAD: nonce/aad/input;
        // verify: public/domain/digest/signature; HMAC: exact input.
        std::array<std::vector<std::uint8_t>, 4> inputs;
    };
    struct Result {
        fly_session_result_v2 result = FLY_SESSION_V2_UNAVAILABLE;
        std::vector<std::uint8_t> bytes;
        std::unique_ptr<Secret> secret;
    };
    struct Backend {
        virtual ~Backend() = default;
        virtual Result execute(const Request&, Secret*) = 0; // worker only
    };
    static constexpr std::size_t kMaximumJobs = 16;
    static constexpr std::size_t kMaximumBytes = 4 * 1024 * 1024;
    static constexpr std::size_t kMaximumSecrets = 64;

    explicit CryptoPort(std::shared_ptr<Backend> backend);
    ~CryptoPort();
    CryptoPort(const CryptoPort&) = delete;
    CryptoPort& operator=(const CryptoPort&) = delete;
    fly_session_crypto_port_v2 port() const noexcept;
    void close() noexcept;
    // Worker-only future-KEY boundary: genuine opaque SecretHandle, never raw
    // bytes. Transfers ownership on OK only; caller closes failures on worker.
    static fly_session_result_v2 adopt(Context*, std::unique_ptr<Secret>&, std::uint32_t byte_length,
                               const fly_session_op_token_v2& producer,
                               fly_session_resource_handle_v2* out);
private:
    Context* context_;
};
}
