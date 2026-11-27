/* mpad_test: the pad poller against a fake pad driver. No console, no display. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mpad/mpad.h"

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

/* ---- the fake driver: one scripted pad per endpoint, and a log of what the poller asked ---- */

typedef struct Fake {
    uint8_t tap_opened[MPAD_PORTS];
    int calls_before_first_pad_open; /* multitap opens seen when the first pad opened */
    int taps_when_first_pad_opened;
    int pad_opens;
    uint8_t open_result[MPAD_ENDPOINTS];
    void *dma[MPAD_ENDPOINTS];
    uint8_t state[MPAD_ENDPOINTS];
    MpadRaw raw[MPAD_ENDPOINTS];
    uint8_t bytes[MPAD_ENDPOINTS];
    int mode_n[MPAD_ENDPOINTS];
    int mode_list[MPAD_ENDPOINTS][4];
    uint8_t set_result[MPAD_ENDPOINTS];
    int set_calls[MPAD_ENDPOINTS];
    int reads[MPAD_ENDPOINTS];
    int bad_port_or_slot;
} Fake;

static int endpoint_of(Fake *f, uint8_t port, uint8_t slot)
{
    if (port >= MPAD_PORTS || slot >= MPAD_SLOTS) {
        f->bad_port_or_slot++;
        return 0;
    }
    return port * MPAD_SLOTS + slot;
}

static uint8_t fk_tap(void *c, uint8_t port)
{
    Fake *f = c;
    if (port < MPAD_PORTS) {
        f->tap_opened[port]++;
    } else {
        f->bad_port_or_slot++;
    }
    return 1;
}
static uint8_t fk_open(void *c, uint8_t port, uint8_t slot, void *dma)
{
    Fake *f = c;
    int e = endpoint_of(f, port, slot);
    if (f->pad_opens++ == 0) {
        f->taps_when_first_pad_opened = f->tap_opened[0] + f->tap_opened[1];
    }
    f->dma[e] = dma;
    return f->open_result[e];
}
static uint8_t fk_state(void *c, uint8_t port, uint8_t slot)
{
    Fake *f = c;
    return f->state[endpoint_of(f, port, slot)];
}
static uint8_t fk_read(void *c, uint8_t port, uint8_t slot, MpadRaw *out)
{
    Fake *f = c;
    int e = endpoint_of(f, port, slot);
    f->reads[e]++;
    *out = f->raw[e];
    return f->bytes[e];
}
static int fk_mode_count(void *c, uint8_t port, uint8_t slot)
{
    Fake *f = c;
    return f->mode_n[endpoint_of(f, port, slot)];
}
static int fk_mode_type(void *c, uint8_t port, uint8_t slot, int index)
{
    Fake *f = c;
    return f->mode_list[endpoint_of(f, port, slot)][index];
}
static uint8_t fk_set(void *c, uint8_t port, uint8_t slot)
{
    Fake *f = c;
    int e = endpoint_of(f, port, slot);
    f->set_calls[e]++;
    return f->set_result[e];
}

static MpadOps fake_ops(Fake *f)
{
    MpadOps ops;
    ops.context = f;
    ops.multitap_open = fk_tap;
    ops.pad_open = fk_open;
    ops.pad_state = fk_state;
    ops.pad_read = fk_read;
    ops.mode_count = fk_mode_count;
    ops.mode_type = fk_mode_type;
    ops.set_dualshock_locked = fk_set;
    return ops;
}

static void fake_reset(Fake *f)
{
    int e;
    memset(f, 0, sizeof(*f));
    for (e = 0; e < MPAD_ENDPOINTS; e++) {
        f->open_result[e] = 1;
        f->state[e] = MPAD_STATE_DISCONNECTED;
        f->raw[e].buttons = 0xffff; /* nothing pressed: the hardware reports pressed as 0 */
        f->raw[e].lx = f->raw[e].ly = f->raw[e].rx = f->raw[e].ry = 128;
        f->set_result[e] = 1;
    }
}

static MpadPoller poller; /* static: the driver wants its buffers to stay put */

static uint8_t start(Fake *f, uint8_t grace)
{
    MpadOps ops = fake_ops(f);
    return mpad_init(&poller, &ops, grace);
}

static MpadSample one(uint8_t endpoint)
{
    MpadSample s[MPAD_ENDPOINTS];
    mpad_poll(&poller, s);
    return s[endpoint];
}

/* a good sample from a pad of `type` holding `pressed` (set bits = pressed buttons) */
static void plug(Fake *f, int e, uint8_t type, uint16_t pressed, uint8_t bytes, uint8_t lx, uint8_t ly)
{
    f->state[e] = MPAD_STATE_STABLE;
    f->raw[e].ok = 0;
    f->raw[e].mode = (uint8_t)(type << 4 | (bytes / 2));
    f->raw[e].buttons = (uint16_t)~pressed;
    f->raw[e].lx = lx;
    f->raw[e].ly = ly;
    f->bytes[e] = bytes;
}

/* ---- tests ---- */

static void test_init(void)
{
    Fake f;
    int e;
    uint8_t opened;

    fake_reset(&f);
    opened = start(&f, 0);
    CHECK(opened == 8, "all eight endpoints open, got %u", opened);
    CHECK(f.tap_opened[0] == 1 && f.tap_opened[1] == 1, "each multitap port is opened once: %d %d", f.tap_opened[0],
          f.tap_opened[1]);
    CHECK(f.taps_when_first_pad_opened == 2, "both multitap ports are open before the first pad is: %d",
          f.taps_when_first_pad_opened);
    CHECK(f.bad_port_or_slot == 0, "only ports 0..1 and slots 0..3 are used");
    for (e = 0; e < MPAD_ENDPOINTS; e++) {
        int other;
        CHECK(f.dma[e] != NULL && ((uintptr_t)f.dma[e] % 64u) == 0, "endpoint %d buffer is 64-byte aligned", e);
        for (other = 0; other < e; other++) {
            uintptr_t a = (uintptr_t)f.dma[e], b = (uintptr_t)f.dma[other];
            CHECK(a >= b + MPAD_DMA_BYTES || b >= a + MPAD_DMA_BYTES, "buffers %d and %d do not overlap", e, other);
        }
        CHECK(mpad_is_open(&poller, (uint8_t)e), "endpoint %d is open", e);
    }
    CHECK(!mpad_is_open(&poller, 8) && !mpad_is_open(NULL, 0), "an endpoint out of range is not open");

    /* a pad that does not open stays absent for ever, even if the driver then says a pad is plugged in */
    fake_reset(&f);
    f.open_result[5] = 0;
    f.open_result[0] = 2; /* only 1 counts as opened */
    opened = start(&f, 0);
    CHECK(opened == 6, "two endpoints did not open, got %u", opened);
    CHECK(!mpad_is_open(&poller, 5) && !mpad_is_open(&poller, 0) && mpad_is_open(&poller, 1), "which opened");
    plug(&f, 5, MPAD_TYPE_DUALSHOCK, MPAD_CROSS, 8, 1, 2);
    CHECK(one(5).status == MPAD_ABSENT && f.reads[5] == 0, "an endpoint that did not open is never read");

    /* a bad table opens nothing */
    {
        MpadOps ops = fake_ops(&f);
        ops.pad_read = NULL;
        CHECK(mpad_init(&poller, &ops, 0) == 0 && !mpad_is_open(&poller, 0), "a table with a missing function opens nothing");
        CHECK(mpad_init(&poller, NULL, 0) == 0, "no table opens nothing");
        CHECK(mpad_init(NULL, &ops, 0) == 0, "no poller is refused");
    }
}

static void check_neutral(MpadSample s, uint8_t status, const char *what)
{
    CHECK(s.held == 0 && s.lx == 128 && s.ly == 128 && s.rx == 128 && s.ry == 128 && s.status == status,
          "%s: held %04x sticks %u,%u %u,%u status %u", what, s.held, s.lx, s.ly, s.rx, s.ry, s.status);
}

static void test_absent_and_fresh(void)
{
    Fake f;
    MpadSample s;

    fake_reset(&f);
    start(&f, 0);
    check_neutral(one(0), MPAD_ABSENT, "nothing plugged in");

    /* a digital pad: buttons only, sticks stay centred even if the bytes say otherwise */
    plug(&f, 0, MPAD_TYPE_DIGITAL, MPAD_CROSS | MPAD_START, 4, 17, 99);
    f.raw[0].rx = 211;
    f.raw[0].ry = 42;
    f.set_result[0] = 1;
    f.mode_n[0] = 0; /* no DualShock mode in its table: no request, a fresh read */
    s = one(0);
    CHECK(s.status == MPAD_FRESH && s.held == (MPAD_CROSS | MPAD_START) && s.lx == 128 && s.ly == 128 &&
              s.rx == 128 && s.ry == 128,
          "digital pad: status %u held %04x sticks %u,%u %u,%u", s.status, s.held, s.lx, s.ly, s.rx, s.ry);
    CHECK(f.set_calls[0] == 0, "a digital pad with no analog mode is not asked to change");

    /* an analog pad with a full sample: sticks are real */
    plug(&f, 1, MPAD_TYPE_DUALSHOCK, MPAD_L1 | MPAD_SQUARE | MPAD_SELECT, 8, 0, 255);
    f.raw[1].rx = 200;
    f.raw[1].ry = 12;
    s = one(1);
    CHECK(s.status == MPAD_FRESH && s.held == (MPAD_L1 | MPAD_SQUARE | MPAD_SELECT) && s.lx == 0 && s.ly == 255 &&
              s.rx == 200 && s.ry == 12,
          "dualshock: status %u held %04x sticks %u,%u %u,%u", s.status, s.held, s.lx, s.ly, s.rx, s.ry);
    plug(&f, 2, MPAD_TYPE_ANALOG, MPAD_R2, 8, 40, 200);
    f.raw[2].rx = 15;
    f.raw[2].ry = 240;
    s = one(2);
    CHECK(s.status == MPAD_FRESH && s.lx == 40 && s.ly == 200 && s.rx == 15 && s.ry == 240,
          "analog type reads both sticks");
    /* an analog type with a short sample has no stick bytes */
    plug(&f, 3, MPAD_TYPE_DUALSHOCK, MPAD_UP, 4, 40, 200);
    f.raw[3].rx = 15;
    f.raw[3].ry = 240;
    s = one(3);
    CHECK(s.status == MPAD_FRESH && s.held == MPAD_UP && s.lx == 128 && s.ly == 128 && s.rx == 128 && s.ry == 128,
          "a short analog sample keeps both sticks centred");

    /* every button bit lands where the header says */
    plug(&f, 4, MPAD_TYPE_DIGITAL, 0xffff, 4, 128, 128);
    CHECK(one(4).held == 0xffff, "all sixteen buttons held");
    plug(&f, 4, MPAD_TYPE_DIGITAL, 0, 4, 128, 128);
    CHECK(one(4).held == 0, "no button held");
    plug(&f, 4, MPAD_TYPE_DIGITAL, MPAD_LEFT, 4, 128, 128);
    CHECK(one(4).held == 0x0080, "left is bit 7");

    /* endpoints are independent and numbered port * 4 + slot */
    fake_reset(&f);
    start(&f, 0);
    plug(&f, MPAD_ENDPOINT(1, 2), MPAD_TYPE_DIGITAL, MPAD_TRIANGLE, 4, 128, 128);
    f.mode_n[6] = 0;
    {
        MpadSample all[MPAD_ENDPOINTS];
        int e;
        mpad_poll(&poller, all);
        for (e = 0; e < MPAD_ENDPOINTS; e++) {
            CHECK(all[e].status == (e == 6 ? MPAD_FRESH : MPAD_ABSENT), "endpoint %d status %u", e, all[e].status);
        }
        CHECK(all[6].held == MPAD_TRIANGLE, "port 2 slot 3 is endpoint 6");
    }
    CHECK(MPAD_ENDPOINT(0, 0) == 0 && MPAD_ENDPOINT(0, 3) == 3 && MPAD_ENDPOINT(1, 0) == 4 && MPAD_ENDPOINT(1, 3) == 7,
          "the endpoint numbering");
}

static void test_bad_reads_and_busy(void)
{
    Fake f;
    MpadSample s;

    fake_reset(&f);
    start(&f, 0);

    /* a bad read from a pad never seen reads as absent... */
    plug(&f, 0, MPAD_TYPE_DIGITAL, MPAD_CROSS, 3, 128, 128); /* too short */
    check_neutral(one(0), MPAD_ABSENT, "short sample, never seen");
    plug(&f, 0, MPAD_TYPE_DIGITAL, MPAD_CROSS, 4, 128, 128);
    f.raw[0].ok = 1;
    check_neutral(one(0), MPAD_ABSENT, "bad sample, never seen");

    /* ...and from a pad that was reading, as unreadable */
    f.raw[0].ok = 0;
    f.mode_n[0] = 0;
    CHECK(one(0).status == MPAD_FRESH, "a good read");
    f.raw[0].ok = 1;
    check_neutral(one(0), MPAD_UNREADABLE, "bad sample after a good one");
    f.bytes[0] = 2;
    f.raw[0].ok = 0;
    check_neutral(one(0), MPAD_UNREADABLE, "short sample after a good one");

    /* a command in progress is not readable and does not age the pad */
    f.state[0] = MPAD_STATE_BUSY;
    {
        int reads = f.reads[0], i;
        for (i = 0; i < 100; i++) {
            check_neutral(one(0), MPAD_UNREADABLE, "busy");
        }
        CHECK(f.reads[0] == reads, "a busy pad is not read");
        CHECK(poller.slots[0].occupied == 1 && poller.slots[0].search_frames == 0, "busy frames do not count as missing");
    }

    /* an unknown state number is treated as busy */
    f.state[0] = 99;
    check_neutral(one(0), MPAD_UNREADABLE, "unknown state");

    /* a pad that is only found (FINDING_CTP) is read, but its mode is not asked about yet */
    fake_reset(&f);
    start(&f, 0);
    plug(&f, 0, MPAD_TYPE_DIGITAL, MPAD_CIRCLE, 4, 128, 128);
    f.state[0] = MPAD_STATE_FINDING_CTP;
    f.mode_n[0] = 1;
    f.mode_list[0][0] = MPAD_TYPE_DUALSHOCK;
    s = one(0);
    CHECK(s.status == MPAD_FRESH && s.held == MPAD_CIRCLE && f.set_calls[0] == 0, "a found pad reads fresh with no mode request");
    CHECK(poller.slots[0].mode_checked == 0, "and is still to be mode-checked");
    f.state[0] = MPAD_STATE_STABLE;
    s = one(0);
    CHECK(s.status == MPAD_UNREADABLE && f.set_calls[0] == 1, "once stable the request is made");
}

static void test_search_grace(void)
{
    Fake f;
    int i;

    fake_reset(&f);
    start(&f, 0); /* the default, 30 */
    plug(&f, 0, MPAD_TYPE_DIGITAL, MPAD_CROSS, 4, 128, 128);
    f.mode_n[0] = 0;
    CHECK(one(0).status == MPAD_FRESH, "reading");

    /* the pad drops out: 29 frames of "unreadable", then absent on the 30th */
    f.state[0] = MPAD_STATE_FINDING;
    for (i = 1; i < 30; i++) {
        MpadSample s = one(0);
        CHECK(s.status == MPAD_UNREADABLE, "frame %d of searching is still unreadable (got %u)", i, s.status);
    }
    check_neutral(one(0), MPAD_ABSENT, "the 30th frame of searching");
    check_neutral(one(0), MPAD_ABSENT, "and after it");

    /* it comes back */
    f.state[0] = MPAD_STATE_STABLE;
    CHECK(one(0).status == MPAD_FRESH, "plugged in again");

    /* coming back inside the grace resets the count */
    f.state[0] = MPAD_STATE_ERROR;
    for (i = 1; i < 20; i++) {
        CHECK(one(0).status == MPAD_UNREADABLE, "error frame %d", i);
    }
    f.state[0] = MPAD_STATE_STABLE;
    CHECK(one(0).status == MPAD_FRESH && poller.slots[0].search_frames == 0, "the count starts again");
    f.state[0] = MPAD_STATE_FINDING;
    for (i = 1; i < 30; i++) {
        CHECK(one(0).status == MPAD_UNREADABLE, "second search, frame %d", i);
    }
    CHECK(one(0).status == MPAD_ABSENT, "second search ends at 30");

    /* a pad that was never seen does not become unreadable while searching */
    fake_reset(&f);
    start(&f, 0);
    f.state[3] = MPAD_STATE_FINDING;
    check_neutral(one(3), MPAD_ABSENT, "searching, never seen");

    /* a chosen grace; 255 is allowed */
    fake_reset(&f);
    start(&f, 3);
    plug(&f, 0, MPAD_TYPE_DIGITAL, 0, 4, 128, 128);
    f.mode_n[0] = 0;
    one(0);
    f.state[0] = MPAD_STATE_FINDING;
    CHECK(one(0).status == MPAD_UNREADABLE && one(0).status == MPAD_UNREADABLE && one(0).status == MPAD_ABSENT, "a grace of three");
    fake_reset(&f);
    start(&f, 255);
    plug(&f, 0, MPAD_TYPE_DIGITAL, 0, 4, 128, 128);
    f.mode_n[0] = 0;
    one(0);
    f.state[0] = MPAD_STATE_FINDING;
    for (i = 1; i < 255; i++) {
        one(0);
    }
    CHECK(one(0).status == MPAD_ABSENT && poller.slots[0].occupied == 0, "a grace of 255 ends at frame 255");

    /* a disconnect is final at once, with no grace, and clears what was known */
    fake_reset(&f);
    start(&f, 0);
    plug(&f, 0, MPAD_TYPE_DIGITAL, MPAD_START, 4, 128, 128);
    f.mode_n[0] = 0;
    one(0);
    f.state[0] = MPAD_STATE_DISCONNECTED;
    check_neutral(one(0), MPAD_ABSENT, "disconnected");
    CHECK(mpad_is_open(&poller, 0) && poller.slots[0].occupied == 0 && poller.slots[0].mode_checked == 0,
          "the port stays open, the rest is forgotten");
    f.state[0] = MPAD_STATE_FINDING;
    check_neutral(one(0), MPAD_ABSENT, "searching after a disconnect is absent, not unreadable");
}

static void test_mode_request(void)
{
    Fake f;
    MpadSample s;

    /* a digital pad whose table lists a DualShock mode is asked once, and reads as nothing for that frame */
    fake_reset(&f);
    start(&f, 0);
    plug(&f, 0, MPAD_TYPE_DIGITAL, MPAD_CROSS, 4, 128, 128);
    f.mode_n[0] = 3;
    f.mode_list[0][0] = MPAD_TYPE_DIGITAL;
    f.mode_list[0][1] = 6; /* some other type */
    f.mode_list[0][2] = MPAD_TYPE_DUALSHOCK;
    s = one(0);
    check_neutral(s, MPAD_UNREADABLE, "the frame the request is made");
    CHECK(f.set_calls[0] == 1, "one request");
    /* the driver now reports an analog pad */
    plug(&f, 0, MPAD_TYPE_DUALSHOCK, MPAD_CROSS, 8, 70, 80);
    s = one(0);
    CHECK(s.status == MPAD_FRESH && s.lx == 70 && s.ly == 80 && s.held == MPAD_CROSS, "then it reads as analog");
    CHECK(f.set_calls[0] == 1, "and is not asked again");

    /* if it is still digital (the request did nothing), no second request, just a fresh read */
    fake_reset(&f);
    start(&f, 0);
    plug(&f, 0, MPAD_TYPE_DIGITAL, MPAD_CROSS, 4, 128, 128);
    f.mode_n[0] = 1;
    f.mode_list[0][0] = MPAD_TYPE_DUALSHOCK;
    one(0);
    s = one(0);
    CHECK(s.status == MPAD_FRESH && f.set_calls[0] == 1, "one request per plug-in even if the pad stays digital");

    /* a refused request reads fresh at once, and is not retried */
    fake_reset(&f);
    start(&f, 0);
    plug(&f, 0, MPAD_TYPE_DIGITAL, MPAD_SQUARE, 4, 128, 128);
    f.mode_n[0] = 1;
    f.mode_list[0][0] = MPAD_TYPE_DUALSHOCK;
    f.set_result[0] = 0;
    s = one(0);
    CHECK(s.status == MPAD_FRESH && s.held == MPAD_SQUARE && f.set_calls[0] == 1, "a refused request is a fresh digital read");
    one(0);
    CHECK(f.set_calls[0] == 1, "not retried");

    /* an analog pad is never asked */
    fake_reset(&f);
    start(&f, 0);
    plug(&f, 0, MPAD_TYPE_DUALSHOCK, 0, 8, 128, 128);
    f.mode_n[0] = 1;
    f.mode_list[0][0] = MPAD_TYPE_DUALSHOCK;
    one(0);
    CHECK(f.set_calls[0] == 0, "an analog pad is not asked to change");

    /* plugging it out and in again asks again: the search clears the check */
    fake_reset(&f);
    start(&f, 0);
    plug(&f, 0, MPAD_TYPE_DIGITAL, 0, 4, 128, 128);
    f.mode_n[0] = 1;
    f.mode_list[0][0] = MPAD_TYPE_DUALSHOCK;
    one(0);
    f.state[0] = MPAD_STATE_FINDING;
    one(0);
    f.state[0] = MPAD_STATE_STABLE;
    one(0);
    CHECK(f.set_calls[0] == 2, "a pad found again is asked again (%d)", f.set_calls[0]);
}

static void test_misc(void)
{
    MpadSample n = mpad_neutral(MPAD_UNREADABLE);
    MpadSample all[MPAD_ENDPOINTS];
    int e;

    CHECK(n.held == 0 && n.lx == 128 && n.ly == 128 && n.rx == 128 && n.ry == 128 && n.status == MPAD_UNREADABLE,
          "mpad_neutral");
    mpad_poll(NULL, all);
    for (e = 0; e < MPAD_ENDPOINTS; e++) {
        CHECK(all[e].status == MPAD_ABSENT, "polling no poller gives absent");
    }
    CHECK(((uintptr_t)&poller.dma[0][0] % 64u) == 0 && ((uintptr_t)&poller.dma[1][0] % 64u) == 0, "the buffers are 64-aligned");
}

int main(void)
{
    test_init();
    test_absent_and_fresh();
    test_bad_reads_and_busy();
    test_search_grace();
    test_mode_request();
    test_misc();
    printf("mpad_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
