#ifndef FLYNES_SESSION_PORTS_SESSION_PORTS_HPP
#define FLYNES_SESSION_PORTS_SESSION_PORTS_HPP

#include <flynes/flynes_session.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace flynes::session {

class SessionPorts final
{
public:
    explicit SessionPorts(const fly_session_ports_v2& ports) noexcept
        : clock_(*ports.clock),
          executor_(*ports.executor),
          platform_state_(*ports.platform_state),
          has_camera_(ports.camera != nullptr)
    {
        if (has_camera_)
        {
            camera_ = *ports.camera;
        }
        capture_optional(ports, offsetof(fly_session_ports_v2, key), key_, has_key_);
        capture_optional(ports, offsetof(fly_session_ports_v2, crypto), crypto_, has_crypto_);
        capture_optional(ports, offsetof(fly_session_ports_v2, tls_material), tls_, has_tls_);
        capture_optional(ports, offsetof(fly_session_ports_v2, secure_store),
                         secure_store_, has_secure_store_);
        capture_optional(ports, offsetof(fly_session_ports_v2, quic), quic_, has_quic_);
        capture_optional(ports, offsetof(fly_session_ports_v2, discovery),
                         discovery_, has_discovery_);
        capture_optional(ports, offsetof(fly_session_ports_v2, bearer),
                         bearer_, has_bearer_);
        capture_optional(ports, offsetof(fly_session_ports_v2, object_store),
                         object_store_, has_object_store_);
        capture_optional(ports, offsetof(fly_session_ports_v2, dual_runtime),
                         dual_runtime_, has_dual_runtime_);
        capture_optional(ports, offsetof(fly_session_ports_v2, content),
                         content_, has_content_);
        clock_.retain(clock_.context);
        executor_.retain(executor_.context);
        platform_state_.retain(platform_state_.context);
        if (has_camera_)
        {
            camera_.retain(camera_.context);
        }
        retain_optional(key_, has_key_);
        retain_optional(crypto_, has_crypto_);
        retain_optional(tls_, has_tls_);
        retain_optional(secure_store_, has_secure_store_);
        retain_optional(quic_, has_quic_);
        retain_optional(discovery_, has_discovery_);
        retain_optional(bearer_, has_bearer_);
        retain_optional(object_store_, has_object_store_);
        retain_optional(dual_runtime_, has_dual_runtime_);
        retain_optional(content_, has_content_);
    }

    SessionPorts(const SessionPorts&) = delete;
    SessionPorts& operator=(const SessionPorts&) = delete;

    ~SessionPorts()
    {
        release_optional(content_, has_content_);
        release_optional(dual_runtime_, has_dual_runtime_);
        release_optional(object_store_, has_object_store_);
        release_optional(bearer_, has_bearer_);
        release_optional(discovery_, has_discovery_);
        release_optional(quic_, has_quic_);
        release_optional(secure_store_, has_secure_store_);
        release_optional(tls_, has_tls_);
        release_optional(crypto_, has_crypto_);
        release_optional(key_, has_key_);
        if (has_camera_)
        {
            camera_.release(camera_.context);
        }
        platform_state_.release(platform_state_.context);
        executor_.release(executor_.context);
        clock_.release(clock_.context);
    }

    fly_session_result_v2 post(fly_session_task_v2_t* task) const
    {
        return executor_.post(executor_.context, task);
    }

    fly_session_result_v2 watch_platform_state(
        const fly_session_op_token_v2* token,
        fly_session_inbox_v2_t* inbox) const
    {
        return platform_state_.watch(platform_state_.context, token, inbox);
    }

    fly_session_result_v2 stop_platform_state(
        const fly_session_op_token_v2* token) const
    {
        return platform_state_.stop(platform_state_.context, token);
    }

    fly_session_result_v2 read_clock(fly_session_clock_sample_v2* sample) const
    {
        return clock_.read_continuous(clock_.context, sample);
    }

    [[nodiscard]] bool has_discovery() const noexcept { return has_discovery_; }
    [[nodiscard]] bool has_key() const noexcept { return has_key_; }
    [[nodiscard]] bool has_crypto() const noexcept { return has_crypto_; }
    [[nodiscard]] bool has_tls_material() const noexcept { return has_tls_; }
    [[nodiscard]] bool has_secure_store() const noexcept
    {
        return has_secure_store_;
    }
    [[nodiscard]] bool has_quic() const noexcept { return has_quic_; }
    [[nodiscard]] bool has_bearer() const noexcept { return has_bearer_; }
    [[nodiscard]] bool has_object_store() const noexcept
    { return has_object_store_; }
    /*
     * DUAL runtime, optional and tail-appended. A pre-DUAL provider table has no
     * slot for it, so DUAL must fail closed with UNAVAILABLE rather than run
     * without a simulation worker.
     */
    [[nodiscard]] bool has_dual_runtime() const noexcept
    { return has_dual_runtime_; }
    /*
     * Content reference provider, optional and tail-appended. Absent means no
     * content is available: the engine then publishes no game choice and every
     * content-dependent action fails closed.
     */
    [[nodiscard]] bool has_content() const noexcept { return has_content_; }
    [[nodiscard]] const fly_session_content_port_v2* content() const noexcept
    { return has_content_ ? &content_ : nullptr; }
    [[nodiscard]] const fly_session_dual_runtime_port_v2* dual_runtime()
        const noexcept
    { return has_dual_runtime_ ? &dual_runtime_ : nullptr; }
    [[nodiscard]] bool has_pairing_stack() const noexcept
    {
        return has_key_ && has_crypto_ && has_tls_ && has_bearer_;
    }

    fly_session_result_v2 generate_key(
        const fly_session_op_token_v2* token, std::uint32_t purpose,
        fly_session_bytes_v2 scope_binding, fly_session_inbox_v2_t* inbox) const
    {
        return has_key_ ? key_.generate(key_.context, token, purpose,
                                        scope_binding, inbox)
                        : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 public_key(
        const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 key, std::uint32_t encoding,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_key_ ? key_.public_key(key_.context, token, key, encoding,
                                          inbox)
                        : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 sign_prehashed(
        const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 key, std::uint32_t purpose,
        fly_session_bytes_v2 domain, const std::uint8_t digest[32],
        fly_session_inbox_v2_t* inbox) const
    {
        return has_key_ ? key_.prehashed_sign(key_.context, token, key, purpose,
                                              domain, digest, inbox)
                        : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 agree_key(
        const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 key, fly_session_bytes_v2 peer_x963,
        fly_session_bytes_v2 attempt_binding,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_key_ ? key_.key_agree(key_.context, token, key, peer_x963,
                                         attempt_binding, inbox)
                        : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 release_key(
        fly_session_resource_handle_v2 key) const
    {
        return has_key_ ? key_.release_key(key_.context, key)
                        : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 cancel_key(
        const fly_session_op_token_v2* token) const
    {
        return has_key_ ? key_.cancel(key_.context, token)
                        : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 random(
        const fly_session_op_token_v2* token, std::uint32_t byte_count,
        fly_session_bytes_v2 purpose, fly_session_inbox_v2_t* inbox) const
    {
        return has_crypto_ ? crypto_.random(crypto_.context, token, byte_count,
                                            purpose, inbox)
                           : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 hkdf(
        const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 secret, fly_session_bytes_v2 salt,
        fly_session_bytes_v2 info, std::uint32_t output_size,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_crypto_ ? crypto_.hkdf(crypto_.context, token, secret, salt,
                                          info, output_size, inbox)
                           : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 hmac_sha256(
        const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 key, fly_session_bytes_v2 exact_input,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_crypto_ ? crypto_.hmac_sha256(
                                 crypto_.context, token, key, exact_input, inbox)
                           : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 aead(
        bool seal, const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 key, fly_session_bytes_v2 nonce,
        fly_session_bytes_v2 aad, fly_session_bytes_v2 input,
        fly_session_inbox_v2_t* inbox) const
    {
        if (!has_crypto_)
            return FLY_SESSION_V2_UNAVAILABLE;
        const auto operation = seal ? crypto_.aead_seal : crypto_.aead_open;
        return operation(crypto_.context, token, key, nonce, aad, input, inbox);
    }

    fly_session_result_v2 verify_prehashed(
        const fly_session_op_token_v2* token, fly_session_bytes_v2 public_key,
        fly_session_bytes_v2 domain, const std::uint8_t digest[32],
        fly_session_bytes_v2 signature, fly_session_inbox_v2_t* inbox) const
    {
        return has_crypto_
                   ? crypto_.verify_prehashed(crypto_.context, token, public_key,
                                               domain, digest, signature, inbox)
                   : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 release_secret(
        fly_session_resource_handle_v2 secret) const
    {
        return has_crypto_ ? crypto_.release_secret(crypto_.context, secret)
                           : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 cancel_crypto(
        const fly_session_op_token_v2* token) const
    {
        return has_crypto_ ? crypto_.cancel(crypto_.context, token)
                           : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 create_tls_material(
        const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 tls_key,
        fly_session_bytes_v2 certificate_policy,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_tls_ ? tls_.create(tls_.context, token, tls_key,
                                      certificate_policy, inbox)
                        : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 release_tls_material(
        fly_session_resource_handle_v2 material) const
    {
        return has_tls_ ? tls_.release_material(tls_.context, material)
                        : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 cancel_tls_material(
        const fly_session_op_token_v2* token) const
    {
        return has_tls_ ? tls_.cancel(tls_.context, token)
                        : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 compare_replace_secure_store(
        const fly_session_op_token_v2* token, fly_session_bytes_v2 name_space,
        fly_session_bytes_v2 record_key, std::uint64_t expected_revision,
        fly_session_buffer_v2_t* immutable_secret_buffer,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_secure_store_
            ? secure_store_.compare_replace(
                  secure_store_.context, token, name_space, record_key,
                  expected_revision, immutable_secret_buffer, inbox)
            : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 put_immutable_object(
        const fly_session_op_token_v2* token, std::uint32_t object_kind,
        const std::uint8_t expected_hash[32],
        fly_session_buffer_v2_t* immutable_buffer,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_object_store_
            ? object_store_.put_immutable(
                  object_store_.context, token, object_kind, expected_hash,
                  immutable_buffer, inbox)
            : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 cancel_object_store(
        const fly_session_op_token_v2* token) const
    {
        return has_object_store_
            ? object_store_.cancel(object_store_.context, token)
            : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 query_content(
        const fly_session_op_token_v2* token, std::uint32_t index,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_content_
            ? content_.query(content_.context, token, index, inbox)
            : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 cancel_content(
        const fly_session_op_token_v2* token) const
    {
        return has_content_ ? content_.cancel(content_.context, token)
                            : FLY_SESSION_V2_UNAVAILABLE;
    }

    /* R3 appended read primitive. An R2-sized table has no read; the durable
     * read-back gate then fails closed instead of assuming a previous write. */
    [[nodiscard]] bool has_object_store_read() const noexcept
    {
        return has_object_store_ && object_store_.read != nullptr;
    }

    fly_session_result_v2 read_immutable_object(
        const fly_session_op_token_v2* token, std::uint32_t object_kind,
        const std::uint8_t expected_hash[32],
        fly_session_inbox_v2_t* inbox) const
    {
        return has_object_store_read()
            ? object_store_.read(object_store_.context, token, object_kind,
                                 expected_hash, inbox)
            : FLY_SESSION_V2_UNSUPPORTED;
    }

    fly_session_result_v2 read_secure_store(
        const fly_session_op_token_v2* token, fly_session_bytes_v2 name_space,
        fly_session_bytes_v2 record_key, fly_session_inbox_v2_t* inbox) const
    {
        return has_secure_store_
            ? secure_store_.read(secure_store_.context, token, name_space,
                                 record_key, inbox)
            : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 cancel_secure_store(
        const fly_session_op_token_v2* token) const
    {
        return has_secure_store_
            ? secure_store_.cancel(secure_store_.context, token)
            : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 start_discovery(
        bool advertise, const fly_session_op_token_v2* token,
        fly_session_bytes_v2 policy, std::uint64_t deadline_ns,
        fly_session_inbox_v2_t* inbox) const
    {
        if (!has_discovery_)
            return FLY_SESSION_V2_UNAVAILABLE;
        const auto start = advertise ? discovery_.advertise : discovery_.scan;
        return start(discovery_.context, token, policy, deadline_ns, inbox);
    }

    fly_session_result_v2 stop_discovery(
        const fly_session_op_token_v2* token) const
    {
        return has_discovery_
                   ? discovery_.stop(discovery_.context, token)
                   : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 connect_discovery(
        const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 candidate, std::uint64_t generation,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_discovery_
                   ? discovery_.connect(discovery_.context, token, candidate,
                                        generation, inbox)
                   : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 write_discovery(
        bool indicate, const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 connection,
        std::uint32_t characteristic, fly_session_buffer_v2_t* buffer,
        fly_session_inbox_v2_t* inbox) const
    {
        if (!has_discovery_)
            return FLY_SESSION_V2_UNAVAILABLE;
        const auto operation = indicate ? discovery_.indicate : discovery_.write;
        return operation(discovery_.context, token, connection, characteristic,
                         buffer, inbox);
    }

    fly_session_result_v2 subscribe_discovery(
        const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 connection,
        std::uint32_t characteristic,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_discovery_
                   ? discovery_.subscribe(discovery_.context, token, connection,
                                          characteristic, inbox)
                   : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 disconnect_discovery(
        const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 connection,
        std::uint64_t generation,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_discovery_
                   ? discovery_.disconnect(discovery_.context, token, connection,
                                           generation, inbox)
                   : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 probe_bearer(
        const fly_session_op_token_v2* token, fly_session_bytes_v2 policy,
        std::uint64_t deadline_ns, fly_session_inbox_v2_t* inbox) const
    {
        return has_bearer_ ? bearer_.probe(bearer_.context, token, policy,
                                           deadline_ns, inbox)
                           : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 start_bearer(
        bool create, const fly_session_op_token_v2* token,
        const std::uint8_t plan_hash[32],
        fly_session_resource_handle_v2 credential,
        std::uint32_t confirmation_budget,
        fly_session_inbox_v2_t* inbox) const
    {
        if (!has_bearer_)
            return FLY_SESSION_V2_UNAVAILABLE;
        const auto operation = create ? bearer_.create : bearer_.join;
        return operation(bearer_.context, token, plan_hash, credential,
                         confirmation_budget, inbox);
    }

    fly_session_result_v2 prepare_bearer_credential(
        const fly_session_op_token_v2* token, const std::uint8_t plan_hash[32],
        fly_session_bytes_v2 selected_plan,
        fly_session_resource_handle_v2 bearer, bool creator,
        fly_session_bytes_v2 canonical_join_params,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_bearer_
            ? bearer_.prepare_credential(
                  bearer_.context, token, plan_hash, selected_plan,
                  bearer, creator ? 1u : 0u, canonical_join_params, inbox)
            : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 release_bearer_credential(
        fly_session_resource_handle_v2 credential) const
    {
        return has_bearer_
            ? bearer_.release_credential(bearer_.context, credential)
            : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 resolve_bearer_endpoint(
        const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 path,
        fly_session_bytes_v2 listener_token,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_bearer_
                   ? bearer_.resolve_endpoint(bearer_.context, token, path,
                                              listener_token, inbox)
                   : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 release_bearer(
        const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 bearer,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_bearer_ ? bearer_.release_bearer(
                                 bearer_.context, token, bearer, inbox)
                           : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 cancel_bearer(
        const fly_session_op_token_v2* token) const
    {
        return has_bearer_ ? bearer_.cancel(bearer_.context, token)
                           : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 start_quic(
        bool listen, const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 path, fly_session_bytes_v2 endpoint,
        fly_session_resource_handle_v2 tls_material,
        const fly_session_quic_connect_policy_v2* policy,
        fly_session_inbox_v2_t* inbox) const
    {
        if (!has_quic_)
            return FLY_SESSION_V2_UNAVAILABLE;
        const auto operation = listen ? quic_.listen : quic_.connect;
        return operation(quic_.context, token, path, endpoint, tls_material,
                         policy, inbox);
    }

    fly_session_result_v2 inspect_quic(
        const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 connection,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_quic_ ? quic_.inspect_handshake(
                               quic_.context, token, connection, inbox)
                         : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 quic_exporter(
        const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 connection, fly_session_bytes_v2 label,
        fly_session_bytes_v2 context, std::uint32_t output_size,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_quic_ ? quic_.exporter(quic_.context, token, connection,
                                          label, context, output_size, inbox)
                         : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 open_quic_stream(
        bool bidi, bool accept, const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 connection, std::uint32_t opener_role,
        std::uint32_t stream_kind, fly_session_inbox_v2_t* inbox) const
    {
        if (!has_quic_)
            return FLY_SESSION_V2_UNAVAILABLE;
        const auto operation = accept
            ? (bidi ? quic_.accept_bidi : quic_.accept_uni)
            : (bidi ? quic_.open_bidi : quic_.open_uni);
        return operation(quic_.context, token, connection, opener_role,
                         stream_kind, inbox);
    }

    fly_session_result_v2 write_quic(
        const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 stream,
        fly_session_buffer_v2_t* immutable_buffer, bool finish,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_quic_ ? quic_.write(quic_.context, token, stream,
                                       immutable_buffer, finish ? 1u : 0u, inbox)
                         : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 grant_quic_read(
        const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 stream,
        std::uint64_t byte_credit,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_quic_ ? quic_.grant_read_credit(
                               quic_.context, token, stream, byte_credit, inbox)
                         : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 close_quic(
        const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 connection, std::uint32_t reason,
        fly_session_inbox_v2_t* inbox) const
    {
        return has_quic_ ? quic_.close(quic_.context, token, connection,
                                       reason, inbox)
                         : FLY_SESSION_V2_UNAVAILABLE;
    }

    fly_session_result_v2 cancel_quic(
        const fly_session_op_token_v2* token) const
    {
        return has_quic_ ? quic_.cancel(quic_.context, token)
                         : FLY_SESSION_V2_UNAVAILABLE;
    }

private:
    template <typename Port>
    static void capture_optional(const fly_session_ports_v2& ports,
                                 std::size_t offset, Port& destination,
                                 bool& present) noexcept
    {
        if (ports.struct_size < offset + sizeof(const Port*))
            return;
        const Port* source = nullptr;
        std::memcpy(&source,
                    reinterpret_cast<const std::uint8_t*>(&ports) + offset,
                    sizeof(source));
        if (source)
        {
            destination = *source;
            present = true;
        }
    }

    template <typename Port>
    static void retain_optional(Port& port, bool present) noexcept
    {
        if (present)
            port.retain(port.context);
    }

    template <typename Port>
    static void release_optional(Port& port, bool present) noexcept
    {
        if (present)
            port.release(port.context);
    }

    fly_session_clock_port_v2 clock_{};
    fly_session_executor_port_v2 executor_{};
    fly_session_platform_state_port_v2 platform_state_{};
    fly_session_camera_port_v2 camera_{};
    fly_session_key_port_v2 key_{};
    fly_session_crypto_port_v2 crypto_{};
    fly_session_tls_material_port_v2 tls_{};
    fly_session_secure_store_port_v2 secure_store_{};
    fly_session_quic_port_v2 quic_{};
    fly_session_discovery_port_v2 discovery_{};
    fly_session_bearer_port_v2 bearer_{};
    fly_session_object_store_port_v2 object_store_{};
    fly_session_dual_runtime_port_v2 dual_runtime_{};
    fly_session_content_port_v2 content_{};
    bool has_camera_ = false;
    bool has_key_ = false;
    bool has_crypto_ = false;
    bool has_tls_ = false;
    bool has_secure_store_ = false;
    bool has_quic_ = false;
    bool has_discovery_ = false;
    bool has_bearer_ = false;
    bool has_object_store_ = false;
    bool has_dual_runtime_ = false;
    bool has_content_ = false;
};

} // namespace flynes::session

#endif
