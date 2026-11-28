/* The input bridge: a port's pad state, as the core's retro_input_state callback sees it. */
#include <stdlib.h>
#include <string.h>

#include "lgp/lgp.h"

/* The libretro joypad ids in order. The ids are part of libretro's ABI; the test checks them against libretro.h. */
static const char *const NAMES[16] = {"b", "y", "select", "start", "up", "down", "left", "right",
                                      "a", "x", "l",      "r",     "l2", "r2",   "l3",   "r3"};

int lgp_button_id(const char *name)
{
    unsigned i;
    if (!name) {
        return -1;
    }
    for (i = 0; i < 16; ++i) {
        if (strcmp(name, NAMES[i]) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int lgp_map_parse(const char *text, LgpButtonMap *entry)
{
    char name[16];
    const char *eq = text ? strchr(text, '=') : NULL;
    char *end;
    unsigned long mask;
    int id;

    if (!eq || (size_t)(eq - text) >= sizeof(name) || eq == text) {
        return 0;
    }
    memcpy(name, text, (size_t)(eq - text));
    name[eq - text] = '\0';
    id = lgp_button_id(name);
    if (id < 0 || !eq[1]) {
        return 0;
    }
    mask = strtoul(eq + 1, &end, 0);
    if (*end || mask == 0 || mask > 0xffffu) {
        return 0;
    }
    entry->retro_id = (unsigned)id;
    entry->mask = (uint16_t)mask;
    return 1;
}

void lgp_pads_init(LgpPads *pads, const LgpButtonMap *map, size_t map_count, unsigned ports)
{
    unsigned i;
    memset(pads, 0, sizeof(*pads));
    pads->map_count = (unsigned)(map_count < LGP_MAX_MAP ? map_count : LGP_MAX_MAP);
    if (map && pads->map_count) {
        memcpy(pads->map, map, pads->map_count * sizeof(LgpButtonMap));
    }
    pads->ports = ports > LGP_MAX_PORTS ? LGP_MAX_PORTS : ports;
    for (i = 0; i < LGP_MAX_PORTS; ++i) {
        pads->pad[i] = padr_neutral(PADR_ABSENT);
    }
}

/* The device and index numbers are libretro's: RETRO_DEVICE_JOYPAD 1, RETRO_DEVICE_ANALOG 5, index 0 the left stick,
 * ids 0 and 1 the X and Y axes, id 256 the whole joypad as a mask. */
int16_t lgp_pads_state(const LgpPads *pads, unsigned port, unsigned device, unsigned index, unsigned id)
{
    const PadrSample *pad;
    unsigned mask = 0, i;

    if (port >= pads->ports || port >= LGP_MAX_PORTS) {
        return 0;
    }
    pad = &pads->pad[port];
    if (pad->connected != PADR_FRESH) {
        return 0;
    }
    if (device == 5u) {
        if (index != 0u) {
            return 0;
        }
        if (id == 0u) {
            return (int16_t)(((int)pad->lx - 128) * 256);
        }
        if (id == 1u) {
            return (int16_t)(((int)pad->ly - 128) * 256);
        }
        return 0;
    }
    if (device != 1u) {
        return 0;
    }
    for (i = 0; i < pads->map_count; ++i) {
        if (pad->buttons & pads->map[i].mask) {
            mask |= 1u << pads->map[i].retro_id;
        }
    }
    if (id == 256u) {
        return (int16_t)mask;
    }
    return id < 16u && (mask & (1u << id)) ? 1 : 0;
}

/* A port driven by the frame hook: one bit per joypad id. Analog index 2 is RETRO_DEVICE_INDEX_ANALOG_BUTTON, which
 * cores such as Flycast read for the L2 and R2 triggers: a pressed button reads fully pressed. */
int16_t lgp_joypad_state(uint16_t joypad, unsigned device, unsigned index, unsigned id)
{
    if (device == 1u) {
        if (id == 256u) {
            return (int16_t)joypad;
        }
        return id < 16u && (joypad & (1u << id)) ? 1 : 0;
    }
    if (device == 5u && index == 2u && id < 16u) {
        return (joypad & (1u << id)) ? 0x7fff : 0;
    }
    return 0;
}
