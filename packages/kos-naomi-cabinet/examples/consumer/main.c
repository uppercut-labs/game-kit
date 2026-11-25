/* A made-up cabinet game's settings and coins, on a RAM chip instead of a cabinet: the same calls a game makes with the
 * KallistiOS glue (kcab_kos_eeprom, kcab_kos_read_inputs) go to a chip in memory here.
 *
 *   my-cabinet
 */
#include <stdio.h>

#include "kcab/cabinet.h"
#include "kcab/eeprom.h"
#include "kcab/ram.h"

/* the game's layout: the game chooses it. 8 bytes: magic "GAME1", a flags byte, two spare; stored twice. */
static const uint8_t MAGIC[5] = { 'G', 'A', 'M', 'E', '1' };
static const KcabBlock BLOCK = { MAGIC, 5, 8, 2 };
enum { FREE_PLAY = 1, WIDESCREEN = 2 };

int main(void)
{
    KcabRam chip;
    KcabEeprom eeprom;
    KcabCoins coins;
    KcabSwitches switches = { 0 };
    uint8_t block[8] = { 'G', 'A', 'M', 'E', '1', FREE_PLAY, 0, 0 }, loaded[8];
    uint16_t meters[2];
    uint32_t pressed;
    kcab_result r;

    kcab_ram_init(&chip, 0xFF); /* a chip nobody has written */
    eeprom = kcab_ram_eeprom(&chip);

    /* first boot: nothing stored, so the game starts from its defaults */
    r = kcab_block_load(&eeprom, &BLOCK, loaded);
    printf("first boot: %s\n", kcab_result_text(r));
    if (r != KCAB_EMPTY) {
        return 1;
    }

    /* an operator presses the test switch, then the game saves a changed setting */
    pressed = kcab_switches_update(&switches, KCAB_SW_TEST, NULL);
    if (pressed & KCAB_SW_TEST) {
        block[5] = (uint8_t)(block[5] ^ WIDESCREEN);
        r = kcab_block_save(&eeprom, &BLOCK, block);
        printf("settings saved: %s\n", kcab_result_text(r));
        if (r != KCAB_OK) {
            return 1;
        }
    }

    /* power cycle: the settings are there */
    r = kcab_block_load(&eeprom, &BLOCK, loaded);
    printf("second boot: %s, flags %u\n", kcab_result_text(r), loaded[5]);
    if (r != KCAB_OK || loaded[5] != (FREE_PLAY | WIDESCREEN)) {
        return 1;
    }

    /* coins: the meters already read 40 and 9 at power-on, then two coins go in */
    kcab_coins_init(&coins, 99);
    meters[0] = 40;
    meters[1] = 9;
    kcab_coins_update(&coins, meters);
    meters[0] = 41;
    meters[1] = 10;
    kcab_coins_update(&coins, meters);
    printf("credits %u\n", coins.credits);
    if (coins.credits != 2 || !kcab_coins_spend(&coins, 1) || coins.credits != 1) {
        return 1;
    }
    return 0;
}
