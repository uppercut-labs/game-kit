/* pad-replay: the recorder. It writes the same six-field format the reader takes. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "padr/padr.h"

struct PadrTrace {
    FILE *file;
    unsigned slots;
    unsigned flush_every;
    unsigned records;
    int failed;
    PadrSample *last;
    unsigned char *seen;
};

PadrTrace *padr_trace_open(const char *path, unsigned slots, const PadrTraceOptions *opt)
{
    PadrTrace *t;
    int error;

    if (!path || slots == 0 || slots > PADR_MAX_SLOTS) {
        errno = EINVAL;
        return NULL;
    }
    t = (PadrTrace *)calloc(1, sizeof(*t));
    if (!t) {
        return NULL;
    }
    t->slots = slots;
    t->flush_every = opt && opt->flush_every ? opt->flush_every : 60u;
    t->last = (PadrSample *)calloc(slots, sizeof(PadrSample));
    t->seen = (unsigned char *)calloc(slots, 1);
    /* "x" is C11's exclusive create: it fails with EEXIST instead of overwriting an earlier trace. */
    t->file = t->last && t->seen ? fopen(path, "wx") : NULL;
    if (!t->file) {
        error = t->last && t->seen ? errno : ENOMEM;
        free(t->last);
        free(t->seen);
        free(t);
        errno = error;
        return NULL;
    }
    return t;
}

int padr_trace_sample(PadrTrace *t, unsigned frame, const PadrSample *samples, unsigned count)
{
    unsigned i;

    if (!t || !t->file || t->failed) {
        return 0;
    }
    if (count > t->slots || (count && !samples)) {
        errno = EINVAL;
        t->failed = 1;
        return 0;
    }
    for (i = 0; i < count; ++i) {
        PadrSample in = samples[i];
        const PadrSample *old = &t->last[i];

        if (in.connected == PADR_UNREADABLE) {
            continue;
        }
        if (in.connected != PADR_FRESH) {
            in = padr_neutral(PADR_ABSENT);
        }
        if (!t->seen[i] || in.buttons != old->buttons || in.lx != old->lx || in.ly != old->ly ||
            in.connected != old->connected) {
            char line[64];
            size_t n = padr_format_event(line, sizeof(line), frame, i, &in);
            if (n == 0 || fwrite(line, 1, n, t->file) != n) {
                t->failed = 1;
                return 0;
            }
            t->last[i] = in;
            t->seen[i] = 1;
            t->records++;
        }
    }
    if (frame % t->flush_every == 0 && fflush(t->file) != 0) {
        t->failed = 1;
        return 0;
    }
    return 1;
}

unsigned padr_trace_records(const PadrTrace *t)
{
    return t ? t->records : 0;
}

int padr_trace_close(PadrTrace *t)
{
    int ok;
    if (!t) {
        return 1;
    }
    ok = t->file ? fclose(t->file) == 0 : 1;
    ok = ok && !t->failed;
    free(t->last);
    free(t->seen);
    free(t);
    return ok;
}
