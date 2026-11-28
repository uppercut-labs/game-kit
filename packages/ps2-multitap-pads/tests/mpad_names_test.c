/* mpad_names_test: mpad/sample.h on its own (no poller, no PS2SDK), with the short names on. It checks that the short
 * names are the MPAD_ numbers, and that the button list names every bit once. */
#define MPAD_SHORT_NAMES 1
#include "mpad/sample.h"

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

/* a game's own controller type, written against the short names, is the sample */
typedef MpadSample Controller;

typedef struct Name {
    const char *name;
    unsigned bit;
} Name;

#define NAME_ENTRY(name, bit) { name, (unsigned)(bit) },
static const Name names[] = { MPAD_FOR_EACH_BUTTON(NAME_ENTRY) };

int main(void)
{
    unsigned seen = 0, i;
    int in_order = 1, one_bit_each = 1;
    Controller pad = { (uint16_t)(B_X | B_START), 128, 128, 128, 128, INPUT_FRESH };

    CHECK(B_SELECT == 0x0001 && B_L3 == 0x0002 && B_R3 == 0x0004 && B_START == 0x0008, "select, sticks, start");
    CHECK(B_UP == 0x0010 && B_RIGHT == 0x0020 && B_DOWN == 0x0040 && B_LEFT == 0x0080, "the d-pad");
    CHECK(B_L2 == 0x0100 && B_R2 == 0x0200 && B_L1 == 0x0400 && B_R1 == 0x0800, "the shoulders");
    CHECK(B_TRIANGLE == 0x1000 && B_CIRCLE == 0x2000 && B_X == 0x4000 && B_SQUARE == 0x8000, "the face buttons");
    CHECK(B_X == MPAD_X && MPAD_X == MPAD_CROSS, "X is the SDK's cross bit");
    CHECK(INPUT_ABSENT == MPAD_ABSENT && INPUT_FRESH == MPAD_FRESH && INPUT_UNREADABLE == MPAD_UNREADABLE, "statuses");
    CHECK(sizeof(MpadSample) == 8u, "a sample is 8 bytes (%u)", (unsigned)sizeof(MpadSample));
    CHECK(pad.status == INPUT_FRESH && (pad.held & B_X) && !(pad.held & B_CIRCLE), "a sample built from the names");

    CHECK(sizeof(names) / sizeof(names[0]) == MPAD_BUTTON_COUNT, "the list has %u entries",
          (unsigned)(sizeof(names) / sizeof(names[0])));
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (names[i].bit != (1u << i)) {
            in_order = 0;
        }
        if (seen & names[i].bit) {
            one_bit_each = 0;
        }
        seen |= names[i].bit;
    }
    CHECK(in_order && one_bit_each && seen == 0xffffu, "every bit once, in bit order (seen %04x)", seen);
    CHECK(strcmp(names[14].name, "X") == 0 && strcmp(names[0].name, "Select") == 0, "printed names");

    printf("mpad_names_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
