#include <flynes/flynes_session.h>

int flynes_session_header_compiles_as_c(void)
{
    return (int)FLY_SESSION_QUIC_AUDIO + (int)FLY_FRAME_CURSOR_V1_SIZE;
}
