/* A stand-in for <dc/maple/mie.h> (see ../../kos.h): the names and field paths the glue reads, as declared in
 * KallistiOS's public header, with just the fields the glue uses. */
#ifndef STUB_DC_MAPLE_MIE_H
#define STUB_DC_MAPLE_MIE_H

#include <stdbool.h>
#include <stdint.h>

#define MIE_EEPROM_SIZE 128
#define MIE_JVS_COIN_SLOTS 2

typedef enum mie_port0_mode { MIE_PORT0_UNKNOWN = 0, MIE_PORT0_JVS, MIE_PORT0_MAPLE } mie_port0_mode_t;

typedef struct mie_jvs_coin_slot {
    uint16_t raw;
    uint16_t count;
} mie_jvs_coin_slot_t;

typedef struct mie_jvs_player_sw {
    unsigned service : 1;
    unsigned start : 1;
} mie_jvs_player_sw_t;

typedef struct mie_jvs_system {
    unsigned test : 1;
} mie_jvs_system_t;

typedef struct mie_jvs_panel_psw {
    unsigned test : 1;
    unsigned service : 1;
} mie_jvs_panel_psw_t;

typedef struct mie_jvs_panel {
    mie_jvs_panel_psw_t psw;
} mie_jvs_panel_t;

typedef struct mie_jvs_inputs {
    mie_jvs_system_t system;
    mie_jvs_panel_t panel;
    mie_jvs_player_sw_t p1;
    mie_jvs_player_sw_t p2;
    mie_jvs_coin_slot_t coin[MIE_JVS_COIN_SLOTS];
    uint8_t coin_pulse;
} mie_jvs_inputs_t;

typedef struct mie_state {
    mie_jvs_inputs_t jvs;
} mie_state_t;

mie_port0_mode_t mie_port0_mode(void);
bool mie_get_eeprom(void *dst);
bool mie_set_eeprom(uint8_t *eeprom);

#endif
