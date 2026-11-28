/* audsrv behind PasOps, and its start. Builds only with the PS2SDK (or the test stubs in tests/ps2sdk-stub). */
#include "pas/ps2.h"

#include <audsrv.h>
#include <loadfile.h>

static int g_available(void *context)
{
    (void)context;
    return audsrv_available();
}

static int g_play(void *context, const void *data, int bytes)
{
    (void)context;
    return audsrv_play_audio((const char *)data, bytes);
}

static int g_set_volume(void *context, int volume)
{
    (void)context;
    return audsrv_set_volume(volume);
}

PasOps pas_ps2_ops(void)
{
    PasOps ops;
    ops.context = 0;
    ops.available = g_available;
    ops.play = g_play;
    ops.set_volume = g_set_volume;
    return ops;
}

static int fail(PasStream *stream, PasPs2Failure *failure, int code, int result, int status)
{
    if (stream) {
        stream->ready = 0;
    }
    if (failure) {
        failure->result = result;
        failure->status = status;
    }
    return code;
}

int pas_ps2_start(PasStream *stream, const PasPs2Config *config, PasPs2Failure *failure)
{
    struct audsrv_fmt_t format;
    PasOps ops;
    int result, status = 0;

    if (failure) {
        failure->result = 0;
        failure->status = 0;
    }
    if (!stream || !config || !config->audsrv || !config->size_audsrv || !config->render) {
        return fail(stream, failure, PAS_PS2_BAD_ARGUMENT, 0, 0);
    }
    stream->ready = 0;

    result = SifLoadModule("rom0:LIBSD", 0, 0);
    if (result < 0) {
        return fail(stream, failure, PAS_PS2_LIBSD_FAILED, result, 0);
    }
    status = -1;
    result = SifExecModuleBuffer((void *)config->audsrv, config->size_audsrv, 0, 0, &status);
    if (result < 0 || status != 0) {
        return fail(stream, failure, PAS_PS2_AUDSRV_FAILED, result, status);
    }
    result = audsrv_init();
    if (result) {
        return fail(stream, failure, PAS_PS2_RPC_FAILED, result, 0);
    }
    format.freq = config->rate;
    format.bits = 16;
    format.channels = 2;
    result = audsrv_set_format(&format);
    if (result) {
        return fail(stream, failure, PAS_PS2_FORMAT_FAILED, result, 0);
    }
    result = audsrv_set_volume(config->volume);
    if (result) {
        return fail(stream, failure, PAS_PS2_VOLUME_FAILED, result, 0);
    }
    ops = pas_ps2_ops();
    if (!pas_init(stream, &ops, config->render, config->user, config->chunk_frames, config->volume)) {
        return fail(stream, failure, PAS_PS2_BAD_ARGUMENT, 0, 0);
    }
    return 0;
}

const char *pas_ps2_step(int code)
{
    switch (code) {
    case PAS_PS2_LIBSD_FAILED:
        return "LIBSD load";
    case PAS_PS2_AUDSRV_FAILED:
        return "audsrv module";
    case PAS_PS2_RPC_FAILED:
        return "audio RPC init";
    case PAS_PS2_FORMAT_FAILED:
        return "audio format";
    case PAS_PS2_VOLUME_FAILED:
        return "audio volume";
    case PAS_PS2_BAD_ARGUMENT:
        return "bad argument";
    default:
        return "";
    }
}
