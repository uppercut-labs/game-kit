/* test_cabinet: coin credits and switch edges. Pure functions, no KOS. */
#include <stdio.h>

#include "kcab/cabinet.h"

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

static uint16_t feed(KcabCoins *c, uint16_t a, uint16_t b)
{
    uint16_t m[2];

    m[0] = a;
    m[1] = b;
    return kcab_coins_update(c, m);
}

static void test_meters(void)
{
    KcabCoins c;

    kcab_coins_init(&c, 0);
    /* coins already in the meter at power-on are not credits */
    CHECK(feed(&c, 7, 3) == 0 && c.credits == 0, "the first reading is a baseline");
    CHECK(feed(&c, 7, 3) == 0, "no change, no credit");
    CHECK(feed(&c, 8, 3) == 1 && c.credits == 1, "slot one rose by one");
    CHECK(feed(&c, 8, 5) == 2 && c.credits == 3, "slot two rose by two");
    CHECK(feed(&c, 10, 6) == 3 && c.credits == 6, "both rose in one reading");
    CHECK(feed(&c, 10, 6) == 0, "steady again");

    /* a meter that goes down (the driver took credits off it, or the board reset) re-bases with no credit */
    CHECK(feed(&c, 2, 6) == 0 && c.credits == 6, "slot one fell");
    CHECK(feed(&c, 3, 6) == 1 && c.credits == 7, "and counts from the new base");
    CHECK(feed(&c, 0, 0) == 0 && feed(&c, 1, 1) == 2 && c.credits == 9, "both fell, then rose");

    /* the largest meter value */
    kcab_coins_init(&c, 0);
    feed(&c, 0, 0);
    CHECK(feed(&c, 0x3FFF, 0) == 0x3FFF && c.credits == 0x3FFF, "a 14-bit meter from zero");

    /* spend and take */
    kcab_coins_init(&c, 0);
    feed(&c, 0, 0);
    feed(&c, 5, 0);
    CHECK(!kcab_coins_spend(&c, 6) && c.credits == 5, "cannot spend what is not there");
    CHECK(kcab_coins_spend(&c, 2) && c.credits == 3, "spend two");
    CHECK(kcab_coins_spend(&c, 0) && c.credits == 3, "spend none");
    CHECK(kcab_coins_take(&c, 2) == 2 && c.credits == 1, "take up to two");
    CHECK(kcab_coins_take(&c, 9) == 1 && c.credits == 0, "take what there is");
    CHECK(kcab_coins_take(&c, 9) == 0, "take from nothing");
}

static void test_cap_and_pulse(void)
{
    KcabCoins c;
    static KcabCoins zeroed; /* never initialised: all zero is a fresh tracker */

    CHECK(kcab_coins_pulse(&zeroed, 3) == 2 && zeroed.credits == 2, "an all-zero tracker works");
    CHECK(feed(&zeroed, 5, 5) == 0 && feed(&zeroed, 6, 5) == 1 && zeroed.credits == 3, "and takes meters");

    kcab_coins_init(&c, 99);
    feed(&c, 0, 0);
    CHECK(feed(&c, 60, 0) == 60 && feed(&c, 100, 0) == 39 && c.credits == 99, "credits stop at the cap and the return says so");
    CHECK(feed(&c, 101, 0) == 0 && c.credits == 99, "at the cap");
    CHECK(kcab_coins_spend(&c, 1) && feed(&c, 102, 0) == 1 && c.credits == 99, "room again after a spend");

    kcab_coins_init(&c, 0);
    CHECK(kcab_coins_pulse(&c, 0) == 0, "no pulse");
    CHECK(kcab_coins_pulse(&c, 1) == 1 && kcab_coins_pulse(&c, 2) == 1 && kcab_coins_pulse(&c, 3) == 2 && c.credits == 4,
          "a credit for each of the two low bits");
    CHECK(kcab_coins_pulse(&c, 0xFC) == 0, "other bits are not slots");

    kcab_coins_init(&c, 3);
    CHECK(kcab_coins_pulse(&c, 3) == 2 && kcab_coins_pulse(&c, 3) == 1 && c.credits == 3, "pulses obey the cap");
}

static void test_switches(void)
{
    KcabSwitches s = { 0 };
    uint32_t released = 99;

    CHECK(kcab_switch_edges(0, KCAB_SW_TEST, &released) == KCAB_SW_TEST && released == 0, "pressed");
    CHECK(kcab_switch_edges(KCAB_SW_TEST, KCAB_SW_TEST, &released) == 0 && released == 0, "held is not an edge");
    CHECK(kcab_switch_edges(KCAB_SW_TEST, 0, &released) == 0 && released == KCAB_SW_TEST, "released");
    CHECK(kcab_switch_edges(KCAB_SW_TEST, KCAB_SW_SERVICE, &released) == KCAB_SW_SERVICE && released == KCAB_SW_TEST,
          "one up as another goes down");
    CHECK(kcab_switch_edges(0x80000000u, 0x00000004u, NULL) == 0x00000004u, "no released pointer, high bits");

    CHECK(kcab_switches_update(&s, KCAB_SW_TEST, &released) == KCAB_SW_TEST, "a new tracker sees a switch held at power-on as a press");
    CHECK(kcab_switches_update(&s, KCAB_SW_TEST, &released) == 0, "once");
    CHECK(kcab_switches_update(&s, KCAB_SW_TEST | KCAB_SW_SERVICE, &released) == KCAB_SW_SERVICE && released == 0, "service joins");
    CHECK(kcab_switches_update(&s, 0, &released) == 0 && released == (KCAB_SW_TEST | KCAB_SW_SERVICE), "both let go");

    kcab_switches_prime(&s, KCAB_SW_TEST);
    CHECK(kcab_switches_update(&s, KCAB_SW_TEST, NULL) == 0, "primed: a switch held at power-on is not a press");
    CHECK(kcab_switches_update(&s, 0, &released) == 0 && released == KCAB_SW_TEST, "but its release is seen");
}

int main(void)
{
    test_meters();
    test_cap_and_pulse();
    test_switches();
    printf("test_cabinet: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
