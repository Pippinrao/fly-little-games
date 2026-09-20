#include <flynes/flynes_session.h>

#include "engine/session_engine.hpp"

#include <memory>
#include <new>
#include <cstring>

struct fly_session_v2_handle
{
    explicit fly_session_v2_handle(
        std::shared_ptr<flynes::session::SessionEngine> source)
        : implementation(std::move(source))
    {
    }

    std::shared_ptr<flynes::session::SessionEngine> implementation;
};

namespace {

constexpr uint32_t kMaximumQueueCapacity = UINT32_C(4096);

bool all_zero(const uint32_t* values, uint32_t count) noexcept
{
    for (uint32_t index = 0; index < count; ++index)
    {
        if (values[index] != 0)
        {
            return false;
        }
    }
    return true;
}

bool any_nonzero(const uint8_t* values, std::size_t count) noexcept
{
    for (std::size_t index = 0; index < count; ++index)
    {
        if (values[index] != 0)
            return true;
    }
    return false;
}

fly_session_result_v2 validate_prefix(uint32_t struct_size,
                                      uint32_t required_size,
                                      uint32_t abi_version) noexcept
{
    if (struct_size < required_size || abi_version != FLY_SESSION_ABI_VERSION_2)
    {
        return FLY_SESSION_V2_ABI_MISMATCH;
    }
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 validate_clock(const fly_session_clock_port_v2* port) noexcept
{
    if (!port)
    {
        return FLY_SESSION_V2_UNSUPPORTED;
    }
    const auto prefix = validate_prefix(
        port->struct_size, FLY_SESSION_CLOCK_PORT_V2_SIZE, port->abi_version);
    if (prefix != FLY_SESSION_V2_OK)
    {
        return prefix;
    }
    if (port->reserved_zero != 0 || port->reserved_zero2 != 0)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    if (!port->retain || !port->release || !port->read_continuous)
    {
        return FLY_SESSION_V2_UNSUPPORTED;
    }
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 validate_executor(
    const fly_session_executor_port_v2* port) noexcept
{
    if (!port)
    {
        return FLY_SESSION_V2_UNSUPPORTED;
    }
    const auto prefix = validate_prefix(
        port->struct_size, FLY_SESSION_EXECUTOR_PORT_V2_SIZE, port->abi_version);
    if (prefix != FLY_SESSION_V2_OK)
    {
        return prefix;
    }
    if (port->reserved_zero != 0 || port->reserved_zero2 != 0)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    if (!port->retain || !port->release || !port->post || !port->arm_timer ||
        !port->cancel_timer)
    {
        return FLY_SESSION_V2_UNSUPPORTED;
    }
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 validate_platform_state(
    const fly_session_platform_state_port_v2* port) noexcept
{
    if (!port)
    {
        return FLY_SESSION_V2_UNSUPPORTED;
    }
    const auto prefix = validate_prefix(port->struct_size,
                                        FLY_SESSION_PLATFORM_STATE_PORT_V2_SIZE,
                                        port->abi_version);
    if (prefix != FLY_SESSION_V2_OK)
    {
        return prefix;
    }
    if (port->reserved_zero != 0 || port->reserved_zero2 != 0)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    if (!port->retain || !port->release || !port->watch || !port->stop)
    {
        return FLY_SESSION_V2_UNSUPPORTED;
    }
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 validate_camera(
    const fly_session_camera_port_v2* port) noexcept
{
    if (!port)
    {
        return FLY_SESSION_V2_OK;
    }
    const auto prefix = validate_prefix(
        port->struct_size, FLY_SESSION_CAMERA_PORT_V2_SIZE, port->abi_version);
    if (prefix != FLY_SESSION_V2_OK)
    {
        return prefix;
    }
    if (port->reserved_zero != 0 || port->reserved_zero2 != 0)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    if (!port->retain || !port->release || !port->start_scan || !port->stop_scan)
    {
        return FLY_SESSION_V2_UNSUPPORTED;
    }
    return FLY_SESSION_V2_OK;
}

template <typename Port>
const Port* optional_port(const fly_session_ports_v2* ports,
                          std::size_t offset) noexcept
{
    if (ports->struct_size < offset + sizeof(const Port*))
        return nullptr;
    const Port* value = nullptr;
    std::memcpy(&value, reinterpret_cast<const std::uint8_t*>(ports) + offset,
                sizeof(value));
    return value;
}

template <typename Port>
fly_session_result_v2 validate_provider_prefix(const Port* port,
                                               std::uint32_t size) noexcept
{
    if (!port)
        return FLY_SESSION_V2_OK;
    const auto prefix = validate_prefix(port->struct_size, size, port->abi_version);
    if (prefix != FLY_SESSION_V2_OK)
        return prefix;
    if (port->reserved_zero != 0 || port->reserved_zero2 != 0)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    if (!port->retain || !port->release)
        return FLY_SESSION_V2_UNSUPPORTED;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 validate_key(const fly_session_key_port_v2* port) noexcept
{
    const auto prefix = validate_provider_prefix(port, FLY_SESSION_KEY_PORT_V2_SIZE);
    if (prefix != FLY_SESSION_V2_OK || !port)
        return prefix;
    return port->generate && port->open && port->public_key &&
                   port->prehashed_sign && port->key_agree &&
                   port->release_key && port->destroy && port->cancel
               ? FLY_SESSION_V2_OK
               : FLY_SESSION_V2_UNSUPPORTED;
}

fly_session_result_v2 validate_crypto(const fly_session_crypto_port_v2* port) noexcept
{
    const auto prefix = validate_provider_prefix(port, FLY_SESSION_CRYPTO_PORT_V2_SIZE);
    if (prefix != FLY_SESSION_V2_OK || !port)
        return prefix;
    return port->random && port->hkdf && port->aead_seal && port->aead_open &&
                   port->verify_prehashed && port->release_secret
                   && port->cancel && port->hmac_sha256
               ? FLY_SESSION_V2_OK
               : FLY_SESSION_V2_UNSUPPORTED;
}

fly_session_result_v2 validate_tls(
    const fly_session_tls_material_port_v2* port) noexcept
{
    const auto prefix = validate_provider_prefix(
        port, FLY_SESSION_TLS_MATERIAL_PORT_V2_SIZE);
    if (prefix != FLY_SESSION_V2_OK || !port)
        return prefix;
    return port->create && port->restore && port->release_material &&
                   port->cancel
               ? FLY_SESSION_V2_OK
               : FLY_SESSION_V2_UNSUPPORTED;
}

fly_session_result_v2 validate_secure_store(
    const fly_session_secure_store_port_v2* port) noexcept
{
    const auto prefix = validate_provider_prefix(
        port, FLY_SESSION_SECURE_STORE_PORT_V2_SIZE);
    if (prefix != FLY_SESSION_V2_OK || !port)
        return prefix;
    return port->read && port->compare_replace && port->remove && port->cancel
               ? FLY_SESSION_V2_OK
               : FLY_SESSION_V2_UNSUPPORTED;
}

fly_session_result_v2 validate_object_store(
    const fly_session_object_store_port_v2* port) noexcept
{
    /*
     * DELIBERATE COMPATIBILITY TRADEOFF, decided by the owner on 2026-09-16 and
     * re-affirmed for this change: `read` stays REQUIRED.
     *
     * R3 appended `read` to fly_session_object_store_port_v2. validate_prefix is
     * this repository's tail-append gate (struct_size >= required), so a table
     * that still reports the R2 size is rejected here as ABI_MISMATCH instead of
     * being accepted with a null `read` that the engine would only discover
     * mid-handshake, after the local 0x0212 binding had already been declared
     * durable. The alternative — required = FLY_SESSION_OBJECT_STORE_PORT_V2_R2_SIZE
     * plus an explicit null check at the point of use — was considered and
     * rejected:
     *
     *   - no provider has ever shipped the R2 shape (the primitive is unreleased),
     *     so there is no compatibility to preserve;
     *   - "persist, then read back" is the whole point of the durable-binding
     *     gate, and a port that cannot read cannot satisfy it;
     *   - a provider that cannot read is better served by a clear, immediate
     *     ABI_MISMATCH at table-validation time than by a late UNAVAILABLE that
     *     looks like a protocol failure.
     *
     * If a released R2 provider ever appears, the correct fix is to relax the
     * size gate to FLY_SESSION_OBJECT_STORE_PORT_V2_R2_SIZE and make the absence
     * of `read` a fail-closed UNAVAILABLE at the point of use — not a silent
     * success. That is a compatibility decision for the owner, not a code fix.
     */
    const auto prefix = validate_provider_prefix(
        port, FLY_SESSION_OBJECT_STORE_PORT_V2_SIZE);
    if (prefix != FLY_SESSION_V2_OK || !port)
        return prefix;
    return port->put_immutable && port->cancel && port->read
               ? FLY_SESSION_V2_OK
               : FLY_SESSION_V2_UNSUPPORTED;
}

fly_session_result_v2 validate_quic(const fly_session_quic_port_v2* port) noexcept
{
    const auto prefix = validate_provider_prefix(port, FLY_SESSION_QUIC_PORT_V2_SIZE);
    if (prefix != FLY_SESSION_V2_OK || !port)
        return prefix;
    return port->listen && port->connect && port->inspect_handshake &&
                   port->exporter && port->open_uni && port->open_bidi &&
                   port->accept_uni && port->accept_bidi &&
                   port->write && port->finish && port->reset &&
                   port->grant_read_credit && port->send_datagram &&
                   port->payload_budget && port->stats && port->close &&
                   port->cancel
               ? FLY_SESSION_V2_OK
               : FLY_SESSION_V2_UNSUPPORTED;
}

fly_session_result_v2 validate_discovery(
    const fly_session_discovery_port_v2* port) noexcept
{
    const auto prefix = validate_provider_prefix(
        port, FLY_SESSION_DISCOVERY_PORT_V2_SIZE);
    if (prefix != FLY_SESSION_V2_OK || !port)
        return prefix;
    return port->scan && port->advertise && port->stop && port->connect &&
                   port->disconnect && port->write && port->indicate &&
                   port->subscribe
               ? FLY_SESSION_V2_OK
               : FLY_SESSION_V2_UNSUPPORTED;
}

fly_session_result_v2 validate_bearer(
    const fly_session_bearer_port_v2* port) noexcept
{
    const auto prefix = validate_provider_prefix(
        port, FLY_SESSION_BEARER_PORT_V2_SIZE);
    if (prefix != FLY_SESSION_V2_OK || !port)
        return prefix;
    return port->probe && port->create && port->join &&
                   port->resolve_endpoint && port->release_bearer &&
                   port->cancel && port->prepare_credential &&
                   port->release_credential
               ? FLY_SESSION_V2_OK
               : FLY_SESSION_V2_UNSUPPORTED;
}

fly_session_result_v2 validate_dual_runtime(
    const fly_session_dual_runtime_port_v2* port) noexcept
{
    const auto prefix = validate_provider_prefix(
        port, FLY_SESSION_DUAL_RUNTIME_PORT_V2_SIZE);
    if (prefix != FLY_SESSION_V2_OK || !port)
        return prefix;
    return port->load && port->step && port->export_state &&
                   port->import_state && port->state_digest
               ? FLY_SESSION_V2_OK
               : FLY_SESSION_V2_UNSUPPORTED;
}

fly_session_result_v2 validate_content(
    const fly_session_content_port_v2* port) noexcept
{
    const auto prefix = validate_provider_prefix(
        port, FLY_SESSION_CONTENT_PORT_V2_SIZE);
    if (prefix != FLY_SESSION_V2_OK || !port)
        return prefix;
    return port->query && port->cancel ? FLY_SESSION_V2_OK
                                       : FLY_SESSION_V2_UNSUPPORTED;
}

fly_session_result_v2 validate_ports(const fly_session_ports_v2* ports) noexcept
{
    if (!ports)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    const auto prefix = validate_prefix(
        ports->struct_size, FLY_SESSION_PORTS_V2_R0_SIZE, ports->abi_version);
    if (prefix != FLY_SESSION_V2_OK)
    {
        return prefix;
    }
    if (ports->reserved_zero != 0 || ports->reserved_zero2 != 0)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }

    const auto clock = validate_clock(ports->clock);
    if (clock != FLY_SESSION_V2_OK)
    {
        return clock;
    }
    const auto executor = validate_executor(ports->executor);
    if (executor != FLY_SESSION_V2_OK)
    {
        return executor;
    }
    const auto platform_state = validate_platform_state(ports->platform_state);
    if (platform_state != FLY_SESSION_V2_OK)
    {
        return platform_state;
    }
    const auto camera = validate_camera(ports->camera);
    if (camera != FLY_SESSION_V2_OK)
        return camera;
    const auto key = validate_key(optional_port<fly_session_key_port_v2>(
        ports, offsetof(fly_session_ports_v2, key)));
    if (key != FLY_SESSION_V2_OK)
        return key;
    const auto crypto = validate_crypto(optional_port<fly_session_crypto_port_v2>(
        ports, offsetof(fly_session_ports_v2, crypto)));
    if (crypto != FLY_SESSION_V2_OK)
        return crypto;
    const auto tls = validate_tls(optional_port<fly_session_tls_material_port_v2>(
        ports, offsetof(fly_session_ports_v2, tls_material)));
    if (tls != FLY_SESSION_V2_OK)
        return tls;
    const auto store = validate_secure_store(
        optional_port<fly_session_secure_store_port_v2>(
            ports, offsetof(fly_session_ports_v2, secure_store)));
    if (store != FLY_SESSION_V2_OK)
        return store;
    const auto quic = validate_quic(optional_port<fly_session_quic_port_v2>(
        ports, offsetof(fly_session_ports_v2, quic)));
    if (quic != FLY_SESSION_V2_OK)
        return quic;
    const auto discovery = validate_discovery(
        optional_port<fly_session_discovery_port_v2>(
            ports, offsetof(fly_session_ports_v2, discovery)));
    if (discovery != FLY_SESSION_V2_OK)
        return discovery;
    const auto bearer = validate_bearer(optional_port<fly_session_bearer_port_v2>(
        ports, offsetof(fly_session_ports_v2, bearer)));
    if (bearer != FLY_SESSION_V2_OK)
        return bearer;
    const auto object_store = validate_object_store(
        optional_port<fly_session_object_store_port_v2>(
            ports, offsetof(fly_session_ports_v2, object_store)));
    if (object_store != FLY_SESSION_V2_OK)
        return object_store;
    /*
     * DUAL runtime: optional and tail-appended. A table that stops before the
     * slot is a legal pre-DUAL table, and DUAL is then unavailable rather than
     * rejected.
     */
    const auto dual = validate_dual_runtime(
        optional_port<fly_session_dual_runtime_port_v2>(
            ports, offsetof(fly_session_ports_v2, dual_runtime)));
    if (dual != FLY_SESSION_V2_OK)
        return dual;
    /*
     * Content: likewise optional and tail-appended. Absent means no content is
     * available, which is a legal pre-content table rather than an error.
     */
    return validate_content(optional_port<fly_session_content_port_v2>(
        ports, offsetof(fly_session_ports_v2, content)));
}

} // namespace

extern "C" fly_session_result_v2 fly_session_create_v2(
    const fly_session_config_v2* config,
    const fly_session_ports_v2* ports,
    fly_session_v2_t** out_engine)
{
    if (!out_engine)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    *out_engine = nullptr;
    if (!config)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }

    const auto config_prefix = validate_prefix(
        config->struct_size, FLY_SESSION_CONFIG_V2_SIZE, config->abi_version);
    if (config_prefix != FLY_SESSION_V2_OK)
    {
        return config_prefix;
    }
    if (!all_zero(config->reserved_zero, 4))
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    if (config->action_queue_capacity == 0 ||
        config->action_queue_capacity > kMaximumQueueCapacity ||
        config->notice_queue_capacity == 0 ||
        config->notice_queue_capacity > kMaximumQueueCapacity)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }

    const auto ports_result = validate_ports(ports);
    if (ports_result != FLY_SESSION_V2_OK)
    {
        return ports_result;
    }

    try
    {
        auto implementation =
            std::make_shared<flynes::session::SessionEngine>(*config, *ports);
        const auto start_result = implementation->start();
        if (start_result != FLY_SESSION_V2_OK)
        {
            return start_result;
        }
        *out_engine = new fly_session_v2_handle(std::move(implementation));
        return FLY_SESSION_V2_OK;
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return FLY_SESSION_V2_CONTRACT_VIOLATION;
    }
}

extern "C" fly_session_result_v2 fly_session_submit_action_v2(
    fly_session_v2_t* engine,
    const fly_session_action_v2* action)
{
    if (!engine || !action)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    const auto prefix = validate_prefix(
        action->struct_size, FLY_SESSION_ACTION_V2_SIZE, action->abi_version);
    if (prefix != FLY_SESSION_V2_OK)
    {
        return prefix;
    }
    if (action->request_id == 0 || action->reserved_zero != 0 ||
        (action->choice_size != 0 &&
         action->choice_size < FLY_SESSION_ACTION_CHOICE_V2_SIZE))
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    return engine->implementation->submit_action(*action);
}

extern "C" fly_session_result_v2 fly_session_submit_input_v2(
    fly_session_v2_t* engine,
    const fly_session_input_v2* input)
{
    if (!engine || !input)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    /*
     * Tail-append compatibility: the pre-DUAL prefix is still a legal input, and
     * the appended four-port mask is only read when the caller declared the
     * enlarged size, so a shorter buffer is never read past its end.
     */
    const auto prefix = validate_prefix(
        input->struct_size, FLY_SESSION_INPUT_V2_R0_SIZE, input->abi_version);
    if (prefix != FLY_SESSION_V2_OK)
        return prefix;
    const auto scope_prefix = validate_prefix(
        input->scope.struct_size, FLY_SESSION_SCOPE_V2_SIZE,
        input->scope.abi_version);
    if (scope_prefix != FLY_SESSION_V2_OK)
        return scope_prefix;
    const auto clock_prefix = validate_prefix(
        input->capture_clock.struct_size, FLY_SESSION_CLOCK_SAMPLE_V2_SIZE,
        input->capture_clock.abi_version);
    if (clock_prefix != FLY_SESSION_V2_OK)
        return clock_prefix;
    if (input->reserved_zero != 0 || input->scope.reserved_zero != 0 ||
        input->scope.kind != FLY_SESSION_SCOPE_GAME_V2 ||
        !any_nonzero(input->scope.link_id, sizeof(input->scope.link_id)) ||
        !any_nonzero(input->scope.branch_id, sizeof(input->scope.branch_id)) ||
        input->expected_seat_revision == 0 ||
        input->local_device_input_id == 0 ||
        input->capture_clock.suspend_inclusive != 1 ||
        input->capture_clock.reserved_zero != 0 ||
        !any_nonzero(input->capture_clock.boot_generation,
                     sizeof(input->capture_clock.boot_generation)))
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    if (input->struct_size >= FLY_SESSION_INPUT_V2_SIZE)
    {
        /*
         * A DUAL port mask is one byte wide (the canonical input contract's
         * kDualFullPortMaskV1). Bits above it are malformed input, not a d-pad
         * conflict: an impossible UP+DOWN / LEFT+RIGHT pair is legal on the wire
         * and is normalized by the engine before it is ever built into a
         * canonical bundle.
         */
        for (std::uint32_t port = 0; port < FLY_SESSION_DUAL_PORT_COUNT_V2; ++port)
        {
            if ((input->port_mask[port] & ~0xFFu) != 0u)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
        }
    }
    return engine->implementation->submit_input(*input);
}

extern "C" fly_session_result_v2 fly_session_read_notice_v2(
    fly_session_v2_t* engine,
    fly_session_notice_v2* out_notice)
{
    if (!engine || !out_notice)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    const auto prefix = validate_prefix(out_notice->struct_size,
                                        FLY_SESSION_NOTICE_V2_SIZE,
                                        out_notice->abi_version);
    if (prefix != FLY_SESSION_V2_OK)
    {
        return prefix;
    }
    return engine->implementation->read_notice(*out_notice);
}

extern "C" fly_session_result_v2 fly_session_deliver_v2(
    fly_session_inbox_v2_t* inbox,
    const fly_session_port_event_v2* event)
{
    if (!inbox || !event)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    if (inbox->closed.load(std::memory_order_acquire))
    {
        return FLY_SESSION_V2_CLOSED;
    }
    const auto prefix = validate_prefix(event->struct_size,
                                        FLY_SESSION_PORT_EVENT_V2_SIZE,
                                        event->abi_version);
    if (prefix != FLY_SESSION_V2_OK)
    {
        return prefix;
    }
    const auto token_prefix = validate_prefix(event->token.struct_size,
                                              FLY_SESSION_OP_TOKEN_V2_SIZE,
                                              event->token.abi_version);
    if (token_prefix != FLY_SESSION_V2_OK)
    {
        return token_prefix;
    }
    if (event->event_sequence == 0 || event->event_kind == 0 ||
        event->terminal > 1 || event->payload_size > sizeof(event->payload) ||
        event->reserved_zero != 0)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    auto implementation = inbox->engine.lock();
    if (!implementation)
    {
        return FLY_SESSION_V2_CLOSED;
    }
    return implementation->deliver(*event);
}

extern "C" fly_session_result_v2 fly_session_acquire_view_v2(
    fly_session_v2_t* engine,
    fly_session_view_v2_t** out_view)
{
    if (!engine || !out_view)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    *out_view = nullptr;
    return engine->implementation->acquire_view(out_view);
}

extern "C" fly_session_result_v2 fly_session_read_dual_start_ref_v2(
    fly_session_v2_t* engine,
    const fly_session_approval_token_v2_t* start_approval,
    fly_session_dual_start_ref_v2* out_ref)
{
    if (!engine || !start_approval || !out_ref)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    const auto prefix = validate_prefix(out_ref->struct_size,
                                         FLY_SESSION_DUAL_START_REF_V2_SIZE,
                                         out_ref->abi_version);
    if (prefix != FLY_SESSION_V2_OK)
        return prefix;
    return engine->implementation->read_dual_start_ref(start_approval, *out_ref);
}

extern "C" fly_session_result_v2 fly_session_begin_shutdown_v2(
    fly_session_v2_t* engine,
    uint64_t request_id)
{
    if (!engine)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    return engine->implementation->begin_shutdown(request_id);
}

extern "C" fly_session_result_v2 fly_session_destroy_v2(
    fly_session_v2_t* engine)
{
    if (!engine)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    if (!engine->implementation->can_destroy())
    {
        return FLY_SESSION_V2_BUSY;
    }
    engine->implementation->detach_handle();
    delete engine;
    return FLY_SESSION_V2_OK;
}
