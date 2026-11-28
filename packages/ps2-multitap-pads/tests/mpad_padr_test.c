/* mpad_padr_test: the pad-replay name table in mpad/padr.h, through pad-replay's own parse and format. */
#include "mpad/padr.h"

#include <stdio.h>
#include <string.h>

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

static uint16_t parse(const char *text)
{
    uint16_t mask = 0xdead;
    return padr_buttons_parse(mpad_padr_buttons, MPAD_PADR_BUTTON_COUNT, text, &mask) ? mask : 0xdead;
}

int main(void)
{
    char text[160];
    unsigned i;

    CHECK(MPAD_PADR_BUTTON_COUNT == MPAD_BUTTON_COUNT + 1, "every button and the Cross spelling");
    CHECK(parse("R1+Down+Left") == (MPAD_R1 | MPAD_DOWN | MPAD_LEFT), "a capture-script line");
    CHECK(parse("x") == MPAD_X && parse("Cross") == MPAD_X && parse("cross+square") == (MPAD_X | MPAD_SQUARE),
          "X, and the older Cross spelling");
    CHECK(parse("L3+R3+Select+Start") == (MPAD_L3 | MPAD_R3 | MPAD_SELECT | MPAD_START), "sticks and select");
    CHECK(parse("none") == 0 && parse("Triangle+Circle") == (MPAD_TRIANGLE | MPAD_CIRCLE), "none, and the rest");
    CHECK(parse("Mode") == 0xdead, "an unknown name is refused");

    CHECK(padr_buttons_format(mpad_padr_buttons, MPAD_PADR_BUTTON_COUNT, (uint16_t)(MPAD_LEFT | MPAD_X), text,
                              sizeof text) > 0 &&
              strcmp(text, "Left+X") == 0,
          "formats in bit order with X: %s", text);
    CHECK(padr_buttons_format(mpad_padr_buttons, MPAD_PADR_BUTTON_COUNT, 0xffffu, text, sizeof text) > 0 &&
              strcmp(text, "Select+L3+R3+Start+Up+Right+Down+Left+L2+R2+L1+R1+Triangle+Circle+X+Square") == 0,
          "every button, no Cross and no hex: %s", text);
    for (i = 0; i < MPAD_BUTTON_COUNT; i++) {
        uint16_t bit = (uint16_t)(1u << i);
        CHECK(padr_buttons_format(mpad_padr_buttons, MPAD_PADR_BUTTON_COUNT, bit, text, sizeof text) > 0 &&
                  parse(text) == bit,
              "bit %04x round-trips through %s", bit, text);
    }

    printf("mpad_padr_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
