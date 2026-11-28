/* The libretro host: load a core, answer its environment, feed it input, read its memory, count and capture its video
 * and audio. One run at a time: the libretro callbacks carry no user pointer, so the state lives in statics. */
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
static uint64_t now_us(void)
{
    LARGE_INTEGER count, rate;
    QueryPerformanceCounter(&count);
    QueryPerformanceFrequency(&rate);
    return (uint64_t)(count.QuadPart / rate.QuadPart) * 1000000u +
           (uint64_t)(count.QuadPart % rate.QuadPart) * 1000000u / (uint64_t)rate.QuadPart;
}
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
static uint64_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
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
    void (*get_system_av_info)(struct retro_system_av_info *); /* optional */
    bool (*load_game)(const struct retro_game_info *);
    void (*unload_game)(void);
    void (*run)(void);
    void (*set_controller_port_device)(unsigned, unsigned);
    void *(*get_memory_data)(unsigned);
    size_t (*get_memory_size)(unsigned);
} CoreApi;

typedef struct Capture {
    FILE *video, *audio;
    int own_video, own_audio;
    int on;         /* writing this run's video and audio */
    int armed;      /* capture started by itself once (the first ready frame) */
    int done_after; /* the frame limit was reached in this run: stop when it ends */
    int error;
    int closed;
    unsigned width, height;
    uint8_t *frame; /* the last frame, top-down RGB24, written again for a dupe */
    uint8_t *flip;  /* a frame read back from OpenGL, bottom row first */
} Capture;

static struct {
    const LgpConfig *config;
    FILE *log;
    LgpPads pads;
    PadrTrace *trace;
    unsigned tick;
    struct retro_hw_render_callback hw;
    int hw_requested;
    int gl_open;
    unsigned pixel_format;
    struct retro_system_av_info av;
    int have_av;
    LgpCounters counters;
    LgpRunStats run;
    unsigned video_calls; /* in this run */
    uint16_t joypad[LGP_MAX_PORTS]; /* the hook's buttons, libretro joypad bits */
    Capture cap;
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
    return lgp_gl_target(); /* the framebuffer object, or 0 (the hidden window's own) when none could be made */
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

static void log_av(const char *how)
{
    fprintf(H.log, "AV %s fps=%.6f sample_rate=%.0f base=%ux%u max=%ux%u\n", how, H.av.timing.fps,
            H.av.timing.sample_rate, H.av.geometry.base_width, H.av.geometry.base_height, H.av.geometry.max_width,
            H.av.geometry.max_height);
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
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        unsigned format = (unsigned)*(const enum retro_pixel_format *)data;
        if (format > 2u) {
            return false;
        }
        H.pixel_format = format;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        H.av = *(const struct retro_system_av_info *)data;
        H.have_av = 1;
        log_av("set");
        return true;
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

/* ---- video and audio --------------------------------------------------------------------------------------------- */

static int capture_set(void)
{
    return H.cap.video || H.cap.audio;
}

static void capture_fail(const char *what)
{
    if (!H.cap.error) {
        fprintf(H.log, "CAPTURE error: %s\n", what);
    }
    H.cap.error = 1;
    H.cap.on = 0;
}

/* The frame buffers once the size is known: from the config, else the first frame. */
static int capture_size(unsigned width, unsigned height)
{
    size_t bytes;
    if (!H.cap.width) {
        H.cap.width = width;
        H.cap.height = height;
    }
    if (H.cap.frame) {
        return 1;
    }
    bytes = (size_t)H.cap.width * H.cap.height * 3u;
    H.cap.frame = (uint8_t *)calloc(bytes ? bytes : 1u, 1);
    H.cap.flip = (uint8_t *)calloc(bytes ? bytes : 1u, 1);
    if (!H.cap.frame || !H.cap.flip) {
        capture_fail("out of memory");
        return 0;
    }
    return 1;
}

static void capture_write_frame(void)
{
    const LgpConfig *c = H.config;
    size_t bytes = (size_t)H.cap.width * H.cap.height * 3u;
    if (!H.cap.video || H.cap.done_after) {
        return;
    }
    if (bytes && fwrite(H.cap.frame, bytes, 1, H.cap.video) != 1) {
        capture_fail("the video output could not be written");
        return;
    }
    H.counters.captured_video++;
    if (c->capture_max_frames && H.counters.captured_video >= c->capture_max_frames) {
        H.cap.done_after = 1; /* finish this run's audio, then stop */
    }
}

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{
    H.video_calls++;
    if (!data) {
        H.counters.repeated++;
        H.run.repeated++;
        if (H.cap.on && H.cap.frame) {
            capture_write_frame();
        }
        return;
    }
    H.counters.presented++;
    H.run.presented++;
    if (!H.cap.on || !H.cap.video || H.cap.done_after) {
        return;
    }
    if (!capture_size(width, height)) {
        return;
    }
    if (width != H.cap.width || height != H.cap.height) {
        char what[96];
        snprintf(what, sizeof(what), "a %ux%u frame, the capture is %ux%u", width, height, H.cap.width, H.cap.height);
        capture_fail(what);
        return;
    }
    if (data == RETRO_HW_FRAME_BUFFER_VALID) {
        unsigned y;
        size_t row = (size_t)width * 3u;
        if (lgp_gl_read_rgb(width, height, H.cap.flip) != 0) {
            capture_fail("the OpenGL frame could not be read");
            return;
        }
        for (y = 0; y < height; ++y) {
            memcpy(H.cap.frame + (size_t)y * row, H.cap.flip + (size_t)(height - 1u - y) * row, row);
        }
    } else if (!lgp_convert_rgb24(H.pixel_format, data, width, height, pitch, H.cap.frame)) {
        capture_fail("an unknown pixel format");
        return;
    }
    capture_write_frame();
}

static void capture_audio(const int16_t *data, size_t frames)
{
    if (!H.cap.on || !H.cap.audio || !frames) {
        return;
    }
    if (fwrite(data, 4u, frames, H.cap.audio) != frames) {
        capture_fail("the audio output could not be written");
        return;
    }
    H.counters.captured_audio += frames;
}

static void audio_cb(int16_t left, int16_t right)
{
    int16_t frame[2];
    frame[0] = left;
    frame[1] = right;
    H.counters.audio_frames++;
    H.run.audio_frames++;
    capture_audio(frame, 1);
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
    H.counters.audio_frames += frames;
    H.run.audio_frames += (unsigned)frames;
    capture_audio(data, frames);
    return frames;
}

/* ---- input ------------------------------------------------------------------------------------------------------- */

/* One poll: the pad states for this tick, from the replay or, with none, connected pads at rest. With a frame hook the
 * buttons are the hook's (libretro joypad bits). Every poll is also offered to the recorder, which keeps only changes. */
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
        if (H.config->frame_hook) {
            PadrSample seen[LGP_MAX_PORTS];
            for (port = 0; port < H.pads.ports; ++port) {
                seen[port] = H.pads.pad[port];
                seen[port].connected = PADR_FRESH;
                seen[port].buttons = H.joypad[port];
            }
            padr_trace_sample(H.trace, H.tick, seen, H.pads.ports);
        } else {
            padr_trace_sample(H.trace, H.tick, H.pads.pad, H.pads.ports);
        }
    }
}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (H.config->frame_hook) {
        if (port >= H.pads.ports) {
            return 0;
        }
        if (device == RETRO_DEVICE_ANALOG && index == RETRO_DEVICE_INDEX_ANALOG_LEFT) {
            PadrSample keep = H.pads.pad[port];
            int16_t v;
            H.pads.pad[port].connected = PADR_FRESH;
            v = lgp_pads_state(&H.pads, port, device, index, id);
            H.pads.pad[port] = keep;
            return v;
        }
        return lgp_joypad_state(H.joypad[port], device, index, id);
    }
    return lgp_pads_state(&H.pads, port, device, index, id);
}

/* The replay's buttons at `tick` for a port, as libretro joypad bits through the button map. */
static uint16_t script_joypad(unsigned port, unsigned tick)
{
    PadrSample states[LGP_MAX_PORTS];
    uint16_t bits = 0;
    unsigned i;
    if (!H.config->script) {
        return 0;
    }
    padr_sample(H.config->script, tick, states, LGP_MAX_PORTS);
    if (states[port].connected != PADR_FRESH) {
        return 0;
    }
    for (i = 0; i < H.pads.map_count; ++i) {
        if (states[port].buttons & H.pads.map[i].mask) {
            bits |= (uint16_t)(1u << H.pads.map[i].retro_id);
        }
    }
    return bits;
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
    void *sym;
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
    sym = dl_sym(lib, "retro_get_system_av_info");
    memcpy(&api->get_system_av_info, &sym, sizeof(sym));
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

static int capture_open(const LgpConfig *c)
{
    if (c->video_out) {
        H.cap.video = c->video_out;
    } else if (c->video_path) {
        H.cap.video = fopen(c->video_path, "wb");
        if (!H.cap.video) {
            fprintf(H.log, "cannot open the video output %s\n", c->video_path);
            return 0;
        }
        H.cap.own_video = 1;
    }
    if (c->audio_out) {
        H.cap.audio = c->audio_out;
    } else if (c->audio_path) {
        H.cap.audio = fopen(c->audio_path, "wb");
        if (!H.cap.audio) {
            fprintf(H.log, "cannot open the audio output %s\n", c->audio_path);
            return 0;
        }
        H.cap.own_audio = 1;
    }
    H.cap.width = c->capture_width;
    H.cap.height = c->capture_height;
    if (H.cap.video && H.cap.width && H.cap.height && !capture_size(H.cap.width, H.cap.height)) {
        return 0;
    }
    return 1;
}

/* Finish the outputs and say what was written. Once per run; later calls do nothing. */
static void capture_close(void)
{
    if (H.cap.closed) {
        return;
    }
    H.cap.closed = 1;
    H.cap.on = 0;
    if (H.cap.video && (H.cap.own_video ? fclose(H.cap.video) : fflush(H.cap.video)) != 0) {
        capture_fail("the video output could not be finished");
    }
    if (H.cap.audio && (H.cap.own_audio ? fclose(H.cap.audio) : fflush(H.cap.audio)) != 0) {
        capture_fail("the audio output could not be finished");
    }
    free(H.cap.frame);
    free(H.cap.flip);
    H.cap.frame = H.cap.flip = NULL;
    if (capture_set()) {
        fprintf(H.log, "CAPTURE video_frames=%u size=%ux%u audio_frames=%llu error=%d\n", H.counters.captured_video,
                H.counters.captured_video ? H.cap.width : 0u, H.counters.captured_video ? H.cap.height : 0u,
                (unsigned long long)H.counters.captured_audio, H.cap.error);
    }
}

static void run_hook(const LgpConfig *config, unsigned frame, const uint8_t *memory, size_t size, LgpSample sample,
                     int *stop)
{
    LgpFrameInfo info;
    LgpFrameControl control;
    unsigned port, next_tick;

    memset(&info, 0, sizeof(info));
    info.host_frame = frame;
    info.ready = sample.ready;
    info.tick = sample.ready ? sample.tick : H.tick;
    info.memory = memory;
    info.memory_size = size;
    info.run = H.run;
    info.total = &H.counters;
    info.fps = H.have_av ? H.av.timing.fps : 0;
    info.sample_rate = H.have_av ? H.av.timing.sample_rate : 0;
    info.ports = H.pads.ports;

    /* the tick the next poll will use: the guest's, or the next host frame */
    if (!lgp_probe_has_tick(config->probe)) {
        next_tick = frame + 1;
    } else {
        next_tick = sample.ready ? sample.tick : H.tick;
    }
    memset(&control, 0, sizeof(control));
    for (port = 0; port < H.pads.ports; ++port) {
        control.joypad[port] = script_joypad(port, next_tick);
    }
    control.capture = H.cap.on;
    config->frame_hook(config->frame_user, &info, &control);
    for (port = 0; port < H.pads.ports; ++port) {
        H.joypad[port] = control.joypad[port];
    }
    if (capture_set() && !H.cap.error) {
        int limit = config->capture_max_frames && H.counters.captured_video >= config->capture_max_frames;
        H.cap.on = control.capture && !limit;
    }
    *stop = control.stop;
}

int lgp_run(const LgpConfig *config, LgpResult *result)
{
    LgpResult local;
    CoreApi api;
    void *lib;
    struct retro_system_info info;
    struct retro_game_info game;
    uint8_t *content = NULL;
    unsigned frame, port, prev_tick = 0;
    int code = LGP_OK, stopped = 0, have_prev_tick = 0;
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
    H.pixel_format = RETRO_PIXEL_FORMAT_0RGB1555; /* libretro's default until the core says otherwise */
    lgp_pads_init(&H.pads, config->map, config->map_count, ports);

    if (config->record_path) {
        H.trace = padr_trace_open(config->record_path, ports, NULL);
        if (!H.trace) {
            fprintf(H.log, "cannot create the record file %s\n", config->record_path);
            return LGP_ERR_IO;
        }
    }
    if (!capture_open(config)) {
        capture_close();
        padr_trace_close(H.trace);
        memset(&H, 0, sizeof(H));
        result->capture_error = 1;
        return LGP_ERR_IO;
    }

    lib = dl_open(config->core_path);
    if (!lib) {
        fprintf(H.log, "dlopen: %s\n", dl_error());
        capture_close();
        padr_trace_close(H.trace);
        memset(&H, 0, sizeof(H));
        return LGP_ERR_CORE;
    }
    if (!load_api(lib, &api)) {
        dl_close(lib);
        capture_close();
        padr_trace_close(H.trace);
        memset(&H, 0, sizeof(H));
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
    if (api.get_system_av_info) {
        memset(&H.av, 0, sizeof(H.av));
        api.get_system_av_info(&H.av);
        H.have_av = 1;
        log_av("core");
    }
    if (H.hw_requested && H.gl_open) {
        unsigned w = H.av.geometry.max_width ? H.av.geometry.max_width : H.av.geometry.base_width;
        unsigned h = H.av.geometry.max_height ? H.av.geometry.max_height : H.av.geometry.base_height;
        if (w && h && lgp_gl_target_open(w, h, H.hw.depth, H.hw.stencil, H.log) == 0) {
            fprintf(H.log, "HW_CONTEXT framebuffer=%ux%u depth=%d stencil=%d\n", w, h, (int)H.hw.depth,
                    (int)H.hw.stencil);
        } else {
            fprintf(H.log, "HW_CONTEXT framebuffer=window (no framebuffer object)\n");
        }
        if (H.hw.context_reset) {
            H.hw.context_reset();
        }
    }

    for (frame = 0; frame < (config->max_frames ? config->max_frames : 20000u); ++frame) {
        const uint8_t *memory;
        size_t size;
        LgpSample sample;
        uint64_t start;
        unsigned took;
        int hook_stop = 0;

        if (!lgp_probe_has_tick(config->probe)) {
            H.tick = frame; /* no guest counter: the script's frames are the host's */
        }
        memset(&H.run, 0, sizeof(H.run));
        H.video_calls = 0;
        start = now_us();
        api.run();
        took = (unsigned)(now_us() - start);
        H.run.run_us = took;
        H.counters.runs++;
        if (took > H.counters.max_run_us) {
            H.counters.max_run_us = took;
        }
        if (!H.video_calls) {
            H.counters.no_video++;
        }
        if (H.cap.done_after) {
            H.cap.on = 0;
        }
        if (H.gl_open) {
            lgp_gl_pump();
        }
        result->frames_run = frame + 1;

        memory = (const uint8_t *)api.get_memory_data(config->memory_id);
        size = memory ? api.get_memory_size(config->memory_id) : 0;
        sample = lgp_probe_sample(config->probe, memory, size, frame, H.log);
        if (sample.ready && lgp_probe_has_tick(config->probe)) {
            if (have_prev_tick) {
                if (sample.tick == prev_tick) {
                    H.counters.guest_stalls++;
                } else if (sample.tick > prev_tick) {
                    H.run.tick_step = sample.tick - prev_tick;
                    H.counters.guest_leaps += sample.tick - prev_tick - 1u;
                }
            }
            prev_tick = sample.tick;
            have_prev_tick = 1;
        }
        if (sample.ready && capture_set() && !H.cap.armed && !H.cap.error) {
            H.cap.armed = 1;
            H.cap.on = 1; /* from the next run */
        }
        if (config->frame_hook) {
            run_hook(config, frame, memory, size, sample, &hook_stop);
        }
        if (H.cap.error) {
            break;
        }
        if (!sample.ready) {
            result->not_ready_frames++;
            if (hook_stop) {
                stopped = 1;
                break;
            }
            sleep_ms(config->not_ready_delay_ms);
            continue;
        }
        if (lgp_probe_has_tick(config->probe)) {
            H.tick = sample.tick;
        }
        result->last_tick = H.tick;
        if (sample.stop || hook_stop) {
            stopped = 1;
            break;
        }
        if (config->frame_delay_ms) {
            sleep_ms(config->frame_delay_ms);
        }
    }
    result->stopped = stopped;
    capture_close();
    fprintf(H.log, "FRAMES runs=%u presented=%u repeated=%u no_video=%u guest_stalls=%u guest_leaps=%u max_run_us=%u "
                   "audio_frames=%llu\n",
            H.counters.runs, H.counters.presented, H.counters.repeated, H.counters.no_video, H.counters.guest_stalls,
            H.counters.guest_leaps, H.counters.max_run_us, (unsigned long long)H.counters.audio_frames);
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
    capture_close();
    if (H.cap.error && code == LGP_OK) {
        code = LGP_ERR_IO;
    }
    result->hw_render_requested = H.hw_requested;
    result->hw_context = H.gl_open;
    result->counters = H.counters;
    result->fps = H.have_av ? H.av.timing.fps : 0;
    result->sample_rate = H.have_av ? H.av.timing.sample_rate : 0;
    result->video_width = H.counters.captured_video ? H.cap.width : 0;
    result->video_height = H.counters.captured_video ? H.cap.height : 0;
    result->capture_error = H.cap.error;
    free(content);
    memset(&H, 0, sizeof(H));
    return code;
}
