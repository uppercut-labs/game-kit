#include "kcab/cabinet.h"

void kcab_coins_init(KcabCoins *coins, uint16_t cap)
{
    coins->last[0] = coins->last[1] = 0;
    coins->credits = 0;
    coins->cap = cap ? cap : 0xFFFFu;
    coins->known = 0;
}

static uint16_t add(KcabCoins *coins, uint32_t count)
{
    uint32_t room = (uint32_t)coins->cap - (coins->credits < coins->cap ? coins->credits : coins->cap);
    uint32_t added = count < room ? count : room;

    coins->credits = (uint16_t)(coins->credits + added);
    return (uint16_t)added;
}

uint16_t kcab_coins_update(KcabCoins *coins, const uint16_t meter[2])
{
    uint32_t gained = 0;
    int slot;

    for (slot = 0; slot < 2; slot++) {
        if (coins->known && meter[slot] >= coins->last[slot]) {
            gained += (uint32_t)(meter[slot] - coins->last[slot]);
        }
        coins->last[slot] = meter[slot];
    }
    coins->known = 1;
    return add(coins, gained);
}

uint16_t kcab_coins_pulse(KcabCoins *coins, uint8_t pulse)
{
    return add(coins, (uint32_t)((pulse & 1u) + ((pulse >> 1) & 1u)));
}

uint8_t kcab_coins_spend(KcabCoins *coins, uint16_t count)
{
    if (coins->credits < count) {
        return 0;
    }
    coins->credits = (uint16_t)(coins->credits - count);
    return 1;
}

uint16_t kcab_coins_take(KcabCoins *coins, uint16_t most)
{
    uint16_t taken = coins->credits < most ? coins->credits : most;

    coins->credits = (uint16_t)(coins->credits - taken);
    return taken;
}

uint32_t kcab_switch_edges(uint32_t previous, uint32_t current, uint32_t *released)
{
    if (released) {
        *released = previous & ~current;
    }
    return current & ~previous;
}

void kcab_switches_prime(KcabSwitches *switches, uint32_t held)
{
    switches->previous = held;
}

uint32_t kcab_switches_update(KcabSwitches *switches, uint32_t held, uint32_t *released)
{
    uint32_t pressed = kcab_switch_edges(switches->previous, held, released);

    switches->previous = held;
    return pressed;
}
