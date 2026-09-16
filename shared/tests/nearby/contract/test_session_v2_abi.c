#include <flynes/flynes_session.h>

#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(fly_session_result_v2) == 4, "result width");
_Static_assert(offsetof(fly_session_config_v2, struct_size) == 0, "size prefix");
_Static_assert(offsetof(fly_session_config_v2, abi_version) == 4, "version prefix");
_Static_assert(FLY_SESSION_ABI_VERSION_2 == 2, "ABI version");
_Static_assert(sizeof(fly_session_resource_handle_v2) == 8, "resource handle width");
_Static_assert(FLY_SESSION_KEY_DEVICE_IDENTITY_V2 == 1, "identity purpose");
_Static_assert(FLY_SESSION_KEY_SESSION_SIGNING_V2 == 2, "session signing purpose");
_Static_assert(FLY_SESSION_KEY_PAIR_ECDH_V2 == 3, "pair ECDH purpose");
_Static_assert(FLY_SESSION_KEY_TLS_V2 == 4, "TLS purpose");
_Static_assert(FLY_SESSION_QUIC_REQUIRE_FULL_TLS13_V2 == 1, "full TLS policy");
_Static_assert(FLY_SESSION_QUIC_FORBID_RESUMPTION_V2 == 1, "no resumption policy");
_Static_assert(offsetof(fly_session_ports_v2, key) >
                   offsetof(fly_session_ports_v2, camera),
               "provider tables append after the R0 prefix");
_Static_assert(offsetof(fly_session_ports_v2, object_store) >
                   offsetof(fly_session_ports_v2, bearer),
               "object store appends without changing earlier provider offsets");
/* R3 tail append: exact-enough byte read-back of a durable immutable object. */
_Static_assert(offsetof(fly_session_object_store_port_v2, read) >
                   offsetof(fly_session_object_store_port_v2, cancel),
               "object store read appends after the frozen R2 prefix");
_Static_assert(FLY_SESSION_OBJECT_STORE_PORT_V2_R2_SIZE ==
                   offsetof(fly_session_object_store_port_v2, read),
               "the R2 prefix ends exactly where read begins");
_Static_assert(FLY_SESSION_OBJECT_STORE_PORT_V2_SIZE >
                   FLY_SESSION_OBJECT_STORE_PORT_V2_R2_SIZE,
               "the R3 object store table is strictly larger than R2");
_Static_assert(offsetof(fly_session_object_store_port_v2, struct_size) == 0,
               "object store size prefix");
_Static_assert(offsetof(fly_session_input_v2, struct_size) == 0,
               "input size prefix");
_Static_assert(offsetof(fly_session_input_v2, abi_version) == 4,
               "input version prefix");
_Static_assert(sizeof(((fly_session_input_v2*)0)->buttons) == 4,
               "input buttons preserve the complete mask");

/* DUAL tail appends, checked from a C consumer. */
_Static_assert(FLY_SESSION_DUAL_PORT_COUNT_V2 == 4, "DUAL port count");
_Static_assert(FLY_SESSION_INPUT_V2_R0_SIZE ==
                   offsetof(fly_session_input_v2, port_mask),
               "the input R0 prefix stops before the DUAL port mask");
_Static_assert(FLY_SESSION_INPUT_V2_SIZE ==
                   FLY_SESSION_INPUT_V2_R0_SIZE +
                       FLY_SESSION_DUAL_PORT_COUNT_V2 * 4,
               "the DUAL port mask is a pure tail append");
_Static_assert(offsetof(fly_session_snapshot_v2, dual_mode) ==
                   FLY_SESSION_SNAPSHOT_V2_R0_SIZE,
               "the snapshot R0 prefix stops before the DUAL block");
_Static_assert(FLY_SESSION_SNAPSHOT_V2_SIZE >
                   FLY_SESSION_SNAPSHOT_V2_R0_SIZE,
               "the DUAL snapshot block is appended");
_Static_assert(FLY_SESSION_PORTS_V2_R1_SIZE ==
                   offsetof(fly_session_ports_v2, dual_runtime),
               "the pre-DUAL provider table stops before the runtime slot");
_Static_assert(FLY_SESSION_PORTS_V2_SIZE > FLY_SESSION_PORTS_V2_R1_SIZE,
               "the DUAL runtime slot is appended");
_Static_assert(offsetof(fly_session_dual_runtime_port_v2, struct_size) == 0,
               "DUAL runtime size prefix");
_Static_assert(offsetof(fly_session_dual_runtime_port_v2, load) >
                   offsetof(fly_session_dual_runtime_port_v2, release),
               "the DUAL runtime keeps the provider retain/release prefix");
_Static_assert(FLY_SESSION_DUAL_FREEZE_TRANSPORT_TERMINAL_V2 == 6,
               "freeze vocabulary matches the frozen DUAL seam");
_Static_assert(FLY_SESSION_DUAL_FREEZE_SEAT_OR_AUTHORITY_CHANGED_V2 == 9,
               "freeze vocabulary matches the frozen DUAL seam");

int main(void)
{
    fly_session_v2_t* engine = NULL;
    fly_session_inbox_v2_t* inbox = NULL;
    fly_session_view_v2_t* view = NULL;
    fly_session_approval_token_v2_t* token = NULL;
    fly_session_key_port_v2 key = {0};
    fly_session_crypto_port_v2 crypto = {0};
    fly_session_tls_material_port_v2 tls = {0};
    fly_session_secure_store_port_v2 secure_store = {0};
    fly_session_object_store_port_v2 object_store = {0};
    fly_session_quic_port_v2 quic = {0};
    fly_session_input_v2 input = {0};
    return engine != NULL || inbox != NULL || view != NULL || token != NULL ||
           key.struct_size != 0 || crypto.struct_size != 0 ||
           tls.struct_size != 0 || secure_store.struct_size != 0 ||
           object_store.struct_size != 0 ||
           quic.struct_size != 0 || input.struct_size != 0;
}
