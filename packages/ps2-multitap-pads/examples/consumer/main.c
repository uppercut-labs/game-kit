/* A made-up game's input check: a loose multitap cable, driven through a pad table that is not a console.
 *
 *   my-ps2-pads
 *
 * On the console the table is mpad_ps2_ops() from mpad/ps2.h; here it is a few functions that answer from two
 * variables, so the same poller runs in a plain test.
 */
#include <stdio.h>

#include "mpad/mpad.h"

static unsigned char pad_there; /* 1 while the pad on port 1 slot 2 is plugged in */
static unsigned char cable_loose; /* 1 while its tap has lost it */

static uint8_t tap(void *c, uint8_t port) { (void)c; (void)port; return 1; }
static uint8_t open_pad(void *c, uint8_t port, uint8_t slot, void *dma) { (void)c; (void)port; (void)slot; (void)dma; return 1; }
static uint8_t state(void *c, uint8_t port, uint8_t slot)
{
    (void)c;
    if (port != 0 || slot != 1 || !pad_there) {
        return MPAD_STATE_DISCONNECTED;
    }
    return cable_loose ? MPAD_STATE_FINDING : MPAD_STATE_STABLE;
}
static uint8_t read_pad(void *c, uint8_t port, uint8_t slot, MpadRaw *out)
{
    (void)c; (void)port; (void)slot;
    out->ok = 0;
    out->mode = (uint8_t)(MPAD_TYPE_DUALSHOCK << 4 | 4);
    out->buttons = (uint16_t)~(MPAD_CROSS | MPAD_START); /* active low, as the hardware reports */
    out->lx = 200;
    out->ly = 128;
    out->rx = 128;
    out->ry = 128;
    return 8;
}
static int mode_count(void *c, uint8_t port, uint8_t slot) { (void)c; (void)port; (void)slot; return 0; }
static int mode_type(void *c, uint8_t port, uint8_t slot, int i) { (void)c; (void)port; (void)slot; (void)i; return 0; }
static uint8_t set_mode(void *c, uint8_t port, uint8_t slot) { (void)c; (void)port; (void)slot; return 0; }

static MpadPoller poller; /* static: the driver keeps its buffers */

static const char *name(uint8_t status)
{
    return status == MPAD_FRESH ? "fresh" : status == MPAD_UNREADABLE ? "unreadable" : "absent";
}

int main(void)
{
    MpadOps ops = { 0, tap, open_pad, state, read_pad, mode_count, mode_type, set_mode };
    MpadSample s[MPAD_ENDPOINTS];
    int frame, failures = 0;
    const uint8_t endpoint = MPAD_ENDPOINT(0, 1);
    /* plugged in at frame 1, loose from frame 2; with a grace of 2 the first searching frame still reads as a pad */
    const uint8_t expect[] = { MPAD_ABSENT, MPAD_FRESH, MPAD_UNREADABLE, MPAD_ABSENT, MPAD_ABSENT };

    if (mpad_init(&poller, &ops, 2) != 8) {
        return 1;
    }
    for (frame = 0; frame < 5; frame++) {
        pad_there = frame >= 1;
        cable_loose = frame >= 2;
        mpad_poll(&poller, s);
        printf("frame %d: endpoint %u %s", frame, endpoint, name(s[endpoint].status));
        if (s[endpoint].status == MPAD_FRESH) {
            printf(", cross %s, start %s, stick %u,%u", s[endpoint].held & MPAD_CROSS ? "down" : "up",
                   s[endpoint].held & MPAD_START ? "down" : "up", s[endpoint].lx, s[endpoint].ly);
        }
        printf("\n");
        if (s[endpoint].status != expect[frame]) {
            failures++;
        }
    }
    return failures;
}
