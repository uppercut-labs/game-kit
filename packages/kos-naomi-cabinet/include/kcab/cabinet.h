/* The cabinet's coin counters and its test and service switches, as pure functions: no JVS, no KOS. Part of
 * kos-naomi-cabinet 0.1.0 (MIT). */
#ifndef KCAB_CABINET_H
#define KCAB_CABINET_H

#include <stdint.h>

/* The two switches every cabinet has, as bits of a switch word. Bits 2..31 are the game's (push switches, as it maps them). */
#define KCAB_SW_TEST 0x00000001u
#define KCAB_SW_SERVICE 0x00000002u

/*
 * Credits from coin meters. The JVS board counts coins per slot in a running meter; credits are the growth of the
 * meter. The first reading only sets the baseline (coins in the meter at power-on are not credits), and a meter that
 * goes down (the driver subtracts from it, or the board was reset) re-bases without a credit.
 */
typedef struct KcabCoins {
    uint16_t last[2];
    uint16_t credits;
    uint16_t cap; /* credits stop here */
    uint8_t known; /* a baseline has been read */
} KcabCoins;

/* `cap` 0 means no cap short of 65535. */
void kcab_coins_init(KcabCoins *coins, uint16_t cap);

/* Feeds the two slots' meters. Returns the credits added by this reading. */
uint16_t kcab_coins_update(KcabCoins *coins, const uint16_t meter[2]);

/* Adds one credit for each of the low two bits of `pulse` (the board's one-shot insert flags). Use this or
 * kcab_coins_update for a game, not both: one coin would count twice. Returns the credits added. */
uint16_t kcab_coins_pulse(KcabCoins *coins, uint8_t pulse);

/* Takes `count` credits if there are that many (returns 1), else takes none (returns 0). */
uint8_t kcab_coins_spend(KcabCoins *coins, uint16_t count);

/* Takes up to `most` credits and returns how many were taken. */
uint16_t kcab_coins_take(KcabCoins *coins, uint16_t most);

/*
 * Edges of a switch word between two frames: the bits that went down to up (`pressed`) and up to down (returned in
 * `*released` if it is not NULL).
 */
uint32_t kcab_switch_edges(uint32_t previous, uint32_t current, uint32_t *released);

/* The same with the previous frame kept. A new KcabSwitches reads as nothing held, so a switch held at power-on gives
 * one press; kcab_switches_prime starts from what is held now instead. */
typedef struct KcabSwitches {
    uint32_t previous;
} KcabSwitches;

void kcab_switches_prime(KcabSwitches *switches, uint32_t held);
uint32_t kcab_switches_update(KcabSwitches *switches, uint32_t held, uint32_t *released);

#endif
