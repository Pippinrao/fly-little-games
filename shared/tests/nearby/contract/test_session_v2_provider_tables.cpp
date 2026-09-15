#include <flynes/flynes_session.h>

#include <cstdint>
#include <cstdio>

namespace {

struct Counts { int retains = 0; int releases = 0; };
int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

void retain(void* context) { ++static_cast<Counts*>(context)->retains; }
void release(void* context) { ++static_cast<Counts*>(context)->releases; }

fly_session_result_v2 unavailable_start(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2,
    std::uint64_t, fly_session_inbox_v2_t*) { return FLY_SESSION_V2_UNAVAILABLE; }
fly_session_result_v2 unavailable_stop(
    void*, const fly_session_op_token_v2*) { return FLY_SESSION_V2_UNAVAILABLE; }
fly_session_result_v2 unavailable_connect(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint64_t, fly_session_inbox_v2_t*) { return FLY_SESSION_V2_UNAVAILABLE; }
fly_session_result_v2 unavailable_io(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, fly_session_buffer_v2_t*, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
fly_session_result_v2 unavailable_subscribe(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, fly_session_inbox_v2_t*) { return FLY_SESSION_V2_UNAVAILABLE; }
fly_session_result_v2 unavailable_bearer_start(
    void*, const fly_session_op_token_v2*, const std::uint8_t[32],
    fly_session_resource_handle_v2, std::uint32_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
fly_session_result_v2 unavailable_resolve(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_inbox_v2_t*) { return FLY_SESSION_V2_UNAVAILABLE; }
fly_session_result_v2 unavailable_release_resource(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_inbox_v2_t*) { return FLY_SESSION_V2_UNAVAILABLE; }
fly_session_result_v2 unavailable_crypto_random(
    void*, const fly_session_op_token_v2*, std::uint32_t,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
fly_session_result_v2 unavailable_crypto_hkdf(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_bytes_v2, std::uint32_t,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
fly_session_result_v2 unavailable_crypto_aead(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_bytes_v2, fly_session_bytes_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
fly_session_result_v2 unavailable_crypto_hmac(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
fly_session_result_v2 unavailable_crypto_verify(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2,
    fly_session_bytes_v2, const std::uint8_t[32], fly_session_bytes_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
fly_session_result_v2 unavailable_secret_release(
    void*, fly_session_resource_handle_v2)
{ return FLY_SESSION_V2_UNAVAILABLE; }
fly_session_result_v2 unavailable_prepare_credential(
    void*, const fly_session_op_token_v2*, const std::uint8_t[32],
    fly_session_bytes_v2, fly_session_resource_handle_v2, std::uint32_t,
    fly_session_bytes_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
fly_session_result_v2 unavailable_put_immutable(
    void*, const fly_session_op_token_v2*, std::uint32_t,
    const std::uint8_t[32], fly_session_buffer_v2_t*,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }

fly_session_result_v2 read_clock(void*, fly_session_clock_sample_v2* out)
{
    if (!out) return FLY_SESSION_V2_INVALID_ARGUMENT;
    *out = {};
    out->continuous_ns = 1;
    out->suspend_inclusive = 1;
    return FLY_SESSION_V2_OK;
}
fly_session_result_v2 post(void*, fly_session_task_v2_t*) { return FLY_SESSION_V2_ACCEPTED; }
fly_session_result_v2 arm(void*, std::uint64_t, const std::uint8_t*,
                          std::uint64_t, fly_session_task_v2_t*)
{ return FLY_SESSION_V2_ACCEPTED; }
fly_session_result_v2 cancel(void*, std::uint64_t) { return FLY_SESSION_V2_ACCEPTED; }
fly_session_result_v2 watch(void*, const fly_session_op_token_v2*,
                            fly_session_inbox_v2_t*) { return FLY_SESSION_V2_ACCEPTED; }
fly_session_result_v2 stop_watch(void*, const fly_session_op_token_v2*)
{ return FLY_SESSION_V2_OK; }

struct Fixture
{
    Counts clock_counts, executor_counts, platform_counts, crypto_counts,
        discovery_counts, bearer_counts, object_store_counts;
    fly_session_clock_port_v2 clock{};
    fly_session_executor_port_v2 executor{};
    fly_session_platform_state_port_v2 platform{};
    fly_session_crypto_port_v2 crypto{};
    fly_session_discovery_port_v2 discovery{};
    fly_session_bearer_port_v2 bearer{};
    fly_session_object_store_port_v2 object_store{};
    fly_session_ports_v2 ports{};
    fly_session_config_v2 config{};

    Fixture()
    {
        clock = {FLY_SESSION_CLOCK_PORT_V2_SIZE, FLY_SESSION_ABI_VERSION_2, 0, 0,
                 &clock_counts, retain, release, read_clock};
        executor = {FLY_SESSION_EXECUTOR_PORT_V2_SIZE, FLY_SESSION_ABI_VERSION_2, 0, 0,
                    &executor_counts, retain, release, post, arm, cancel};
        platform = {FLY_SESSION_PLATFORM_STATE_PORT_V2_SIZE, FLY_SESSION_ABI_VERSION_2,
                    0, 0, &platform_counts, retain, release, watch, stop_watch};

        crypto.struct_size = FLY_SESSION_CRYPTO_PORT_V2_SIZE;
        crypto.abi_version = FLY_SESSION_ABI_VERSION_2;
        crypto.context = &crypto_counts;
        crypto.retain = retain;
        crypto.release = release;
        crypto.random = unavailable_crypto_random;
        crypto.hkdf = unavailable_crypto_hkdf;
        crypto.hmac_sha256 = unavailable_crypto_hmac;
        crypto.aead_seal = unavailable_crypto_aead;
        crypto.aead_open = unavailable_crypto_aead;
        crypto.verify_prehashed = unavailable_crypto_verify;
        crypto.release_secret = unavailable_secret_release;
        crypto.cancel = unavailable_stop;

        discovery.struct_size = FLY_SESSION_DISCOVERY_PORT_V2_SIZE;
        discovery.abi_version = FLY_SESSION_ABI_VERSION_2;
        discovery.context = &discovery_counts;
        discovery.retain = retain;
        discovery.release = release;
        discovery.scan = unavailable_start;
        discovery.advertise = unavailable_start;
        discovery.stop = unavailable_stop;
        discovery.connect = unavailable_connect;
        discovery.disconnect = unavailable_connect;
        discovery.write = unavailable_io;
        discovery.indicate = unavailable_io;
        discovery.subscribe = unavailable_subscribe;

        bearer.struct_size = FLY_SESSION_BEARER_PORT_V2_SIZE;
        bearer.abi_version = FLY_SESSION_ABI_VERSION_2;
        bearer.context = &bearer_counts;
        bearer.retain = retain;
        bearer.release = release;
        bearer.probe = unavailable_start;
        bearer.create = unavailable_bearer_start;
        bearer.join = unavailable_bearer_start;
        bearer.resolve_endpoint = unavailable_resolve;
        bearer.release_bearer = unavailable_release_resource;
        bearer.cancel = unavailable_stop;
        bearer.prepare_credential = unavailable_prepare_credential;
        bearer.release_credential = unavailable_secret_release;

        object_store.struct_size = FLY_SESSION_OBJECT_STORE_PORT_V2_SIZE;
        object_store.abi_version = FLY_SESSION_ABI_VERSION_2;
        object_store.context = &object_store_counts;
        object_store.retain = retain;
        object_store.release = release;
        object_store.put_immutable = unavailable_put_immutable;
        object_store.cancel = unavailable_stop;

        ports.struct_size = FLY_SESSION_PORTS_V2_SIZE;
        ports.abi_version = FLY_SESSION_ABI_VERSION_2;
        ports.clock = &clock;
        ports.executor = &executor;
        ports.platform_state = &platform;
        ports.crypto = &crypto;
        ports.discovery = &discovery;
        ports.bearer = &bearer;
        ports.object_store = &object_store;

        config.struct_size = FLY_SESSION_CONFIG_V2_SIZE;
        config.abi_version = FLY_SESSION_ABI_VERSION_2;
        config.action_queue_capacity = 2;
        config.notice_queue_capacity = 2;
    }
};

void test_complete_tables_are_retained_and_released()
{
    Fixture fixture;
    fly_session_v2_t* engine = nullptr;
    check(fly_session_create_v2(&fixture.config, &fixture.ports, &engine) ==
              FLY_SESSION_V2_OK, "complete discovery and bearer tables create");
    check(fixture.crypto_counts.retains == 1 &&
              fixture.discovery_counts.retains == 1 &&
              fixture.bearer_counts.retains == 1 &&
              fixture.object_store_counts.retains == 1,
          "complete optional provider contexts retain once");
    check(fly_session_begin_shutdown_v2(engine, 1) == FLY_SESSION_V2_ACCEPTED &&
              fly_session_destroy_v2(engine) == FLY_SESSION_V2_OK,
          "provider-table engine shuts down");
    check(fixture.crypto_counts.releases == 1 &&
              fixture.discovery_counts.releases == 1 &&
              fixture.bearer_counts.releases == 1 &&
              fixture.object_store_counts.releases == 1,
          "complete optional provider contexts release once");
}

void test_invalid_tables_retain_nothing()
{
    Fixture fixture;
    fly_session_v2_t* engine = reinterpret_cast<fly_session_v2_t*>(1);
    auto undersized = fixture.discovery;
    --undersized.struct_size;
    fixture.ports.discovery = &undersized;
    check(fly_session_create_v2(&fixture.config, &fixture.ports, &engine) ==
              FLY_SESSION_V2_ABI_MISMATCH && engine == nullptr,
          "undersized discovery table is rejected");

    fixture.ports.discovery = &fixture.discovery;
    auto reserved = fixture.bearer;
    reserved.reserved_zero = 1;
    fixture.ports.bearer = &reserved;
    check(fly_session_create_v2(&fixture.config, &fixture.ports, &engine) ==
              FLY_SESSION_V2_INVALID_ARGUMENT,
          "nonzero bearer reserved field is rejected");

    fixture.ports.bearer = &fixture.bearer;
    auto partial = fixture.discovery;
    partial.subscribe = nullptr;
    fixture.ports.discovery = &partial;
    check(fly_session_create_v2(&fixture.config, &fixture.ports, &engine) ==
              FLY_SESSION_V2_UNSUPPORTED,
          "partial discovery table is rejected");

    fixture.ports.discovery = &fixture.discovery;
    auto missing_hmac = fixture.crypto;
    missing_hmac.hmac_sha256 = nullptr;
    fixture.ports.crypto = &missing_hmac;
    check(fly_session_create_v2(&fixture.config, &fixture.ports, &engine) ==
              FLY_SESSION_V2_UNSUPPORTED,
          "crypto provider cannot omit exact HMAC-SHA256");

    fixture.ports.crypto = &fixture.crypto;
    auto uncancellable = fixture.bearer;
    uncancellable.cancel = nullptr;
    fixture.ports.bearer = &uncancellable;
    check(fly_session_create_v2(&fixture.config, &fixture.ports, &engine) ==
              FLY_SESSION_V2_UNSUPPORTED,
          "an asynchronous provider without cancel is rejected");

    fixture.ports.bearer = &fixture.bearer;
    auto missing_put = fixture.object_store;
    missing_put.put_immutable = nullptr;
    fixture.ports.object_store = &missing_put;
    check(fly_session_create_v2(&fixture.config, &fixture.ports, &engine) ==
              FLY_SESSION_V2_UNSUPPORTED,
          "object store cannot omit durable immutable put");
    check(fixture.clock_counts.retains == 0 && fixture.executor_counts.retains == 0 &&
              fixture.platform_counts.retains == 0 &&
              fixture.crypto_counts.retains == 0 &&
              fixture.discovery_counts.retains == 0 && fixture.bearer_counts.retains == 0 &&
              fixture.object_store_counts.retains == 0,
          "all invalid tables fail before any retain");
}

} // namespace


int main()
{
    test_complete_tables_are_retained_and_released();
    test_invalid_tables_retain_nothing();
    return failures == 0 ? 0 : 1;
}
