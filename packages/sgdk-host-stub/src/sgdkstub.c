/* sgdk-host-stub: the recorder behind genesis.h. See include/sgdkstub/sgdkstub.h. */
#include "sgdkstub/sgdkstub.h"

#include <stdarg.h>
#include <string.h>

/* Already in the power-on state, so a test that never calls sgs_reset() works. */
SgsState sgs = {.screen_width = 320, .plane_w = 64, .plane_h = 32};
SgsOptions sgs_opt;

static SgsFailHandler fail_handler; /* NULL: record and go on */
static bool abort_on_violation = true;
static char last_violation[160];
static u16 random_state = 1;

static void violation(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(last_violation, sizeof last_violation, format, args);
    va_end(args);
    ++sgs.violations;
    if (fail_handler) {
        fail_handler(last_violation);
    } else if (abort_on_violation) {
        fprintf(stderr, "sgdkstub: %s\n", last_violation);
        abort();
    }
}

#define REQUIRE(condition, ...)    \
    do {                           \
        if (!(condition)) {        \
            violation(__VA_ARGS__); \
        }                          \
    } while (0)

void sgs_set_fail_handler(SgsFailHandler handler)
{
    fail_handler = handler;
    abort_on_violation = true;
}

void sgs_record_violations_only(void)
{
    fail_handler = NULL;
    abort_on_violation = false;
}

const char *sgs_last_violation(void) { return last_violation; }

void sgs_reset(void)
{
    memset(&sgs, 0, sizeof sgs);
    sgs.screen_width = 320;
    sgs.plane_w = 64;
    sgs.plane_h = 32;
    random_state = 1;
    last_violation[0] = 0;
}

/* ---- planes ---- */

static u16 limit_w(void)
{
    u16 w = sgs.plane_w;
    if (sgs_opt.write_limit_w && sgs_opt.write_limit_w < w) w = sgs_opt.write_limit_w;
    return w;
}

static u16 limit_h(void)
{
    u16 h = sgs.plane_h;
    if (sgs_opt.write_limit_h && sgs_opt.write_limit_h < h) h = sgs_opt.write_limit_h;
    return h;
}

static bool plane_ok(VDPPlane plane, const char *call)
{
    REQUIRE((int)plane >= 0 && (int)plane < SGS_PLANE_COUNT, "%s: plane %d does not exist", call, (int)plane);
    return (int)plane >= 0 && (int)plane < SGS_PLANE_COUNT;
}

bool VDP_loadTileSet(const TileSet *tileset, u16 index, TransferMethod transfer)
{
    u16 i;
    (void)transfer;
    REQUIRE(tileset != NULL, "VDP_loadTileSet: no tile set");
    if (!tileset) return FALSE;
    REQUIRE((u32)index + tileset->numTile <= SGS_MAX_TILES, "VDP_loadTileSet: tiles %u..%u go past the %d tile slots",
            (unsigned)index, (unsigned)index + tileset->numTile, SGS_MAX_TILES);
    if ((u32)index + tileset->numTile > SGS_MAX_TILES) return FALSE;
    for (i = 0; i < tileset->numTile; ++i) {
        if (sgs_opt.forbid_tile_overlap) {
            REQUIRE(!sgs.tile_loaded[index + i], "VDP_loadTileSet: tile %u is already loaded", (unsigned)(index + i));
        }
        sgs.tile_loaded[index + i] = 1;
        memcpy(sgs.tile_pixels[index + i], &tileset->tiles[(size_t)i * 8], 8 * sizeof(u32));
    }
    return TRUE;
}

static void note_write(u16 x, u16 y)
{
    ++sgs.tile_writes;
    if (x > sgs.max_tile_x) sgs.max_tile_x = x;
    if (y > sgs.max_tile_y) sgs.max_tile_y = y;
}

void VDP_setTileMapXY(VDPPlane plane, u16 attr, u16 x, u16 y)
{
    if (!plane_ok(plane, "VDP_setTileMapXY")) return;
    REQUIRE(x < limit_w() && y < limit_h(), "VDP_setTileMapXY: cell %u,%u is outside the %ux%u write area", (unsigned)x,
            (unsigned)y, (unsigned)limit_w(), (unsigned)limit_h());
    if (x >= limit_w() || y >= limit_h()) return;
    if (sgs_opt.require_loaded_tiles && (attr & TILE_INDEX_MASK)) {
        REQUIRE(sgs.tile_loaded[attr & TILE_INDEX_MASK], "VDP_setTileMapXY: tile %u was never loaded",
                (unsigned)(attr & TILE_INDEX_MASK));
    }
    sgs.tiles[plane][y][x] = attr;
    sgs.text[plane][y][x] = 0;
    note_write(x, y);
}

void VDP_fillTileMapRect(VDPPlane plane, u16 attr, u16 x, u16 y, u16 w, u16 h)
{
    u16 row, column;
    if (!plane_ok(plane, "VDP_fillTileMapRect")) return;
    REQUIRE((u32)x + w <= limit_w() && (u32)y + h <= limit_h(),
            "VDP_fillTileMapRect: %ux%u at %u,%u goes outside the %ux%u write area", (unsigned)w, (unsigned)h,
            (unsigned)x, (unsigned)y, (unsigned)limit_w(), (unsigned)limit_h());
    if ((u32)x + w > limit_w() || (u32)y + h > limit_h()) return;
    for (row = y; row < y + h; ++row)
        for (column = x; column < x + w; ++column) VDP_setTileMapXY(plane, attr, column, row);
}

void VDP_clearPlane(VDPPlane plane, bool wait)
{
    (void)wait;
    if (!plane_ok(plane, "VDP_clearPlane")) return;
    memset(sgs.tiles[plane], 0, sizeof sgs.tiles[plane]);
    memset(sgs.text[plane], 0, sizeof sgs.text[plane]);
    ++sgs.clears;
}

void VDP_setPlaneSize(u16 w, u16 h, bool setupVram)
{
    (void)setupVram;
    REQUIRE((w == 32 || w == 64) && (h == 32 || h == 64), "VDP_setPlaneSize: %ux%u (the stub holds 32 or 64 cells each way)",
            (unsigned)w, (unsigned)h);
    if ((w == 32 || w == 64) && (h == 32 || h == 64)) {
        sgs.plane_w = w;
        sgs.plane_h = h;
    }
}

void VDP_setHorizontalScroll(VDPPlane plane, s16 value)
{
    if (plane_ok(plane, "VDP_setHorizontalScroll")) sgs.hscroll[plane] = value;
}

void VDP_setVerticalScroll(VDPPlane plane, s16 value)
{
    if (plane_ok(plane, "VDP_setVerticalScroll")) sgs.vscroll[plane] = value;
}

void VDP_drawText(const char *text, u16 x, u16 y)
{
    size_t i, length;
    REQUIRE(text != NULL, "VDP_drawText: no text");
    if (!text) return;
    length = strlen(text);
    REQUIRE(y < limit_h() && x + length <= limit_w(), "VDP_drawText: \"%.40s\" at %u,%u goes outside the %ux%u write area",
            text, (unsigned)x, (unsigned)y, (unsigned)limit_w(), (unsigned)limit_h());
    if (y >= limit_h() || x + length > limit_w()) return;
    for (i = 0; i < length; ++i) {
        sgs.text[sgs.text_plane][y][x + i] = text[i];
        sgs.tiles[sgs.text_plane][y][x + i] = 0;
    }
}

void VDP_setTextPlane(VDPPlane plane)
{
    if (plane_ok(plane, "VDP_setTextPlane")) sgs.text_plane = plane;
}

void VDP_setTextPalette(u16 palette)
{
    REQUIRE(palette < 4, "VDP_setTextPalette: palette %u (0 to 3)", (unsigned)palette);
    if (palette < 4) sgs.text_palette = palette;
}

void VDP_setTextPriority(u16 priority) { sgs.text_priority = priority != 0; }

void VDP_setBackgroundColor(u16 index)
{
    REQUIRE(index < 64, "VDP_setBackgroundColor: colour %u (0 to 63)", (unsigned)index);
    if (index < 64) sgs.background_index = index;
}

void VDP_setScreenWidth320(void) { sgs.screen_width = 320; }
void VDP_setScreenWidth256(void) { sgs.screen_width = 256; }

void PAL_setColor(u16 index, u16 value)
{
    REQUIRE(index < 64, "PAL_setColor: colour %u (0 to 63)", (unsigned)index);
    REQUIRE((value & (u16)~0x0eeeu) == 0, "PAL_setColor: 0x%04x has bits outside the 0x0eee colour bits", (unsigned)value);
    if (index >= 64 || (value & (u16)~0x0eeeu)) return;
    sgs.palette[index] = value;
    ++sgs.palette_writes;
}

void PAL_setPalette(u16 palette, const u16 *data, TransferMethod transfer)
{
    u16 i;
    (void)transfer;
    REQUIRE(palette < 4 && data != NULL, "PAL_setPalette: palette %u (0 to 3) or no data", (unsigned)palette);
    if (palette >= 4 || !data) return;
    for (i = 0; i < 16; ++i) PAL_setColor((u16)(palette * 16 + i), data[i]);
}

/* ---- sprites ---- */

static void check_sprite_position(const char *call, s16 x, s16 y)
{
    if (sgs_opt.sprite_bound_w && sgs_opt.sprite_bound_h) {
        REQUIRE(x >= 0 && x < sgs_opt.sprite_bound_w && y >= 0 && y < sgs_opt.sprite_bound_h,
                "%s: position %d,%d is outside 0..%d, 0..%d", call, (int)x, (int)y, (int)sgs_opt.sprite_bound_w - 1,
                (int)sgs_opt.sprite_bound_h - 1);
    }
}

void SPR_init(void) { sgs.sprite_count = 0; }

Sprite *SPR_addSprite(const SpriteDefinition *definition, s16 x, s16 y, u16 attr)
{
    Sprite *result;
    REQUIRE(definition != NULL, "SPR_addSprite: no sprite definition");
    REQUIRE(sgs.sprite_count < SGS_MAX_SPRITES, "SPR_addSprite: more than %d sprites", SGS_MAX_SPRITES);
    if (!definition || sgs.sprite_count >= SGS_MAX_SPRITES) return NULL;
    check_sprite_position("SPR_addSprite", x, y);
    result = &sgs.sprites[sgs.sprite_count++];
    memset(result, 0, sizeof *result);
    result->definition = definition;
    result->x = x;
    result->y = y;
    result->attr = attr;
    return result;
}

void SPR_setPosition(Sprite *sprite, s16 x, s16 y)
{
    REQUIRE(sprite != NULL, "SPR_setPosition: no sprite");
    if (!sprite) return;
    check_sprite_position("SPR_setPosition", x, y);
    sprite->x = x;
    sprite->y = y;
}

void SPR_setAnim(Sprite *sprite, s16 anim)
{
    REQUIRE(sprite != NULL && anim >= 0, "SPR_setAnim: no sprite or animation %d", (int)anim);
    if (!sprite || anim < 0) return;
    if (sprite->definition && sprite->definition->numAnimation) {
        REQUIRE(anim < sprite->definition->numAnimation, "SPR_setAnim: animation %d of %u", (int)anim,
                (unsigned)sprite->definition->numAnimation);
        if (anim >= sprite->definition->numAnimation) return;
    }
    sprite->anim = (u16)anim;
}

void SPR_setFrame(Sprite *sprite, s16 frame)
{
    REQUIRE(sprite != NULL && frame >= 0, "SPR_setFrame: no sprite or frame %d", (int)frame);
    if (!sprite || frame < 0) return;
    if (sprite->definition && sprite->definition->numFrame) {
        REQUIRE(frame < sprite->definition->numFrame, "SPR_setFrame: frame %d of %u", (int)frame,
                (unsigned)sprite->definition->numFrame);
        if (frame >= sprite->definition->numFrame) return;
    }
    sprite->frame = (u16)frame;
}

void SPR_setVisibility(Sprite *sprite, SpriteVisibility visibility)
{
    REQUIRE(sprite != NULL, "SPR_setVisibility: no sprite");
    if (sprite) sprite->visibility = (u16)visibility;
}

void SPR_update(void) { ++sgs.sprite_updates; }

/* ---- input ---- */

void JOY_init(void) {}

u16 JOY_readJoypad(u16 port)
{
    REQUIRE(port < 2, "JOY_readJoypad: port %u (the stub has JOY_1 and JOY_2)", (unsigned)port);
    if (port >= 2) return 0;
    if (sgs.joy_script_index[port] < sgs.joy_script_length[port]) return sgs.joy_script[port][sgs.joy_script_index[port]++];
    return sgs.joy_state[port];
}

void sgs_joy_set(u16 port, u16 state)
{
    if (port < 2) sgs.joy_state[port] = state;
}

void sgs_joy_script(u16 port, const u16 *states, u16 count)
{
    u16 i;
    if (port >= 2) return;
    if (count > SGS_MAX_JOY_SCRIPT) {
        violation("sgs_joy_script: %u states (the script holds %d)", (unsigned)count, SGS_MAX_JOY_SCRIPT);
        count = SGS_MAX_JOY_SCRIPT;
    }
    for (i = 0; i < count; ++i) sgs.joy_script[port][i] = states[i];
    sgs.joy_script_length[port] = count;
    sgs.joy_script_index[port] = 0;
}

/* ---- sound ---- */

void Z80_loadDriver(u16 driver, bool wait)
{
    (void)wait;
    sgs.z80_driver = driver;
}

static SgsAudioCall *next_audio_call(const char *call)
{
    REQUIRE(sgs.audio_count < SGS_MAX_AUDIO_CALLS, "%s: more than %d sound calls since the last sgs_clear_audio_calls()", call,
            SGS_MAX_AUDIO_CALLS);
    if (sgs.audio_count >= SGS_MAX_AUDIO_CALLS) return NULL;
    return &sgs.audio[sgs.audio_count++];
}

bool XGM2_playPCMEx(const u8 *sample, u32 length, SoundPCMChannel channel, u8 priority, bool halfRate, bool loop)
{
    SgsAudioCall *call;
    REQUIRE(sample != NULL && length != 0, "XGM2_playPCMEx: no sample or no length");
    REQUIRE((int)channel >= -1 && (int)channel < SGS_XGM2_CHANNELS, "XGM2_playPCMEx: channel %d (-1 or 0 to %d)", (int)channel,
            SGS_XGM2_CHANNELS - 1);
    REQUIRE(priority < 16, "XGM2_playPCMEx: priority %u (0 to 15)", (unsigned)priority);
    if (!sample || !length || (int)channel < -1 || (int)channel >= SGS_XGM2_CHANNELS || priority >= 16) return FALSE;
    call = next_audio_call("XGM2_playPCMEx");
    if (!call) return FALSE;
    memset(call, 0, sizeof *call);
    call->sample = sample;
    call->length = length;
    call->channel = (s8)channel;
    call->priority = priority;
    call->half_rate = halfRate;
    call->loop = loop;
    if (channel >= 0) {
        sgs.channel_sample[channel] = sample;
        sgs.channel_loop[channel] = loop;
    }
    return TRUE;
}

void XGM2_stopPCM(SoundPCMChannel channel)
{
    SgsAudioCall *call;
    REQUIRE((int)channel >= 0 && (int)channel < SGS_XGM2_CHANNELS, "XGM2_stopPCM: channel %d (0 to %d)", (int)channel,
            SGS_XGM2_CHANNELS - 1);
    if ((int)channel < 0 || (int)channel >= SGS_XGM2_CHANNELS) return;
    call = next_audio_call("XGM2_stopPCM");
    if (!call) return;
    memset(call, 0, sizeof *call);
    call->stopped = TRUE;
    call->channel = (s8)channel;
    sgs.channel_sample[channel] = NULL;
    sgs.channel_loop[channel] = FALSE;
}

void sgs_clear_audio_calls(void)
{
    memset(sgs.audio, 0, sizeof sgs.audio);
    sgs.audio_count = 0;
}

void PSG_write(u8 value)
{
    REQUIRE(sgs.psg_log_length < SGS_PSG_LOG_SIZE, "PSG_write: more than %d bytes written since sgs_reset()", SGS_PSG_LOG_SIZE);
    if (sgs.psg_log_length < SGS_PSG_LOG_SIZE) sgs.psg_log[sgs.psg_log_length++] = value;
}

void PSG_reset(void)
{
    u8 channel;
    for (channel = 0; channel < 4; ++channel) {
        PSG_write((u8)(0x80 | (channel << 5)));
        PSG_write(0);
        PSG_write((u8)(0x90 | (channel << 5) | 0x0F));
    }
}

void PSG_setEnvelope(u8 channel, u8 value)
{
    REQUIRE(channel < 4 && value < 16, "PSG_setEnvelope: channel %u (0 to 3), value %u (0 to 15)", (unsigned)channel, (unsigned)value);
    PSG_write((u8)(0x90 | ((channel & 3) << 5) | (value & 0x0F)));
}

void PSG_setTone(u8 channel, u16 value)
{
    REQUIRE(channel < 3 && value < 1024, "PSG_setTone: channel %u (0 to 2), period %u (0 to 1023)", (unsigned)channel, (unsigned)value);
    PSG_write((u8)(0x80 | ((channel & 3) << 5) | (value & 0x0F)));
    PSG_write((u8)((value >> 4) & 0x3F));
}

void PSG_setNoise(u8 type, u8 frequency)
{
    REQUIRE(type < 2 && frequency < 4, "PSG_setNoise: type %u (0 or 1), frequency %u (0 to 3)", (unsigned)type, (unsigned)frequency);
    PSG_write((u8)(0xE0 | ((type & 1) << 2) | (frequency & 3)));
}

u16 sgs_psg_events(SgsPsgEvent *out, u16 max)
{
    u16 i, count = 0;
    u16 period[3] = {0, 0, 0};
    int latched_channel = -1; /* the tone channel a following data byte belongs to */
    for (i = 0; i < sgs.psg_log_length; ++i) {
        const u8 b = sgs.psg_log[i];
        SgsPsgEvent event;
        if (b & 0x80) {
            const u8 channel = (u8)((b >> 5) & 3);
            const bool volume = (b & 0x10) != 0;
            event.channel = channel;
            event.volume = volume;
            if (volume) {
                event.value = b & 0x0F;
                latched_channel = -1;
            } else if (channel < 3) {
                period[channel] = (u16)((period[channel] & 0x3F0) | (b & 0x0F));
                event.value = period[channel];
                latched_channel = channel;
            } else {
                event.value = b & 0x07;
                latched_channel = -1;
            }
        } else if (latched_channel >= 0) {
            period[latched_channel] = (u16)((period[latched_channel] & 0x00F) | ((b & 0x3F) << 4));
            event.channel = (u8)latched_channel;
            event.volume = false;
            event.value = period[latched_channel];
        } else {
            continue; /* a data byte for a register that is not a tone */
        }
        if (out && count < max) out[count] = event;
        ++count;
    }
    return count;
}

bool sgs_psg_has_tone(u8 channel)
{
    u16 i, n;
    SgsPsgEvent events[SGS_PSG_LOG_SIZE];
    n = sgs_psg_events(events, SGS_PSG_LOG_SIZE);
    for (i = 0; i < n; ++i)
        if (!events[i].volume && events[i].channel == channel) return true;
    return false;
}

bool sgs_psg_has_envelope(u8 channel, u8 value)
{
    u16 i, n;
    SgsPsgEvent events[SGS_PSG_LOG_SIZE];
    n = sgs_psg_events(events, SGS_PSG_LOG_SIZE);
    for (i = 0; i < n; ++i)
        if (events[i].volume && events[i].channel == channel && events[i].value == value) return true;
    return false;
}

bool sgs_psg_tone_changed(u8 channel)
{
    u16 i, n, first = 0;
    bool have_first = false;
    SgsPsgEvent events[SGS_PSG_LOG_SIZE];
    n = sgs_psg_events(events, SGS_PSG_LOG_SIZE);
    for (i = 0; i < n; ++i) {
        /* a period counts once its second byte has arrived: that is the event whose previous one was the same channel's latch */
        if (events[i].volume || events[i].channel != channel || i == 0) continue;
        if (events[i - 1].volume || events[i - 1].channel != channel) continue;
        if (!have_first) {
            first = events[i].value;
            have_first = true;
        } else if (events[i].value != first) {
            return true;
        }
    }
    return false;
}

/* ---- save RAM ---- */

void SRAM_enable(void) { sgs.sram_mode = 2; }
void SRAM_enableRO(void) { sgs.sram_mode = 1; }
void SRAM_disable(void) { sgs.sram_mode = 0; }

u8 SRAM_readByte(u32 offset)
{
    REQUIRE(sgs.sram_mode != 0, "SRAM_readByte: save RAM is not enabled");
    REQUIRE(offset < SGS_SRAM_SIZE, "SRAM_readByte: offset %lu is past the %d bytes the stub holds", (unsigned long)offset, SGS_SRAM_SIZE);
    return offset < SGS_SRAM_SIZE ? sgs.sram[offset] : 0;
}

void SRAM_writeByte(u32 offset, u8 value)
{
    REQUIRE(sgs.sram_mode == 2, "SRAM_writeByte: save RAM is not enabled for writing");
    REQUIRE(offset < SGS_SRAM_SIZE, "SRAM_writeByte: offset %lu is past the %d bytes the stub holds", (unsigned long)offset, SGS_SRAM_SIZE);
    if (sgs.sram_mode == 2 && offset < SGS_SRAM_SIZE) sgs.sram[offset] = value;
}

/* ---- system ---- */

bool SYS_doVBlankProcess(void)
{
    ++sgs.vblanks;
    return TRUE;
}

void KDebug_Alert(const char *text)
{
    if (sgs.alert_count < SGS_MAX_ALERTS) {
        snprintf(sgs.alerts[sgs.alert_count], SGS_ALERT_LEN, "%s", text ? text : "");
        ++sgs.alert_count;
    }
    if (sgs_opt.alert_stream) fprintf(sgs_opt.alert_stream, "ROM: %s\n", text ? text : "");
}

u16 sgs_random(void)
{
    /* a 16-bit xorshift: fixed, so a test sees the same numbers on every system */
    u16 x = random_state ? random_state : 1;
    x ^= (u16)(x << 7);
    x ^= (u16)(x >> 9);
    x ^= (u16)(x << 8);
    random_state = x;
    return x;
}

void setRandomSeed(u16 seed) { random_state = seed ? seed : 1; }

/* ---- reading back ---- */

u16 sgs_tile_at(VDPPlane plane, u16 x, u16 y)
{
    if ((int)plane < 0 || (int)plane >= SGS_PLANE_COUNT || x >= SGS_MAX_PLANE_W || y >= SGS_MAX_PLANE_H) return 0;
    return sgs.tiles[plane][y][x];
}

void sgs_row_text(VDPPlane plane, u16 y, char *out)
{
    u16 x;
    for (x = 0; x < sgs.plane_w; ++x) out[x] = sgs.text[plane][y][x] ? sgs.text[plane][y][x] : ' ';
    out[sgs.plane_w] = 0;
}

bool sgs_row_contains(VDPPlane plane, u16 y, const char *text)
{
    char row[SGS_MAX_PLANE_W + 1];
    sgs_row_text(plane, y, row);
    return strstr(row, text) != NULL;
}

bool sgs_tile_has_ink(u16 attr)
{
    u16 row;
    const u16 index = attr & TILE_INDEX_MASK;
    for (row = 0; row < 8; ++row)
        if (sgs.tile_pixels[index][row]) return true;
    return false;
}
