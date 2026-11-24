/* test_kos_glue: src/kos.c and src/kos_naomi_mode.c built against stand-in KallistiOS headers (tests/kos-stub) with fake
 * driver functions. It checks the glue's own work: when the cabinet counts as ready, how the board's state maps onto the
 * package's types, the EEPROM wrapper, and the system-mode override. It does not run KallistiOS or a cabinet. */
#include <stdio.h>
#include <string.h>

#include <dc/maple/mie.h>
#include <kos.h>

#include "kcab/kos.h"

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

/* ---- the fake driver ---- */
static mie_port0_mode_t port_mode = MIE_PORT0_JVS;
static int have_device = 1, have_status = 1;
static mie_state_t state;
static uint8_t chip[MIE_EEPROM_SIZE];
static int chip_read_ok = 1, chip_write_ok = 1;
static int real_mode = HW_TYPE_RETAIL, real_region = 7, region_seen;
static struct maple_device { int unused; } device;

mie_port0_mode_t mie_port0_mode(void)
{
    return port_mode;
}
maple_device_t *maple_enum_type(int n, uint32_t func)
{
    return n == 0 && func == MAPLE_FUNC_MIE && have_device ? &device : NULL;
}
void *maple_dev_status(maple_device_t *dev)
{
    return dev == &device && have_status ? &state : NULL;
}
bool mie_get_eeprom(void *dst)
{
    if (!chip_read_ok) {
        return false;
    }
    memcpy(dst, chip, sizeof(chip));
    return true;
}
bool mie_set_eeprom(uint8_t *eeprom)
{
    if (!chip_write_ok) {
        return false;
    }
    memcpy(chip, eeprom, sizeof(chip));
    return true;
}
int __real_hardware_sys_mode(int *region)
{
    if (region) {
        *region = real_region;
        region_seen = 1;
    }
    return real_mode;
}
int __wrap_hardware_sys_mode(int *region);

static void test_ready(void)
{
    CHECK(kcab_kos_ready(), "port A is JVS, a device, a status");
    port_mode = MIE_PORT0_MAPLE;
    CHECK(!kcab_kos_ready(), "port A is a plain Maple port");
    port_mode = MIE_PORT0_UNKNOWN;
    CHECK(!kcab_kos_ready(), "the probe has not finished");
    port_mode = MIE_PORT0_JVS;
    have_device = 0;
    CHECK(!kcab_kos_ready(), "no MIE device yet");
    have_device = 1;
    have_status = 0;
    CHECK(!kcab_kos_ready(), "no status yet");
    have_status = 1;
    CHECK(kcab_kos_ready(), "ready again");
}

static void test_inputs(void)
{
    KcabKosInputs in;

    memset(&in, 0xEE, sizeof(in));
    memset(&state, 0, sizeof(state));
    state.jvs.coin[0].count = 12;
    state.jvs.coin[0].raw = 0x4000 | 12; /* the raw word carries status bits; the glue reads the cleaned count */
    state.jvs.coin[1].count = 0x3FFF;
    state.jvs.coin_pulse = 2;
    CHECK(kcab_kos_read_inputs(&in) == 1, "read");
    CHECK(in.meter[0] == 12 && in.meter[1] == 0x3FFF && in.coin_pulse == 2 && in.switches == 0, "meters, pulse, no switches");

    state.jvs.system.test = 1;
    kcab_kos_read_inputs(&in);
    CHECK(in.switches == KCAB_SW_TEST, "the system test switch");
    state.jvs.system.test = 0;
    state.jvs.panel.psw.test = 1;
    kcab_kos_read_inputs(&in);
    CHECK(in.switches == KCAB_SW_TEST, "the panel test switch");
    state.jvs.panel.psw.test = 0;
    state.jvs.panel.psw.service = 1;
    kcab_kos_read_inputs(&in);
    CHECK(in.switches == KCAB_SW_SERVICE, "the panel service switch");
    state.jvs.panel.psw.service = 0;
    state.jvs.p1.service = 1;
    kcab_kos_read_inputs(&in);
    CHECK(in.switches == KCAB_SW_SERVICE, "player one's service switch");
    state.jvs.p1.service = 0;
    state.jvs.p2.service = 1;
    state.jvs.system.test = 1;
    kcab_kos_read_inputs(&in);
    CHECK(in.switches == (KCAB_SW_SERVICE | KCAB_SW_TEST), "player two's service with the test switch");
    state.jvs.p1.start = 1; /* a start button is not a cabinet switch */
    state.jvs.p2.service = 0;
    state.jvs.system.test = 0;
    kcab_kos_read_inputs(&in);
    CHECK(in.switches == 0, "start is the game's");

    memset(&in, 0xEE, sizeof(in));
    have_device = 0;
    CHECK(kcab_kos_read_inputs(&in) == 0 && in.meter[0] == 0xEEEE, "not ready: nothing is written");
    have_device = 1;
}

static void test_eeprom(void)
{
    KcabEeprom e = kcab_kos_eeprom();
    uint8_t image[KCAB_EEPROM_BYTES], back[KCAB_EEPROM_BYTES];
    static const uint8_t magic[4] = { 'K', 'O', 'S', '1' };
    KcabBlock b = { magic, 4, 6, 2 };
    uint8_t block[6] = { 'K', 'O', 'S', '1', 3, 4 }, out[6];
    int i;

    for (i = 0; i < 128; i++) {
        chip[i] = (uint8_t)(i * 5);
        image[i] = (uint8_t)(i * 9);
    }
    CHECK(e.read(e.context, back) == 1 && memcmp(back, chip, 128) == 0, "read passes the chip through");
    CHECK(e.write(e.context, image) == 1 && memcmp(chip, image, 128) == 0, "write stores the image");
    chip_read_ok = 0;
    CHECK(e.read(e.context, back) == 0, "a failed read is 0");
    chip_read_ok = 1;
    chip_write_ok = 0;
    CHECK(e.write(e.context, image) == 0, "a failed write is 0");
    chip_write_ok = 1;

    /* the package over this chip */
    memset(chip, 0xFF, sizeof(chip));
    CHECK(kcab_block_save(&e, &b, block) == KCAB_OK && kcab_block_load(&e, &b, out) == KCAB_OK && memcmp(out, block, 6) == 0,
          "save and load through the glue");
}

static void test_mode(void)
{
    int region = 0;

    real_mode = HW_TYPE_RETAIL;
    CHECK(__wrap_hardware_sys_mode(&region) == HW_TYPE_NAOMI && region == 7, "retail reads as NAOMI and the region is kept");
    real_mode = HW_TYPE_NAOMI;
    CHECK(__wrap_hardware_sys_mode(&region) == HW_TYPE_NAOMI, "NAOMI stays NAOMI");
    real_mode = HW_TYPE_OTHER;
    CHECK(__wrap_hardware_sys_mode(NULL) == HW_TYPE_OTHER, "another type is left alone, with no region pointer");
    real_mode = HW_TYPE_RETAIL;
    CHECK(kcab_kos_raw_hardware_mode() == HW_TYPE_RETAIL, "the raw mode is what KOS read");
    (void)region_seen;
}

int main(void)
{
    test_ready();
    test_inputs();
    test_eeprom();
    test_mode();
    printf("test_kos_glue: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
