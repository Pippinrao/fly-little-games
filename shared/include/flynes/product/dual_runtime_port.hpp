#ifndef FLYNES_PRODUCT_DUAL_RUNTIME_PORT_HPP
#define FLYNES_PRODUCT_DUAL_RUNTIME_PORT_HPP

#include <flynes/flynes_runtime.h>
#include <flynes/flynes_session.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace flynes::product {

struct AuthorizedRom final
{
    fly_session_result_v2 result = FLY_SESSION_V2_PERMISSION_DENIED;
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
};

// Resolve only the owner's currently authorized source for this complete
// reference. A matching hash is an integrity check, never content authorization.
using AuthorizedContentResolver =
    std::function<AuthorizedRom(const fly_session_dual_content_ref_v2&)>;

// A serialized simulation-worker provider borrowing the owner's sole DUAL
// runtime. It never creates/destroys that runtime, starts a thread, or consumes
// PCM. The existing view may copy video and is the sole PCM consumer; only the
// engine may step/load/restore. Before destroying runtime, the owner must drain
// and destroy the engine and release every retained table plus this wrapper.
// port() is borrowed; retain its context before keeping it beyond this wrapper.
//
// export_state requires a successful step since load/import. Its opaque product
// checkpoint is a 96-byte header followed by the complete runtime checkpoint:
// "FLYDUAL1" (8), version=1/reserved=0 (two u32 LE), session (16), branch (16),
// content hash (32), epoch/payload length (two u64 LE). The returned SHA-256
// covers that entire envelope. import validates this binding and asks runtime
// to atomically validate the opaque payload's epoch before loading it. A peer
// instance with the same binding may import; subsequent digest/export require
// another successful step. This adds no session wire message or authorization.
class ProductDualRuntimePort final
{
public:
    ProductDualRuntimePort(fly_runtime_t* borrowed_runtime,
                          AuthorizedContentResolver resolver);
    ~ProductDualRuntimePort();
    ProductDualRuntimePort(const ProductDualRuntimePort&) = delete;
    ProductDualRuntimePort& operator=(const ProductDualRuntimePort&) = delete;
    fly_session_dual_runtime_port_v2 port() const noexcept;

private:
    struct Context;
    Context* context_;
};

} // namespace flynes::product
#endif
