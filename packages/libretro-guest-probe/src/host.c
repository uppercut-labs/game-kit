/* The libretro host: load a core, answer its environment, feed it input, read its memory. One run at a time: the
 * libretro callbacks carry no user pointer, so the state lives in statics. */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <time.h>
#endif

#include "gl.h"
#include "libretro.h"
#include "lgp/lgp.h"

/* ---- the shared library and the clock --------------------------------------------------------------------------- */

#ifdef _WIN32
static void *dl_open(const char *path) { return (void *)LoadLibraryA(path); }
static void *dl_sym(void *lib, const char *name) { return (void *)GetProcAddress((HMODULE)lib, name); }
static void dl_close(void *lib) { FreeLibrary((HMODULE)lib); }
static const char *dl_error(void) { return "LoadLibrary failed"; }
static void sleep_ms(unsigned ms) { Sleep(ms); }
#else
static void *dl_open(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void *dl_sym(void *lib, const char *name) { return dlsym(lib, name); }
static void dl_close(void *lib) { dlclose(lib); }
static const char *dl_error(void)
{
    const char *e = dlerror();
    return e ? e : "unknown error";
}
static void sleep_ms(unsigned ms)
{
    struct timespec t;
    t.tv_sec = ms / 1000u;
    t.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&t, NULL);
}
#endif

/* ---- the state of the one run ------------------------------------------------------------------------------------ */

typedef struct CoreApi {
    void (*set_environment)(retro_environment_t);
    void (*set_video_refresh)(retro_video_refresh_t);
    void (*set_audio_sample)(retro_audio_sample_t);
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*set_input_poll)(retro_input_poll_t);
    void (*set_input_state)(retro_input_state_t);
    void (*init)(void);
    void (*deinit)(void);
    void (*get_system_info)(struct retro_system_info *);
    bool (*load_game)(const struct retro_game_info *);
    void (*unload_game)(void);
    void (*run)(void);
    void (*set_controller_port_device)(unsigned, unsigned);
    void *(*get_memory_data)(unsigned);
    size_t (*get_memory_size)(unsigned);
} CoreApi;

static struct {
    const LgpConfig *config;
    FILE *log;
    LgpPads pads;
    PadrTrace *trace;
    unsigned tick;
    struct retro_hw_render_callback hw;
    int hw_requested;
    int gl_open;
} H;

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
    va_list ap;
    (void)level;
    va_start(ap, fmt);
    vfprintf(H.log, fmt, ap);
    va_end(ap);
}

static uintptr_t framebuffer(void)
{
    return 0; /* the default framebuffer of the hidden window */
}

static retro_proc_address_t proc(const char *name)
{
    union {
        void *d;
        retro_proc_address_t f;
    } u;
    u.d = lgp_gl_proc(name);
    return u.f;
}

static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = H.config->system_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = log_cb;
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = false;
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned *)data = 2;
        return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        *(unsigned *)data = RETRO_LANGUAGE_ENGLISH;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = (struct retro_variable *)data;
        int first = 0;
        const char *value = lgp_options_get(H.config->options, v->key, &first);
        if (!value) {
            return false;
        }
        v->value = value;
        if (first) {
            fprintf(H.log, "OPTION %s=%s\n", v->key, value);
        }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
        struct retro_hw_render_callback *hw = (struct retro_hw_render_callback *)data;
        H.hw_requested = 1;
        fprintf(H.log, "HW_CONTEXT requested=%d version=%u.%u\n", (int)hw->context_type, hw->version_major,
                hw->version_minor);
        if (hw->context_type != RETRO_HW_CONTEXT_OPENGL && hw->context_type != RETRO_HW_CONTEXT_OPENGL_CORE) {
            return false;
        }
        if (!H.gl_open) {
            if (lgp_gl_open(H.config->gl_major, H.config->gl_minor, H.log) != 0) {
                fprintf(H.log, "HW_CONTEXT refused: no hidden OpenGL context could be made\n");
                return false;
            }
            H.gl_open = 1;
        }
        H.hw = *hw;
        hw->get_current_framebuffer = framebuffer;
        hw->get_proc_address = proc;
        H.hw.get_current_framebuffer = framebuffer;
        H.hw.get_proc_address = proc;
        return true;
    }
    default:
        return false;
    }
}

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{
    (void)data;
    (void)width;
    (void)height;
    (void)pitch;
}

static void audio_cb(int16_t left, int16_t right)
{
    (void)left;
    (void)right;
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
    (void)data;
    return frames;
}

/* One poll: the pad states for this tick, from the replay or, with none, connected pads at rest. Every poll is also
 * offered to the recorder, which keeps only the changes. */
static void input_poll_cb(void)
{
    unsigned port;
    PadrSample states[LGP_MAX_PORTS];
    padr_sample(H.config->script, H.tick, states, LGP_MAX_PORTS);
    for (port = 0; port < H.pads.ports; ++port) {
        H.pads.pad[port] = H.config->script ? states[port] : padr_neutral(PADR_FRESH);
        if (H.pads.pad[port].connected != PADR_FRESH) {
            H.pads.pad[port] = padr_neutral(PADR_ABSENT);
        }
    }
    if (H.trace) {
        padr_trace_sample(H.trace, H.tick, H.pads.pad, H.pads.ports);
    }
}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    return lgp_pads_state(&H.pads, port, device, index, id);
}

/* ---- the run ---------------------------------------------------------------------------------------------------- */

void lgp_config_defaults(LgpConfig *c)
{
    memset(c, 0, sizeof(*c));
    c->ports = 1;
    c->memory_id = RETRO_MEMORY_SYSTEM_RAM;
    c->max_frames = 20000;
    c->not_ready_delay_ms = 1;
    c->gl_major = 3;
    c->gl_minor = 2;
}

#define LOAD(field, name)                                    \
    do {                                                     \
        void *sym = dl_sym(lib, name);                       \
        if (!sym) {                                          \
            fprintf(H.log, "missing %s\n", name);            \
            return 0;                                        \
        }                                                    \
        memcpy(&api->field, &sym, sizeof(sym));              \
    } while (0)

static int load_api(void *lib, CoreApi *api)
{
    LOAD(set_environment, "retro_set_environment");
    LOAD(set_video_refresh, "retro_set_video_refresh");
    LOAD(set_audio_sample, "retro_set_audio_sample");
    LOAD(set_audio_sample_batch, "retro_set_audio_sample_batch");
    LOAD(set_input_poll, "retro_set_input_poll");
    LOAD(set_input_state, "retro_set_input_state");
    LOAD(init, "retro_init");
    LOAD(deinit, "retro_deinit");
    LOAD(get_system_info, "retro_get_system_info");
    LOAD(load_game, "retro_load_game");
    LOAD(unload_game, "retro_unload_game");
    LOAD(run, "retro_run");
    LOAD(set_controller_port_device, "retro_set_controller_port_device");
    LOAD(get_memory_data, "retro_get_memory_data");
    LOAD(get_memory_size, "retro_get_memory_size");
    return 1;
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    long length;
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (length = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    data = (uint8_t *)malloc((size_t)length ? (size_t)length : 1u);
    if (data && fread(data, 1, (size_t)length, f) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = (size_t)length;
    return data;
}

int lgp_run(const LgpConfig *config, LgpResult *result)
{
    LgpResult local;
    CoreApi api;
    void *lib;
    struct retro_system_info info;
    struct retro_game_info game;
    uint8_t *content = NULL;
    unsigned frame, port;
    int code = LGP_OK, stopped = 0;
    unsigned ports;

    if (!result) {
        result = &local;
    }
    memset(result, 0, sizeof(*result));
    result->expectations_met = 1;
    if (!config || !config->core_path || !config->system_dir) {
        return LGP_ERR_ARGS;
    }
    ports = config->ports ? config->ports : 1;
    if (ports > LGP_MAX_PORTS) {
        return LGP_ERR_ARGS;
    }

    memset(&H, 0, sizeof(H));
    H.config = config;
    H.log = config->log ? config->log : stderr;
    lgp_pads_init(&H.pads, config->map, config->map_count, ports);

    if (config->record_path) {
        H.trace = padr_trace_open(config->record_path, ports, NULL);
        if (!H.trace) {
            fprintf(H.log, "cannot create the record file %s\n", config->record_path);
            return LGP_ERR_IO;
        }
    }

    lib = dl_open(config->core_path);
    if (!lib) {
        fprintf(H.log, "dlopen: %s\n", dl_error());
        padr_trace_close(H.trace);
        return LGP_ERR_CORE;
    }
    if (!load_api(lib, &api)) {
        dl_close(lib);
        padr_trace_close(H.trace);
        return LGP_ERR_CORE;
    }

    api.set_environment(env_cb);
    api.set_video_refresh(video_cb);
    api.set_audio_sample(audio_cb);
    api.set_audio_sample_batch(audio_batch_cb);
    api.set_input_poll(input_poll_cb);
    api.set_input_state(input_state_cb);
    api.init();

    memset(&info, 0, sizeof(info));
    api.get_system_info(&info);
    fprintf(H.log, "CORE %s %s need_fullpath=%d\n", info.library_name ? info.library_name : "?",
            info.library_version ? info.library_version : "?", (int)info.need_fullpath);

    memset(&game, 0, sizeof(game));
    game.path = config->content_path;
    if (config->content_path && !info.need_fullpath) {
        size_t size = 0;
        content = read_file(config->content_path, &size);
        if (!content) {
            fprintf(H.log, "cannot read %s\n", config->content_path);
            code = LGP_ERR_LOAD;
            goto unload_core;
        }
        game.data = content;
        game.size = size;
    }
    if (!api.load_game(config->content_path ? &game : NULL)) {
        fprintf(H.log, "LOAD FAILED\n");
        code = LGP_ERR_LOAD;
        goto unload_core;
    }
    for (port = 0; port < ports; ++port) {
        api.set_controller_port_device(port, RETRO_DEVICE_JOYPAD);
    }
    if (H.hw_requested && H.gl_open && H.hw.context_reset) {
        H.hw.context_reset();
    }

    for (frame = 0; frame < (config->max_frames ? config->max_frames : 20000u); ++frame) {
        const uint8_t *memory;
        size_t size;
        LgpSample sample;

        if (!lgp_probe_has_tick(config->probe)) {
            H.tick = frame; /* no guest counter: the script's frames are the host's */
        }
        api.run();
        result->frames_run = frame + 1;

        memory = (const uint8_t *)api.get_memory_data(config->memory_id);
        size = memory ? api.get_memory_size(config->memory_id) : 0;
        sample = lgp_probe_sample(config->probe, memory, size, frame, H.log);
        if (!sample.ready) {
            result->not_ready_frames++;
            sleep_ms(config->not_ready_delay_ms);
            continue;
        }
        if (lgp_probe_has_tick(config->probe)) {
            H.tick = sample.tick;
        }
        result->last_tick = H.tick;
        if (sample.stop) {
            stopped = 1;
            break;
        }
        if (config->frame_delay_ms) {
            sleep_ms(config->frame_delay_ms);
        }
    }
    result->stopped = stopped;
    result->expectations_met = lgp_probe_report(config->probe, H.log);
    fprintf(H.log, "PROBE %s frames=%u not_ready=%u tick=%u stopped=%d\n", result->expectations_met ? "PASS" : "FAIL",
            result->frames_run, result->not_ready_frames, result->last_tick, stopped);
    if (!result->expectations_met) {
        code = LGP_EXPECT_FAILED;
    }

    api.unload_game();
unload_core:
    api.deinit();
    dl_close(lib);
    if (H.gl_open) {
        lgp_gl_close();
    }
    if (H.trace && !padr_trace_close(H.trace)) {
        fprintf(H.log, "the record file could not be written\n");
        if (code == LGP_OK) {
            code = LGP_ERR_IO;
        }
    }
    result->hw_render_requested = H.hw_requested;
    free(content);
    memset(&H, 0, sizeof(H));
    return code;
}
