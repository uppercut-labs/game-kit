/* A fake libretro core for the host test: no emulation, no video, just enough libretro to be driven. Its "guest memory"
 * is a 4 KiB block it keeps up to date:
 *
 *   0x100  u32  the guest tick; stays 0 for the first FAKE_BOOT_RUNS retro_run calls, then counts up
 *   0x104  u32  state: 1 at start, 2 from tick 10, 3 once the start button (joypad id 3) has been seen down
 *   0x108  u32  the joypad mask port 0 read this frame (RETRO_DEVICE_ID_JOYPAD_MASK)
 *   0x10c  u32  port 0's left stick X as read, plus 0x8000 (so an unsigned number)
 *   0x110  u32  port 0's left stick Y as read, plus 0x8000
 *   0x120  u32  1 when the core option "fake_serial_mode" was answered, with its first byte in 0x124
 *   0x130  u32  1 when SET_HW_RENDER was refused (the core asked for it because LGP_FAKE_HW is set)
 *   0x140+n u8  1 + the device number set on port n
 *
 * A guest address of 0x8c000100 with an address mask of 0x00ffffff reaches the tick, as on a console that mirrors RAM.
 */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "libretro.h"

#define FAKE_BOOT_RUNS 5

static retro_environment_t env;
static retro_input_poll_t poll_cb;
static retro_input_state_t state_cb;
static uint8_t ram[0x1000];
static unsigned runs, tick;

static void put32(unsigned at, uint32_t v)
{
    ram[at] = (uint8_t)v;
    ram[at + 1] = (uint8_t)(v >> 8);
    ram[at + 2] = (uint8_t)(v >> 16);
    ram[at + 3] = (uint8_t)(v >> 24);
}

static uint32_t get32(unsigned at)
{
    return (uint32_t)ram[at] | ((uint32_t)ram[at + 1] << 8) | ((uint32_t)ram[at + 2] << 16) | ((uint32_t)ram[at + 3] << 24);
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }
void retro_set_environment(retro_environment_t cb) { env = cb; }
void retro_set_video_refresh(retro_video_refresh_t cb) { (void)cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { (void)cb; }
void retro_set_input_poll(retro_input_poll_t cb) { poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { state_cb = cb; }

void retro_init(void)
{
    memset(ram, 0, sizeof(ram));
    runs = 0;
    tick = 0;
    put32(0x104, 1);
}

void retro_deinit(void) {}

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name = "lgp-fake-core";
    info->library_version = "0";
    info->need_fullpath = true;
}

bool retro_load_game(const struct retro_game_info *game)
{
    const char *dir = NULL;
    struct retro_variable v;
    unsigned version = 0;
    int format = RETRO_PIXEL_FORMAT_XRGB8888;

    (void)game;
    if (!env(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) || !dir) {
        return false;
    }
    if (!env(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) || version != 2) {
        return false;
    }
    env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);
    v.key = "fake_serial_mode";
    v.value = NULL;
    if (env(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value) {
        put32(0x120, 1);
        ram[0x124] = (uint8_t)v.value[0];
    }
    v.key = "fake_unmatched_option";
    v.value = NULL;
    if (env(RETRO_ENVIRONMENT_GET_VARIABLE, &v)) {
        put32(0x128, 1); /* the host answered a key it should not have */
    }
    if (getenv("LGP_FAKE_HW")) {
        struct retro_hw_render_callback hw;
        memset(&hw, 0, sizeof(hw));
        hw.context_type = RETRO_HW_CONTEXT_OPENGL_CORE;
        hw.version_major = 3;
        hw.version_minor = 2;
        if (!env(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw)) {
            put32(0x130, 1);
        }
    }
    return true;
}

void retro_unload_game(void) {}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    if (port < 8) {
        ram[0x140 + port] = (uint8_t)(1 + device);
    }
}

void retro_run(void)
{
    uint32_t mask;
    runs++;
    if (runs > FAKE_BOOT_RUNS) {
        tick++;
    }
    put32(0x100, tick);
    poll_cb();
    mask = (uint32_t)(uint16_t)state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
    put32(0x108, mask);
    put32(0x10c, (uint32_t)((int)state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) + 0x8000));
    put32(0x110, (uint32_t)((int)state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) + 0x8000));
    if (get32(0x104) < 3 && (state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START) == 1)) {
        put32(0x104, 3);
    } else if (tick >= 10 && get32(0x104) == 1) {
        put32(0x104, 2);
    }
}

void *retro_get_memory_data(unsigned id) { return id == RETRO_MEMORY_SYSTEM_RAM ? ram : NULL; }
size_t retro_get_memory_size(unsigned id) { return id == RETRO_MEMORY_SYSTEM_RAM ? sizeof(ram) : 0; }
