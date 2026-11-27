/* ps2-multitap-pads: a poller for up to eight PlayStation 2 controllers through two multitaps. Part of ps2-multitap-pads 1.0.0 (MIT). */
#ifndef MPAD_H
#define MPAD_H

#include <stdint.h>

#define MPAD_VERSION "1.0.0"

/* 64-byte alignment for the driver's buffers (C99 has no standard spelling). */
#if defined(_MSC_VER)
#define MPAD_ALIGN64_BEFORE __declspec(align(64))
#define MPAD_ALIGN64_AFTER
#else
#define MPAD_ALIGN64_BEFORE
#define MPAD_ALIGN64_AFTER __attribute__((aligned(64)))
#endif

#define MPAD_PORTS 2
#define MPAD_SLOTS 4 /* the slots of a multitap; a pad plugged straight into a port is slot 0 */
#define MPAD_ENDPOINTS (MPAD_PORTS * MPAD_SLOTS)
#define MPAD_DMA_BYTES 256 /* the buffer the pad driver fills for each endpoint */
#define MPAD_DEFAULT_SEARCH_GRACE 30 /* frames a pad may be missing before it counts as unplugged */
#define MPAD_AXIS_CENTER 128

/* Endpoint number = port * 4 + slot: 0..3 on port 1, 4..7 on port 2. */
#define MPAD_ENDPOINT(port, slot) ((uint8_t)((port) * MPAD_SLOTS + (slot)))

/* The buttons in a sample's `held`, one bit each, set while pressed (the hardware reports them the other way round). */
enum {
    MPAD_SELECT = 0x0001,
    MPAD_L3 = 0x0002,
    MPAD_R3 = 0x0004,
    MPAD_START = 0x0008,
    MPAD_UP = 0x0010,
    MPAD_RIGHT = 0x0020,
    MPAD_DOWN = 0x0040,
    MPAD_LEFT = 0x0080,
    MPAD_L2 = 0x0100,
    MPAD_R2 = 0x0200,
    MPAD_L1 = 0x0400,
    MPAD_R1 = 0x0800,
    MPAD_TRIANGLE = 0x1000,
    MPAD_CIRCLE = 0x2000,
    MPAD_CROSS = 0x4000,
    MPAD_SQUARE = 0x8000
};

/* What a poll says about one endpoint. */
enum {
    MPAD_ABSENT = 0, /* nothing there (or never opened): buttons none, sticks centred */
    MPAD_FRESH = 1, /* a pad was read this frame: `held` and the sticks are real */
    MPAD_UNREADABLE = 2 /* a pad is there but gave no sample this frame (still being found, switching mode): neutral */
};

typedef struct MpadSample {
    uint16_t held;
    uint8_t lx; /* left and right sticks, 0..255, centre 128; centred unless the pad is analog */
    uint8_t ly;
    uint8_t rx;
    uint8_t ry;
    uint8_t status; /* MPAD_ABSENT, MPAD_FRESH or MPAD_UNREADABLE */
} MpadSample;

/* The pad driver's state for an endpoint, as the poller sees it. The PS2SDK glue maps libpad's PAD_STATE_* onto these. */
enum {
    MPAD_STATE_DISCONNECTED = 0,
    MPAD_STATE_FINDING = 1, /* a pad was lost or has not been found yet */
    MPAD_STATE_FINDING_CTP = 2, /* found; learning what it is. Readable. */
    MPAD_STATE_BUSY = 3, /* the driver is running a command; not readable and not counted as missing */
    MPAD_STATE_STABLE = 4, /* readable, and the pad's mode may be changed */
    MPAD_STATE_ERROR = 5 /* treated like FINDING */
};

/* Pad types, the high nibble of a raw sample's mode byte (the hardware's numbers). */
enum { MPAD_TYPE_DIGITAL = 4, MPAD_TYPE_ANALOG = 5, MPAD_TYPE_DUALSHOCK = 7 };

/* One raw pad sample, as the driver gives it. `buttons` is active low (a pressed button is a 0 bit). */
typedef struct MpadRaw {
    uint8_t ok; /* 0 when the sample is good */
    uint8_t mode; /* pad type in the high nibble */
    uint16_t buttons;
    uint8_t lx;
    uint8_t ly;
    uint8_t rx;
    uint8_t ry;
} MpadRaw;

/*
 * The pad driver, as a table of functions. The PS2SDK glue (mpad/ps2.h) fills it with libpad and libmtap calls; a
 * host test fills it with a fake and drives the whole poller with no console. Every function gets `context` first.
 * A missing function (NULL) is a bad table: mpad_init returns 0.
 */
typedef struct MpadOps {
    void *context;
    uint8_t (*multitap_open)(void *context, uint8_t port); /* the result is not used: a port without a tap still works */
    uint8_t (*pad_open)(void *context, uint8_t port, uint8_t slot, void *dma); /* 1 when opened */
    uint8_t (*pad_state)(void *context, uint8_t port, uint8_t slot); /* an MPAD_STATE_* */
    uint8_t (*pad_read)(void *context, uint8_t port, uint8_t slot, MpadRaw *out); /* bytes the sample holds; 0 on failure */
    /* the pad's mode table (what it can be switched to), for asking a digital pad to become a DualShock */
    int (*mode_count)(void *context, uint8_t port, uint8_t slot);
    int (*mode_type)(void *context, uint8_t port, uint8_t slot, int index);
    uint8_t (*set_dualshock_locked)(void *context, uint8_t port, uint8_t slot); /* 1 when the request was accepted */
} MpadOps;

typedef struct MpadSlot {
    uint8_t opened;
    uint8_t mode_checked;
    uint8_t occupied;
    uint8_t search_frames;
} MpadSlot;

/*
 * The poller. Keep it in static storage (or anywhere that lives as long as the pads are open): the pad driver writes
 * into its DMA buffers behind the program's back, and wants them 64-byte aligned.
 */
typedef MPAD_ALIGN64_BEFORE struct MpadPoller {
    uint8_t dma[MPAD_ENDPOINTS][MPAD_DMA_BYTES] MPAD_ALIGN64_AFTER;
    MpadSlot slots[MPAD_ENDPOINTS];
    MpadOps ops;
    uint8_t search_grace;
} MpadPoller;

/*
 * Opens both multitap ports and the eight pad endpoints. `search_grace` is how many consecutive "pad is being
 * searched for" frames a pad that was reading may have before it counts as gone (0 = MPAD_DEFAULT_SEARCH_GRACE; it
 * holds a lost pad's last "unreadable" status that long, so a loose plug does not flicker the game's player slot).
 * Returns the number of endpoints that opened (0..8), or 0 when `ops` is not a complete table. An endpoint that did
 * not open polls as MPAD_ABSENT for ever; `mpad_is_open` says which did.
 */
uint8_t mpad_init(MpadPoller *poller, const MpadOps *ops, uint8_t search_grace);
uint8_t mpad_is_open(const MpadPoller *poller, uint8_t endpoint);

/* One frame: fills `out` with a sample for each of the eight endpoints (index = endpoint number). */
void mpad_poll(MpadPoller *poller, MpadSample out[MPAD_ENDPOINTS]);

/* A sample with no buttons, centred sticks and the given status. */
MpadSample mpad_neutral(uint8_t status);

#endif
