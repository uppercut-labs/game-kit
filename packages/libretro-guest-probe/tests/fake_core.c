/* A fake libretro core for the host test: no emulation, just enough libretro to be driven. Its "guest memory" is a
 * 4 KiB block it keeps up to date:
 *
 *   0x100  u32  the guest tick; stays 0 for the first FAKE_BOOT_RUNS retro_run calls, then counts up
 *   0x104  u32  state: 1 at start, 2 from tick 10, 3 once the start button (joypad id 3) has been seen down
 *   0x108  u32  the joypad mask port 0 read this frame (RETRO_DEVICE_ID_JOYPAD_MASK)
 *   0x10c  u32  port 0's left stick X as read, plus 0x8000 (so an unsigned number)
 *   0x110  u32  port 0's left stick Y as read, plus 0x8000
 *   0x120  u32  1 when the core option "fake_serial_mode" was answered, with its first byte in 0x124
 *   0x130  u32  1 when SET_HW_RENDER was refused (the core asked for it because LGP_FAKE_HW is set)
 *   0x140+n u8  1 + the device number set on port n
 *   0x150  u32  the joypad mask port 1 read this frame
 *   0x154  u32  port 0's analog R2 trigger as read (RETRO_DEVICE_INDEX_ANALOG_BUTTON), plus 0x8000
 *   0x200  a record of 32-bit words for the closed-loop example: [0] 7 once the tick runs, [1] the screen (0 a menu,
 *          1 in play, reached by pressing Start and then B), [2] the player's x as a float (right adds 0.25 and left
 *          takes 0.25 each frame in play, within -4 to 4), [3] frames in play
 *
 * Every retro_run also sends video and audio: run r (from 1) sends no frame when r % 4 == 0, a dupe (NULL) when
 * r % 4 == 3, and otherwise an 8x4 XRGB8888 frame with a 40-byte pitch whose pixel (x, y) is r & 0xff in red, y * 16
 * in green and x * 8 in blue; then a batch of 8 stereo frames (left r, right -r), and one more single frame (1, 2) when
 * r is even. The AV info says 60 fps, 32000 Hz, 8x4. With LGP_FAKE_PACE set, the tick stands still on runs 20 and 21
 * and jumps by 3 on run 30.
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
static retro_video_refresh_t video_cb;
static retro_audio_sample_t sample_cb;
static retro_audio_sample_batch_t batch_cb;
static uint8_t ram[0x1000];
static unsigned runs, tick, pace, menu_step;
static float player_x;

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
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { sample_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { state_cb = cb; }

void retro_init(void)
{
    memset(ram, 0, sizeof(ram));
    runs = 0;
    tick = 0;
    menu_step = 0;
    player_x = 0;
    pace = getenv("LGP_FAKE_PACE") && *getenv("LGP_FAKE_PACE");
    put32(0x104, 1);
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info, 0, sizeof(*info));
    info->timing.fps = 60.0;
    info->timing.sample_rate = 32000.0;
    info->geometry.base_width = info->geometry.max_width = 8;
    info->geometry.base_height = info->geometry.max_height = 4;
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

static void send_av(void)
{
    static uint8_t frame[4 * 40];
    int16_t audio[16];
    unsigned x, y;
    if (runs % 4u == 3u) {
        video_cb(NULL, 8, 4, 40);
    } else if (runs % 4u != 0u) {
        for (y = 0; y < 4; ++y) {
            for (x = 0; x < 8; ++x) {
                uint32_t p = ((runs & 0xffu) << 16) | ((y * 16u) << 8) | (x * 8u);
                memcpy(frame + y * 40u + x * 4u, &p, sizeof(p));
            }
        }
        video_cb(frame, 8, 4, 40);
    }
    for (x = 0; x < 8; ++x) {
        audio[2 * x] = (int16_t)runs;
        audio[2 * x + 1] = (int16_t)-(int)runs;
    }
    batch_cb(audio, 8);
    if (runs % 2u == 0u) {
        sample_cb(1, 2);
    }
}

/* the closed-loop example's record: Start then B leaves the menu; left and right move x in play */
static void play(void)
{
    uint32_t word;
    if (!tick) {
        return;
    }
    put32(0x200, 7);
    if (menu_step == 0 && state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START)) {
        menu_step = 1;
    } else if (menu_step == 1 && state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B)) {
        menu_step = 2;
    }
    put32(0x204, menu_step == 2 ? 1u : 0u);
    if (menu_step == 2) {
        if (state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT)) {
            player_x += 0.25f;
        }
        if (state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT)) {
            player_x -= 0.25f;
        }
        player_x = player_x > 4.f ? 4.f : player_x < -4.f ? -4.f : player_x;
        put32(0x20c, get32(0x20c) + 1u);
    }
    memcpy(&word, &player_x, sizeof(word));
    put32(0x208, word);
}

void retro_run(void)
{
    uint32_t mask;
    runs++;
    if (runs > FAKE_BOOT_RUNS && !(pace && (runs == 20 || runs == 21))) {
        tick += pace && runs == 30 ? 3u : 1u;
    }
    put32(0x100, tick);
    poll_cb();
    mask = (uint32_t)(uint16_t)state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
    put32(0x108, mask);
    put32(0x10c, (uint32_t)((int)state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) + 0x8000));
    put32(0x110, (uint32_t)((int)state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) + 0x8000));
    put32(0x150, (uint32_t)(uint16_t)state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK));
    put32(0x154, (uint32_t)((int)state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON,
                                          RETRO_DEVICE_ID_JOYPAD_R2) + 0x8000));
    if (get32(0x104) < 3 && (state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START) == 1)) {
        put32(0x104, 3);
    } else if (tick >= 10 && get32(0x104) == 1) {
        put32(0x104, 2);
    }
    play();
    send_av();
}

void *retro_get_memory_data(unsigned id) { return id == RETRO_MEMORY_SYSTEM_RAM ? ram : NULL; }
size_t retro_get_memory_size(unsigned id) { return id == RETRO_MEMORY_SYSTEM_RAM ? sizeof(ram) : 0; }
