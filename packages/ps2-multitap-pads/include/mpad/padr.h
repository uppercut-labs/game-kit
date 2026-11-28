/* ps2-multitap-pads: the DualShock 2 buttons as a pad-replay name table. Part of ps2-multitap-pads 1.1.0 (MIT).
 *
 * Needs pad-replay's include directory (padr/padr.h) and nothing else: no PS2SDK, and no code to link beyond
 * pad-replay itself. With it a replay or capture script line such as "R1+Down+Left" reads into the bits a poll
 * reports, and a sample's bits print the same way:
 *
 *   uint16_t mask;
 *   padr_buttons_parse(mpad_padr_buttons, MPAD_PADR_BUTTON_COUNT, "L1+X", &mask);
 *   padr_buttons_format(mpad_padr_buttons, MPAD_PADR_BUTTON_COUNT, sample.held, text, sizeof text);
 *
 * The table lists every button once in bit order (MPAD_FOR_EACH_BUTTON), so formatting writes "X" for the bottom face
 * button. One more entry, "Cross", comes last: older scripts spell the button that way and still parse, and formatting
 * never writes it because "X" has already covered the bit.
 */
#ifndef MPAD_PADR_H
#define MPAD_PADR_H

#include "mpad/sample.h"
#include "padr/padr.h"

#define MPAD_PADR_BUTTON_ENTRY(name, bit) { name, (uint16_t)(bit) },

static const PadrButton mpad_padr_buttons[] = {
    MPAD_FOR_EACH_BUTTON(MPAD_PADR_BUTTON_ENTRY)
    { "Cross", (uint16_t)MPAD_CROSS }
};

#undef MPAD_PADR_BUTTON_ENTRY

#define MPAD_PADR_BUTTON_COUNT (sizeof(mpad_padr_buttons) / sizeof(mpad_padr_buttons[0]))

#endif
