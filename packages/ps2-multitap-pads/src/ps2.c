/* libpad and libmtap behind MpadOps. Builds only with the PS2SDK (or the test stubs in tests/ps2sdk-stub). */
#include "mpad/ps2.h"

#include <libmtap.h>
#include <libpad.h>
#include <loadfile.h>

/* a compile-time check that C99 compilers accept: a negative array size when the condition is false */
#define MPAD_ASSERT(cond, name) typedef char mpad_assert_##name[(cond) ? 1 : -1]

/* The glue's numbers must be the SDK's. A mismatch is a build error here, not a pad that quietly reads wrong. */
MPAD_ASSERT(MPAD_TYPE_DIGITAL == PAD_TYPE_DIGITAL, pad_type_digital);
MPAD_ASSERT(MPAD_TYPE_ANALOG == PAD_TYPE_ANALOG, pad_type_analog);
MPAD_ASSERT(MPAD_TYPE_DUALSHOCK == PAD_TYPE_DUALSHOCK, pad_type_dualshock);
MPAD_ASSERT(MPAD_SELECT == PAD_SELECT && MPAD_L3 == PAD_L3 && MPAD_R3 == PAD_R3 && MPAD_START == PAD_START, buttons_1);
MPAD_ASSERT(MPAD_UP == PAD_UP && MPAD_RIGHT == PAD_RIGHT && MPAD_DOWN == PAD_DOWN && MPAD_LEFT == PAD_LEFT, buttons_2);
MPAD_ASSERT(MPAD_L2 == PAD_L2 && MPAD_R2 == PAD_R2 && MPAD_L1 == PAD_L1 && MPAD_R1 == PAD_R1, buttons_3);
MPAD_ASSERT(MPAD_TRIANGLE == PAD_TRIANGLE && MPAD_CIRCLE == PAD_CIRCLE && MPAD_CROSS == PAD_CROSS && MPAD_SQUARE == PAD_SQUARE,
            buttons_4);

static uint8_t g_multitap_open(void *context, uint8_t port)
{
    (void)context;
    return mtapPortOpen(port) == 1;
}

static uint8_t g_pad_open(void *context, uint8_t port, uint8_t slot, void *dma)
{
    (void)context;
    return padPortOpen(port, slot, dma) == 1;
}

static uint8_t g_pad_state(void *context, uint8_t port, uint8_t slot)
{
    (void)context;
    switch (padGetState(port, slot)) {
    case PAD_STATE_DISCONN:
        return MPAD_STATE_DISCONNECTED;
    case PAD_STATE_FINDPAD:
        return MPAD_STATE_FINDING;
    case PAD_STATE_FINDCTP1:
        return MPAD_STATE_FINDING_CTP;
    case PAD_STATE_STABLE:
        return MPAD_STATE_STABLE;
    case PAD_STATE_ERROR:
        return MPAD_STATE_ERROR;
    default: /* PAD_STATE_EXECCMD and anything the SDK adds */
        return MPAD_STATE_BUSY;
    }
}

static uint8_t g_pad_read(void *context, uint8_t port, uint8_t slot, MpadRaw *out)
{
    struct padButtonStatus status = { 0 };
    uint8_t bytes;

    (void)context;
    bytes = padRead(port, slot, &status);
    out->ok = status.ok;
    out->mode = status.mode;
    out->buttons = status.btns;
    out->lx = status.ljoy_h;
    out->ly = status.ljoy_v;
    return bytes;
}

static int g_mode_count(void *context, uint8_t port, uint8_t slot)
{
    (void)context;
    return padInfoMode(port, slot, PAD_MODETABLE, -1);
}

static int g_mode_type(void *context, uint8_t port, uint8_t slot, int index)
{
    (void)context;
    return padInfoMode(port, slot, PAD_MODETABLE, index);
}

static uint8_t g_set_dualshock_locked(void *context, uint8_t port, uint8_t slot)
{
    (void)context;
    return padSetMainMode(port, slot, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK) == 1;
}

MpadOps mpad_ps2_ops(void)
{
    MpadOps ops;
    ops.context = 0;
    ops.multitap_open = g_multitap_open;
    ops.pad_open = g_pad_open;
    ops.pad_state = g_pad_state;
    ops.pad_read = g_pad_read;
    ops.mode_count = g_mode_count;
    ops.mode_type = g_mode_type;
    ops.set_dualshock_locked = g_set_dualshock_locked;
    return ops;
}

static int load_module(const void *irx, unsigned size)
{
    int status = 0;
    int id;

    if (!irx || !size) {
        return 0;
    }
    id = SifExecModuleBuffer((void *)irx, size, 0, 0, &status);
    return id >= 0 && status == 0;
}

int mpad_ps2_load_drivers(const MpadPs2Drivers *drivers)
{
    if (!drivers) {
        return MPAD_PS2_BAD_ARGUMENT;
    }
    if (!load_module(drivers->sio2man, drivers->size_sio2man)) {
        return MPAD_PS2_SIO2MAN_FAILED;
    }
    if (!load_module(drivers->mtapman, drivers->size_mtapman)) {
        return MPAD_PS2_MTAPMAN_FAILED;
    }
    if (!load_module(drivers->padman, drivers->size_padman)) {
        return MPAD_PS2_PADMAN_FAILED;
    }
    if (mtapInit() != 1 || padInit(0) != 1) {
        return MPAD_PS2_RPC_FAILED;
    }
    return 0;
}

int mpad_ps2_start(MpadPoller *poller, const MpadPs2Drivers *drivers, uint8_t search_grace)
{
    MpadOps ops;
    int loaded = mpad_ps2_load_drivers(drivers);

    if (loaded < 0) {
        return loaded;
    }
    if (!poller) {
        return MPAD_PS2_BAD_ARGUMENT;
    }
    ops = mpad_ps2_ops();
    return mpad_init(poller, &ops, search_grace);
}
