/* The input bridge: button names, the map, and what retro_input_state returns. */
#include <string.h>

#include "check.h"
#include "libretro.h"
#include "lgp/lgp.h"

int main(void)
{
    LgpPads pads;
    LgpButtonMap map[3];
    LgpButtonMap entry;

    /* the ids in lgp_button_id are libretro's: check them against the header */
    CHECK(lgp_button_id("b") == RETRO_DEVICE_ID_JOYPAD_B && lgp_button_id("y") == RETRO_DEVICE_ID_JOYPAD_Y &&
              lgp_button_id("select") == RETRO_DEVICE_ID_JOYPAD_SELECT &&
              lgp_button_id("start") == RETRO_DEVICE_ID_JOYPAD_START && lgp_button_id("up") == RETRO_DEVICE_ID_JOYPAD_UP &&
              lgp_button_id("down") == RETRO_DEVICE_ID_JOYPAD_DOWN && lgp_button_id("left") == RETRO_DEVICE_ID_JOYPAD_LEFT &&
              lgp_button_id("right") == RETRO_DEVICE_ID_JOYPAD_RIGHT && lgp_button_id("a") == RETRO_DEVICE_ID_JOYPAD_A &&
              lgp_button_id("x") == RETRO_DEVICE_ID_JOYPAD_X && lgp_button_id("l") == RETRO_DEVICE_ID_JOYPAD_L &&
              lgp_button_id("r") == RETRO_DEVICE_ID_JOYPAD_R && lgp_button_id("l2") == RETRO_DEVICE_ID_JOYPAD_L2 &&
              lgp_button_id("r2") == RETRO_DEVICE_ID_JOYPAD_R2 && lgp_button_id("l3") == RETRO_DEVICE_ID_JOYPAD_L3 &&
              lgp_button_id("r3") == RETRO_DEVICE_ID_JOYPAD_R3,
          "joypad names against libretro.h");
    CHECK(lgp_button_id("Start") == -1 && lgp_button_id("") == -1 && lgp_button_id(NULL) == -1 && lgp_button_id("l4") == -1,
          "unknown names");
    CHECK(RETRO_DEVICE_JOYPAD == 1 && RETRO_DEVICE_ANALOG == 5 && RETRO_DEVICE_INDEX_ANALOG_LEFT == 0 &&
              RETRO_DEVICE_ID_ANALOG_X == 0 && RETRO_DEVICE_ID_ANALOG_Y == 1 && RETRO_DEVICE_ID_JOYPAD_MASK == 256,
          "the device numbers lgp_pads_state uses");

    CHECK(lgp_map_parse("start=0x0008", &entry) && entry.retro_id == 3 && entry.mask == 8, "start=0x0008");
    CHECK(lgp_map_parse("b=16384", &entry) && entry.retro_id == 0 && entry.mask == 0x4000, "decimal");
    CHECK(!lgp_map_parse("start", &entry) && !lgp_map_parse("start=", &entry) && !lgp_map_parse("=8", &entry) &&
              !lgp_map_parse("nope=8", &entry) && !lgp_map_parse("start=0", &entry) && !lgp_map_parse("start=0x10000", &entry) &&
              !lgp_map_parse("start=8x", &entry) && !lgp_map_parse(NULL, &entry),
          "bad map entries");

    map[0].retro_id = RETRO_DEVICE_ID_JOYPAD_START;
    map[0].mask = 0x0008;
    map[1].retro_id = RETRO_DEVICE_ID_JOYPAD_B;
    map[1].mask = 0x4000;
    map[2].retro_id = RETRO_DEVICE_ID_JOYPAD_LEFT;
    map[2].mask = 0x0080 | 0x0100; /* either of two bits */
    lgp_pads_init(&pads, map, 3, 2);

    CHECK(lgp_pads_state(&pads, 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START) == 0, "an absent pad reads zero");
    pads.pad[0].connected = PADR_FRESH;
    pads.pad[0].lx = 128;
    pads.pad[0].ly = 128;
    pads.pad[0].buttons = 0x0008 | 0x0100;
    CHECK(lgp_pads_state(&pads, 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START) == 1, "start is down");
    CHECK(lgp_pads_state(&pads, 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) == 0, "b is not");
    CHECK(lgp_pads_state(&pads, 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT) == 1, "any bit of a mask counts");
    CHECK(lgp_pads_state(&pads, 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK) == (int16_t)((1 << 3) | (1 << 6)),
          "the whole joypad as a mask");
    CHECK(lgp_pads_state(&pads, 0, RETRO_DEVICE_JOYPAD, 0, 16) == 0 && lgp_pads_state(&pads, 0, RETRO_DEVICE_JOYPAD, 0, 300) == 0,
          "ids that are not buttons");
    CHECK(lgp_pads_state(&pads, 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START) == 0, "the other port is still absent");
    CHECK(lgp_pads_state(&pads, 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START) == 0, "a port past the ones fed");
    CHECK(lgp_pads_state(&pads, 0, RETRO_DEVICE_MOUSE, 0, 0) == 0, "a device that is not a pad");

    /* the left stick: (axis - 128) * 256, the same range a libretro frontend gives */
    pads.pad[0].lx = 255;
    pads.pad[0].ly = 0;
    CHECK(lgp_pads_state(&pads, 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) == 127 * 256,
          "stick x full right");
    CHECK(lgp_pads_state(&pads, 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) == -32768,
          "stick y full up");
    pads.pad[0].lx = 128;
    CHECK(lgp_pads_state(&pads, 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) == 0,
          "stick at rest");
    CHECK(lgp_pads_state(&pads, 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X) == 0,
          "no right stick");

    /* a script plays through the bridge, tick by tick */
    {
        static const char text[] = "0 0 0000 128 128 1\n10 0 0008 128 128 1\n15 0 0000 128 128 1\n20 1 4000 128 128 1\n";
        PadrScript *s = padr_load_text(text, sizeof(text) - 1, NULL, NULL);
        PadrSample states[8];
        unsigned tick;
        unsigned seen_start = 0, seen_b = 0;
        CHECK(s != NULL, "the script loads");
        for (tick = 0; s && tick < 30; ++tick) {
            padr_sample(s, tick, states, 8);
            pads.pad[0] = states[0];
            pads.pad[1] = states[1];
            seen_start += (unsigned)lgp_pads_state(&pads, 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
            seen_b += (unsigned)lgp_pads_state(&pads, 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
        }
        CHECK(seen_start == 5, "start is down for ticks 10 to 14, saw %u", seen_start);
        CHECK(seen_b == 10, "port 1's b is down from tick 20, saw %u", seen_b);
        padr_free(s);
    }

    printf("input_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
