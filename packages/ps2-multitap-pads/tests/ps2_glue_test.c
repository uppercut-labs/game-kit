/* ps2_glue_test: src/ps2.c built against stand-in SDK headers (tests/ps2sdk-stub) with fake SDK functions. It checks
 * the glue's own work: the order modules load in, the failure codes, the state and sample mapping. It does not run the
 * real PS2SDK or a console. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libmtap.h>
#include <libpad.h>
#include <loadfile.h>

#include "mpad/ps2.h"

static int checks, failures;

#define CHECK(cond, ...)                                 \
    do {                                                 \
        checks++;                                        \
        if (!(cond)) {                                   \
            failures++;                                  \
            fprintf(stderr, "FAIL line %d: ", __LINE__); \
            fprintf(stderr, __VA_ARGS__);                \
            fputc('\n', stderr);                         \
        }                                                \
    } while (0)

/* ---- the fake SDK ---- */
static const void *loaded[8];
static int loaded_n;
static int fail_load_index = -1, load_status;
static int mtap_init_result = 1, pad_init_result = 1, mtap_inits, pad_inits;
static int events[32], event_n; /* 1 = module, 2 = mtapInit, 3 = padInit, 4 = mtapPortOpen, 5 = padPortOpen */
static int sdk_state[2][4];
static struct padButtonStatus sdk_status[2][4];
static unsigned char sdk_bytes[2][4];
static int sdk_modes[2][4][3], sdk_mode_n[2][4], sdk_set_result = 1, sdk_set_calls, sdk_set_mode, sdk_set_lock;
static int sdk_pad_open_result = 1;

int SifExecModuleBuffer(void *ptr, unsigned int size, unsigned int arg_len, const char *args, int *mod_res)
{
    (void)size;
    (void)arg_len;
    (void)args;
    events[event_n++] = 1;
    loaded[loaded_n] = ptr;
    *mod_res = loaded_n == fail_load_index ? load_status : 0;
    return loaded_n++ == fail_load_index && load_status == 0 ? -1 : 100 + loaded_n;
}
int mtapInit(void)
{
    events[event_n++] = 2;
    mtap_inits++;
    return mtap_init_result;
}
int padInit(int mode)
{
    (void)mode;
    events[event_n++] = 3;
    pad_inits++;
    return pad_init_result;
}
int mtapPortOpen(int port)
{
    (void)port;
    events[event_n++] = 4;
    return 1;
}
int padPortOpen(int port, int slot, void *area)
{
    (void)port;
    (void)slot;
    (void)area;
    events[event_n++] = 5;
    return sdk_pad_open_result;
}
unsigned char padRead(int port, int slot, struct padButtonStatus *data)
{
    *data = sdk_status[port][slot];
    return sdk_bytes[port][slot];
}
int padGetState(int port, int slot)
{
    return sdk_state[port][slot];
}
int padInfoMode(int port, int slot, int info, int index)
{
    if (info != PAD_MODETABLE) {
        return -99;
    }
    return index < 0 ? sdk_mode_n[port][slot] : sdk_modes[port][slot][index];
}
int padSetMainMode(int port, int slot, int mode, int lock)
{
    (void)port;
    (void)slot;
    sdk_set_calls++;
    sdk_set_mode = mode;
    sdk_set_lock = lock;
    return sdk_set_result;
}

static unsigned char irx_a[4], irx_b[4], irx_c[4];
static MpadPoller poller;

static void reset(void)
{
    memset(loaded, 0, sizeof(loaded));
    loaded_n = 0;
    fail_load_index = -1;
    load_status = 0;
    mtap_init_result = pad_init_result = 1;
    mtap_inits = pad_inits = 0;
    event_n = 0;
    memset(sdk_state, 0, sizeof(sdk_state));
    memset(sdk_status, 0, sizeof(sdk_status));
    memset(sdk_bytes, 0, sizeof(sdk_bytes));
    memset(sdk_modes, 0, sizeof(sdk_modes));
    memset(sdk_mode_n, 0, sizeof(sdk_mode_n));
    sdk_set_result = 1;
    sdk_set_calls = 0;
    sdk_pad_open_result = 1;
}

static MpadPs2Drivers drivers(void)
{
    MpadPs2Drivers d;
    d.sio2man = irx_a;
    d.size_sio2man = sizeof(irx_a);
    d.mtapman = irx_b;
    d.size_mtapman = sizeof(irx_b);
    d.padman = irx_c;
    d.size_padman = sizeof(irx_c);
    return d;
}

static void test_start(void)
{
    MpadPs2Drivers d = drivers();
    int i, taps_before_pads = 1;

    reset();
    CHECK(mpad_ps2_start(&poller, &d, 0) == 8, "everything opens");
    CHECK(loaded_n == 3 && loaded[0] == irx_a && loaded[1] == irx_b && loaded[2] == irx_c,
          "sio2man, then mtapman, then padman");
    CHECK(events[0] == 1 && events[1] == 1 && events[2] == 1 && events[3] == 2 && events[4] == 3,
          "three modules, then mtapInit, then padInit");
    for (i = 5; i < event_n; i++) {
        if (events[i] == 4 && i > 6) {
            taps_before_pads = 0;
        }
    }
    CHECK(events[5] == 4 && events[6] == 4 && taps_before_pads, "both multitap ports open before any pad");
    CHECK(event_n == 5 + 2 + 8, "nothing else is called (%d events)", event_n);

    /* each failure has its own code and stops there */
    reset();
    fail_load_index = 0;
    load_status = 0;
    CHECK(mpad_ps2_start(&poller, &d, 0) == MPAD_PS2_SIO2MAN_FAILED && loaded_n == 1, "sio2man fails by id");
    reset();
    fail_load_index = 1;
    load_status = 5;
    CHECK(mpad_ps2_start(&poller, &d, 0) == MPAD_PS2_MTAPMAN_FAILED && loaded_n == 2, "mtapman fails by status");
    reset();
    fail_load_index = 2;
    load_status = 1;
    CHECK(mpad_ps2_start(&poller, &d, 0) == MPAD_PS2_PADMAN_FAILED && mtap_inits == 0, "padman fails before any RPC");
    reset();
    mtap_init_result = 0;
    CHECK(mpad_ps2_start(&poller, &d, 0) == MPAD_PS2_RPC_FAILED, "mtapInit fails");
    reset();
    pad_init_result = 0;
    CHECK(mpad_ps2_start(&poller, &d, 0) == MPAD_PS2_RPC_FAILED, "padInit fails");
    reset();
    d.padman = NULL;
    CHECK(mpad_ps2_start(&poller, &d, 0) == MPAD_PS2_PADMAN_FAILED, "a missing buffer is a failed module");
    d = drivers();
    CHECK(mpad_ps2_start(&poller, NULL, 0) == MPAD_PS2_BAD_ARGUMENT, "no drivers");
    CHECK(mpad_ps2_start(NULL, &d, 0) == MPAD_PS2_BAD_ARGUMENT, "no poller");

    /* pads that do not open are counted, not an error */
    reset();
    sdk_pad_open_result = 0;
    CHECK(mpad_ps2_start(&poller, &d, 0) == 0, "no pad opens: zero endpoints, not a failure code");
}

static void test_mapping(void)
{
    MpadPs2Drivers d = drivers();
    MpadSample s[MPAD_ENDPOINTS];

    reset();
    CHECK(mpad_ps2_start(&poller, &d, 0) == 8, "start");

    /* PAD_STATE_* to the poller's states: a DualShock on port 2 slot 3 (endpoint 6), then each state */
    sdk_state[1][2] = PAD_STATE_STABLE;
    sdk_bytes[1][2] = 8;
    sdk_status[1][2].ok = 0;
    sdk_status[1][2].mode = (unsigned char)(PAD_TYPE_DUALSHOCK << 4 | 4);
    sdk_status[1][2].btns = (unsigned short)~(PAD_CROSS | PAD_L2);
    sdk_status[1][2].ljoy_h = 12;
    sdk_status[1][2].ljoy_v = 34;
    sdk_status[1][2].rjoy_h = 200;
    sdk_status[1][2].rjoy_v = 201;
    mpad_poll(&poller, s);
    CHECK(s[6].status == MPAD_FRESH && s[6].held == (MPAD_CROSS | MPAD_L2) && s[6].lx == 12 && s[6].ly == 34,
          "sample mapped: status %u held %04x sticks %u,%u (left stick, not right)", s[6].status, s[6].held, s[6].lx, s[6].ly);
    CHECK(s[0].status == MPAD_ABSENT && s[5].status == MPAD_ABSENT, "the others are absent");

    sdk_state[1][2] = PAD_STATE_EXECCMD;
    mpad_poll(&poller, s);
    CHECK(s[6].status == MPAD_UNREADABLE, "EXECCMD is busy: unreadable, still occupied");
    sdk_state[1][2] = PAD_STATE_FINDCTP1;
    mpad_poll(&poller, s);
    CHECK(s[6].status == MPAD_FRESH, "FINDCTP1 reads");
    sdk_state[1][2] = PAD_STATE_FINDPAD;
    mpad_poll(&poller, s);
    CHECK(s[6].status == MPAD_UNREADABLE && poller.slots[6].search_frames == 1, "FINDPAD counts as searching");
    sdk_state[1][2] = PAD_STATE_ERROR;
    mpad_poll(&poller, s);
    CHECK(s[6].status == MPAD_UNREADABLE && poller.slots[6].search_frames == 2, "ERROR counts as searching");
    sdk_state[1][2] = PAD_STATE_DISCONN;
    mpad_poll(&poller, s);
    CHECK(s[6].status == MPAD_ABSENT, "DISCONN is absent");

    /* a digital pad with a DualShock mode in its table: asked for DUALSHOCK and LOCK */
    sdk_state[0][1] = PAD_STATE_STABLE;
    sdk_bytes[0][1] = 4;
    sdk_status[0][1].mode = (unsigned char)(PAD_TYPE_DIGITAL << 4 | 2);
    sdk_status[0][1].btns = 0xffff;
    sdk_mode_n[0][1] = 2;
    sdk_modes[0][1][0] = PAD_TYPE_DIGITAL;
    sdk_modes[0][1][1] = PAD_TYPE_DUALSHOCK;
    mpad_poll(&poller, s);
    CHECK(s[1].status == MPAD_UNREADABLE && sdk_set_calls == 1 && sdk_set_mode == PAD_MMODE_DUALSHOCK && sdk_set_lock == PAD_MMODE_LOCK,
          "mode request: %d call(s), mode %d lock %d", sdk_set_calls, sdk_set_mode, sdk_set_lock);
}

int main(void)
{
    test_start();
    test_mapping();
    printf("ps2_glue_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
