#pragma once
#include "../../../../app/src/main/cpp/nearby/product_quic_port.hpp"

// Normalize only diagnostics for the existing integration scenario. Every port
// operation and every engine event still executes the actual Android adapter.
namespace flynes::session::quic_port {
class ProductQuicPort final {
public:
    auto port() { return impl_.port(); }
    bool ready() const { return impl_.ready(); }
    const char* provider_type() const { return impl_.provider_type(); }
    std::string listen_address() const { return impl_.listen_address(); }
    auto listen_endpoint() const { return impl_.listen_endpoint(); }
    auto material_handle() const { return impl_.material_handle(); }
    const auto& der_spki() const { return impl_.der_spki(); }
    auto bytes_written() const { return impl_.bytes_written(); }
    auto bytes_read() const { return impl_.bytes_read(); }
    int listens() const { return impl_.listens(); }
    int connects() const { return impl_.connects(); }
    int handshake_inspections() const { return impl_.handshake_inspections(); }
    int exporters() const { return impl_.exporters(); }
    int streams_opened() const { return impl_.streams_opened(); }
    int pending_ops() const { return impl_.pending_count(); }
    int drain() { return impl_.drain(); }
private:
    flynes::android::nearby::ProductQuicPort impl_;
};
}
