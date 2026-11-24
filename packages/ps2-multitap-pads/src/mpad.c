/* The pad poller: no console calls, only the MpadOps table. See include/mpad/mpad.h. */
#include "mpad/mpad.h"

#include <string.h>

MpadSample mpad_neutral(uint8_t status)
{
    MpadSample s;
    s.held = 0;
    s.lx = MPAD_AXIS_CENTER;
    s.ly = MPAD_AXIS_CENTER;
    s.status = status;
    return s;
}

static uint8_t complete(const MpadOps *ops)
{
    return ops && ops->multitap_open && ops->pad_open && ops->pad_state && ops->pad_read && ops->mode_count &&
           ops->mode_type && ops->set_dualshock_locked;
}

uint8_t mpad_init(MpadPoller *poller, const MpadOps *ops, uint8_t search_grace)
{
    uint8_t port, slot, opened = 0;

    if (!poller) {
        return 0;
    }
    memset(poller->slots, 0, sizeof(poller->slots));
    memset(poller->dma, 0, sizeof(poller->dma));
    memset(&poller->ops, 0, sizeof(poller->ops));
    poller->search_grace = search_grace ? search_grace : (uint8_t)MPAD_DEFAULT_SEARCH_GRACE;
    if (!complete(ops)) {
        return 0;
    }
    poller->ops = *ops;

    /* The driver keeps looking for a multitap even when none is plugged in at start, so both ports are opened always. */
    for (port = 0; port < MPAD_PORTS; port++) {
        poller->ops.multitap_open(poller->ops.context, port);
    }
    for (port = 0; port < MPAD_PORTS; port++) {
        for (slot = 0; slot < MPAD_SLOTS; slot++) {
            uint8_t endpoint = MPAD_ENDPOINT(port, slot);
            poller->slots[endpoint].opened = poller->ops.pad_open(poller->ops.context, port, slot, poller->dma[endpoint]) == 1;
            opened = (uint8_t)(opened + poller->slots[endpoint].opened);
        }
    }
    return opened;
}

uint8_t mpad_is_open(const MpadPoller *poller, uint8_t endpoint)
{
    return poller && endpoint < MPAD_ENDPOINTS && poller->slots[endpoint].opened;
}

/* Asks a digital pad to become a DualShock and stay one, when its mode table lists that. */
static uint8_t request_analog(MpadPoller *p, uint8_t port, uint8_t slot)
{
    int count = p->ops.mode_count(p->ops.context, port, slot);
    int index;

    for (index = 0; index < count; index++) {
        if (p->ops.mode_type(p->ops.context, port, slot, index) == MPAD_TYPE_DUALSHOCK) {
            return p->ops.set_dualshock_locked(p->ops.context, port, slot) == 1;
        }
    }
    return 0;
}

static MpadSample read_endpoint(MpadPoller *p, uint8_t endpoint)
{
    MpadSlot *pad = &p->slots[endpoint];
    uint8_t port = (uint8_t)(endpoint / MPAD_SLOTS);
    uint8_t slot = (uint8_t)(endpoint % MPAD_SLOTS);
    uint8_t state, missing, bytes, type;
    MpadRaw raw;
    MpadSample sample;

    if (!pad->opened) {
        return mpad_neutral(MPAD_ABSENT);
    }
    state = p->ops.pad_state(p->ops.context, port, slot);
    if (state == MPAD_STATE_DISCONNECTED) {
        /* unplugged for certain: forget everything but the open port */
        memset(pad, 0, sizeof(*pad));
        pad->opened = 1;
        return mpad_neutral(MPAD_ABSENT);
    }
    if (state == MPAD_STATE_FINDING || state == MPAD_STATE_ERROR) {
        pad->mode_checked = 0;
        if (pad->occupied && ++pad->search_frames >= p->search_grace) {
            pad->occupied = 0;
        }
    }
    missing = pad->occupied ? MPAD_UNREADABLE : MPAD_ABSENT;
    if (state != MPAD_STATE_STABLE && state != MPAD_STATE_FINDING_CTP) {
        return mpad_neutral(missing);
    }

    memset(&raw, 0, sizeof(raw));
    bytes = p->ops.pad_read(p->ops.context, port, slot, &raw);
    if (bytes < 4 || raw.ok != 0) {
        return mpad_neutral(missing);
    }

    pad->occupied = 1;
    pad->search_frames = 0;
    type = (uint8_t)(raw.mode >> 4);
    if (state == MPAD_STATE_STABLE && !pad->mode_checked) {
        pad->mode_checked = 1; /* one request per plug-in, whether or not it was accepted */
        if (type == MPAD_TYPE_DIGITAL && request_analog(p, port, slot)) {
            /* the driver applies the new mode over the next frames; this one reads as nothing yet */
            return mpad_neutral(MPAD_UNREADABLE);
        }
    }

    sample = mpad_neutral(MPAD_FRESH);
    sample.held = (uint16_t)~raw.buttons;
    if (bytes >= 8 && (type == MPAD_TYPE_ANALOG || type == MPAD_TYPE_DUALSHOCK)) {
        sample.lx = raw.lx;
        sample.ly = raw.ly;
    }
    return sample;
}

void mpad_poll(MpadPoller *poller, MpadSample out[MPAD_ENDPOINTS])
{
    uint8_t endpoint;

    for (endpoint = 0; endpoint < MPAD_ENDPOINTS; endpoint++) {
        out[endpoint] = poller ? read_endpoint(poller, endpoint) : mpad_neutral(MPAD_ABSENT);
    }
}
