/* ps2_glue_test: src/ps2.c and src/ps2_bin2c.c built against stand-in SDK headers (tests/ps2sdk-stub) with fake SDK
 * functions. It checks the glue's own work: the order of the start, the failure codes and numbers, the format it asks
 * for, and that a started stream feeds audsrv. It does not run the real PS2SDK or a console. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <audsrv.h>
#include <loadfile.h>

#include "pas/ps2.h"

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

/* ---- the fake SDK ---- */
static int events[32], event_n; /* 1 LIBSD, 2 module buffer, 3 init, 4 format, 5 volume, 6 available, 7 play */
static char loaded_path[64];
static const void *loaded_buffer;
static unsigned loaded_size;
static int libsd_result, module_result, module_status, init_result, format_result, volume_result;
static struct audsrv_fmt_t asked_format;
static int asked_volume, free_bytes, played_bytes;

int SifLoadModule(const char *path, int arg_len, const char *args)
{
    (void)arg_len;
    (void)args;
    events[event_n++] = 1;
    strncpy(loaded_path, path, sizeof(loaded_path) - 1);
    return libsd_result;
}
int SifExecModuleBuffer(void *ptr, unsigned int size, unsigned int arg_len, const char *args, int *mod_res)
{
    (void)arg_len;
    (void)args;
    events[event_n++] = 2;
    loaded_buffer = ptr;
    loaded_size = size;
    *mod_res = module_status;
    return module_result;
}
int audsrv_init(void)
{
    events[event_n++] = 3;
    return init_result;
}
int audsrv_set_format(struct audsrv_fmt_t *fmt)
{
    events[event_n++] = 4;
    asked_format = *fmt;
    return format_result;
}
int audsrv_set_volume(int volume)
{
    events[event_n++] = 5;
    asked_volume = volume;
    return volume_result;
}
int audsrv_available(void)
{
    events[event_n++] = 6;
    return free_bytes;
}
int audsrv_play_audio(const char *chunk, int bytes)
{
    (void)chunk;
    events[event_n++] = 7;
    played_bytes += bytes;
    return bytes;
}

/* the program's bin2c array, as `bin2c` would name it */
unsigned char audsrv_irx[16];
unsigned int size_audsrv_irx = sizeof(audsrv_irx);

static PasStream stream;
static int rendered_frames;

static void render(void *user, int16_t *pcm, uint16_t frames)
{
    (void)user;
    memset(pcm, 0, (size_t)frames * PAS_FRAME_BYTES);
    rendered_frames += frames;
}

static void reset(void)
{
    event_n = 0;
    loaded_path[0] = '\0';
    loaded_buffer = NULL;
    loaded_size = 0;
    libsd_result = 1;
    module_result = 2;
    module_status = 0;
    init_result = format_result = volume_result = 0;
    memset(&asked_format, 0, sizeof(asked_format));
    asked_volume = -1;
    free_bytes = played_bytes = rendered_frames = 0;
    memset(&stream, 0, sizeof(stream));
}

static void test_start(void)
{
    PasPs2Config config;
    PasPs2Failure failure;
    static const int expected[] = { 1, 2, 3, 4, 5 };

    reset();
    config = pas_ps2_config_bin2c(22050, 90, render, NULL);
    CHECK(config.audsrv == audsrv_irx && config.size_audsrv == sizeof(audsrv_irx) && config.chunk_frames == 0,
          "bin2c config");
    CHECK(pas_ps2_start(&stream, &config, &failure) == 0 && pas_ready(&stream), "starts");
    CHECK(event_n == 5 && memcmp(events, expected, sizeof(expected)) == 0,
          "LIBSD, audsrv, init, format, volume, in that order (%d events)", event_n);
    CHECK(strcmp(loaded_path, "rom0:LIBSD") == 0, "LIBSD from the ROM: %s", loaded_path);
    CHECK(loaded_buffer == audsrv_irx && loaded_size == sizeof(audsrv_irx), "audsrv from the buffer");
    CHECK(asked_format.freq == 22050 && asked_format.bits == 16 && asked_format.channels == 2,
          "16-bit stereo at 22050: %d %d %d", asked_format.freq, asked_format.bits, asked_format.channels);
    CHECK(asked_volume == 90 && stream.volume == 90 && stream.chunk_frames == PAS_MAX_FRAMES, "volume and chunk");

    /* a started stream feeds audsrv through the ops */
    free_bytes = 4 + 100 * PAS_FRAME_BYTES;
    CHECK(pas_feed(&stream) == 100 * PAS_FRAME_BYTES && played_bytes == 400 && rendered_frames == 100, "feeds audsrv");
    pas_mute_for(&stream, 1);
    CHECK(asked_volume == 0, "mute through audsrv_set_volume");
    pas_feed(&stream);
    CHECK(asked_volume == 90, "and back");
}

static void test_failures(void)
{
    PasPs2Config config;
    PasPs2Failure failure;

    reset();
    config = pas_ps2_config_bin2c(48000, 100, render, NULL);
    libsd_result = -203;
    CHECK(pas_ps2_start(&stream, &config, &failure) == PAS_PS2_LIBSD_FAILED && failure.result == -203 && event_n == 1,
          "LIBSD fails: stops there with its number");
    CHECK(!pas_ready(&stream) && pas_feed(&stream) == 0, "a failed start feeds nothing");

    reset();
    module_result = -1;
    CHECK(pas_ps2_start(&stream, &config, &failure) == PAS_PS2_AUDSRV_FAILED && failure.result == -1 && event_n == 2,
          "audsrv fails to load");
    reset();
    module_status = 1;
    CHECK(pas_ps2_start(&stream, &config, &failure) == PAS_PS2_AUDSRV_FAILED && failure.status == 1,
          "audsrv loads but does not start: status %d", failure.status);
    reset();
    init_result = 5;
    CHECK(pas_ps2_start(&stream, &config, &failure) == PAS_PS2_RPC_FAILED && failure.result == 5 && event_n == 3,
          "audsrv_init fails");
    reset();
    format_result = 2;
    CHECK(pas_ps2_start(&stream, &config, &failure) == PAS_PS2_FORMAT_FAILED && asked_format.freq == 48000,
          "the format is refused");
    reset();
    volume_result = 3;
    CHECK(pas_ps2_start(&stream, &config, &failure) == PAS_PS2_VOLUME_FAILED && failure.result == 3 && !pas_ready(&stream),
          "the volume is refused");

    reset();
    CHECK(pas_ps2_start(&stream, &config, NULL) == 0, "the failure record is optional");
    reset();
    CHECK(pas_ps2_start(NULL, &config, &failure) == PAS_PS2_BAD_ARGUMENT && event_n == 0, "no stream");
    CHECK(pas_ps2_start(&stream, NULL, &failure) == PAS_PS2_BAD_ARGUMENT, "no config");
    config.render = NULL;
    CHECK(pas_ps2_start(&stream, &config, &failure) == PAS_PS2_BAD_ARGUMENT && event_n == 0, "no renderer");
    config = pas_ps2_config_bin2c(48000, 100, render, NULL);
    config.audsrv = NULL;
    CHECK(pas_ps2_start(&stream, &config, &failure) == PAS_PS2_BAD_ARGUMENT && event_n == 0, "no module");
}

static void test_steps(void)
{
    CHECK(strcmp(pas_ps2_step(PAS_PS2_LIBSD_FAILED), "LIBSD load") == 0, "LIBSD");
    CHECK(strcmp(pas_ps2_step(PAS_PS2_AUDSRV_FAILED), "audsrv module") == 0, "module");
    CHECK(strcmp(pas_ps2_step(PAS_PS2_RPC_FAILED), "audio RPC init") == 0, "RPC");
    CHECK(strcmp(pas_ps2_step(PAS_PS2_FORMAT_FAILED), "audio format") == 0, "format");
    CHECK(strcmp(pas_ps2_step(PAS_PS2_VOLUME_FAILED), "audio volume") == 0, "volume");
    CHECK(strcmp(pas_ps2_step(0), "") == 0 && strcmp(pas_ps2_step(-99), "") == 0, "anything else");
}

int main(void)
{
    test_start();
    test_failures();
    test_steps();
    printf("ps2_glue_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
