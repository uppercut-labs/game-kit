/* pad-replay: the reader, the player, the line writer and the button names. The recorder is in padtrace.c. */
#include "padr/padr.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct Event {
    unsigned frame;
    uint16_t buttons;
    uint8_t slot, lx, ly, connected;
} Event;

struct PadrScript {
    Event *events;
    unsigned count;
    unsigned slots;
    unsigned position;
    PadrSample *state; /* one per slot */
};

PadrSample padr_neutral(uint8_t connected)
{
    PadrSample s;
    s.buttons = 0;
    s.lx = PADR_NEUTRAL_AXIS;
    s.ly = PADR_NEUTRAL_AXIS;
    s.connected = connected;
    return s;
}

const char *padr_status_text(PadrStatus status)
{
    switch (status) {
    case PADR_OK: return "ok";
    case PADR_ERR_OPEN: return "cannot open the file";
    case PADR_ERR_READ: return "read error";
    case PADR_ERR_NUL: return "NUL byte in the input";
    case PADR_ERR_LINE_LONG: return "line too long";
    case PADR_ERR_FIELD: return "invalid field";
    case PADR_ERR_TRAILING: return "extra text after the sixth field";
    case PADR_ERR_ORDER: return "frames out of order";
    case PADR_ERR_TOO_MANY: return "too many events";
    case PADR_ERR_EMPTY: return "no events";
    case PADR_ERR_MEMORY: return "out of memory";
    case PADR_ERR_ARGUMENT: return "bad argument";
    }
    return "unknown status";
}

/* ---- parsing ------------------------------------------------------------------------------------------------- */

typedef struct Parser {
    Event *events;
    unsigned count, capacity;
    unsigned slots, max_events;
    unsigned line_number;
    PadrError err;
} Parser;

static const char *skip_space(const char *text)
{
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    return text;
}

/* One field: plain digits (hex digits for base 16, which also takes a 0x prefix), no sign, nothing glued on, in range. */
static int read_field(const char **cursor, int base, unsigned long maximum, unsigned long *value)
{
    const char *start = skip_space(*cursor);
    unsigned char first = (unsigned char)*start;
    char *end;
    unsigned long field;

    if (!(base == 16 ? isxdigit(first) : isdigit(first))) {
        return 0;
    }
    errno = 0;
    field = strtoul(start, &end, base);
    if (errno == ERANGE || field > maximum || (*end && !isspace((unsigned char)*end))) {
        return 0;
    }
    *cursor = end;
    *value = field;
    return 1;
}

static int fail(Parser *p, PadrStatus status)
{
    p->err.status = status;
    p->err.line = p->line_number;
    return 0;
}

/* `line` is NUL-terminated and holds no line break. Returns 1 to go on, 0 on an error (recorded in p->err). */
static int parse_line(Parser *p, const char *line)
{
    const char *cursor = skip_space(line);
    unsigned long frame, slot, held, lx, ly, connected;
    Event *e;

    if (!*cursor || *cursor == '#') {
        return 1;
    }
    if (!read_field(&cursor, 10, UINT_MAX, &frame) || !read_field(&cursor, 10, p->slots - 1u, &slot) ||
        !read_field(&cursor, 16, UINT16_MAX, &held) || !read_field(&cursor, 10, UINT8_MAX, &lx) ||
        !read_field(&cursor, 10, UINT8_MAX, &ly) || !read_field(&cursor, 10, 1, &connected)) {
        return fail(p, PADR_ERR_FIELD);
    }
    if (*skip_space(cursor)) {
        return fail(p, PADR_ERR_TRAILING);
    }
    if (p->count && frame < p->events[p->count - 1].frame) {
        return fail(p, PADR_ERR_ORDER);
    }
    if (p->count == p->max_events) {
        return fail(p, PADR_ERR_TOO_MANY);
    }
    if (p->count == p->capacity) {
        unsigned grown = p->capacity ? p->capacity * 2u : 64u;
        Event *bigger;
        if (grown > p->max_events || grown < p->capacity) {
            grown = p->max_events;
        }
        bigger = (Event *)realloc(p->events, (size_t)grown * sizeof(Event));
        if (!bigger) {
            return fail(p, PADR_ERR_MEMORY);
        }
        p->events = bigger;
        p->capacity = grown;
    }
    e = &p->events[p->count++];
    e->frame = (unsigned)frame;
    e->slot = (uint8_t)slot;
    /* A pad that is not connected is at rest whatever else the line says. */
    e->connected = (uint8_t)(connected ? PADR_FRESH : PADR_ABSENT);
    e->buttons = connected ? (uint16_t)held : 0;
    e->lx = connected ? (uint8_t)lx : PADR_NEUTRAL_AXIS;
    e->ly = connected ? (uint8_t)ly : PADR_NEUTRAL_AXIS;
    return 1;
}

static void parser_init(Parser *p, const PadrOptions *opt)
{
    memset(p, 0, sizeof(*p));
    p->slots = opt && opt->slots ? opt->slots : PADR_DEFAULT_SLOTS;
    p->max_events = opt && opt->max_events ? opt->max_events : PADR_DEFAULT_MAX_EVENTS;
}

static void report(const Parser *p, PadrError *err)
{
    if (err) {
        *err = p->err;
    }
}

static PadrScript *finish(Parser *p, PadrError *err)
{
    PadrScript *s;
    unsigned i;

    if (!p->count) {
        p->line_number = 0;
        fail(p, PADR_ERR_EMPTY);
        free(p->events);
        report(p, err);
        return NULL;
    }
    s = (PadrScript *)calloc(1, sizeof(*s));
    if (s) {
        s->state = (PadrSample *)calloc(p->slots, sizeof(PadrSample));
    }
    if (!s || !s->state) {
        free(s);
        free(p->events);
        p->line_number = 0;
        fail(p, PADR_ERR_MEMORY);
        report(p, err);
        return NULL;
    }
    s->events = p->events;
    s->count = p->count;
    s->slots = p->slots;
    for (i = 0; i < s->slots; ++i) {
        s->state[i] = padr_neutral(PADR_ABSENT);
    }
    p->err.status = PADR_OK;
    p->err.line = 0;
    report(p, err);
    return s;
}

static int options_ok(const PadrOptions *opt)
{
    return !opt || opt->slots <= PADR_MAX_SLOTS;
}

PadrScript *padr_load_file(const char *path, const PadrOptions *opt, PadrError *err)
{
    Parser p;
    FILE *file;
    char line[PADR_MAX_LINE + 1];
    size_t length = 0;
    int byte;
    int broken = 0;

    parser_init(&p, opt);
    if (!path || !options_ok(opt)) {
        fail(&p, PADR_ERR_ARGUMENT);
        report(&p, err);
        return NULL;
    }
    file = fopen(path, "rb");
    if (!file) {
        fail(&p, PADR_ERR_OPEN);
        report(&p, err);
        return NULL;
    }
    for (;;) {
        byte = fgetc(file);
        if (byte == EOF || byte == '\n') {
            if (byte == EOF && length == 0) {
                break; /* no unfinished last line */
            }
            line[length] = '\0';
            ++p.line_number;
            if (!parse_line(&p, line)) {
                broken = 1;
                break;
            }
            length = 0;
            if (byte == EOF) {
                break;
            }
            continue;
        }
        if (byte == '\0' || length == PADR_MAX_LINE) {
            ++p.line_number;
            fail(&p, byte == '\0' ? PADR_ERR_NUL : PADR_ERR_LINE_LONG);
            broken = 1;
            break;
        }
        line[length++] = (char)byte;
    }
    if (!broken && ferror(file)) {
        fail(&p, PADR_ERR_READ);
        broken = 1;
    }
    fclose(file);
    if (broken) {
        free(p.events);
        report(&p, err);
        return NULL;
    }
    return finish(&p, err);
}

PadrScript *padr_load_text(const char *text, size_t length, const PadrOptions *opt, PadrError *err)
{
    Parser p;
    char line[PADR_MAX_LINE + 1];
    size_t i = 0;

    parser_init(&p, opt);
    if (!text || !options_ok(opt)) {
        fail(&p, PADR_ERR_ARGUMENT);
        report(&p, err);
        return NULL;
    }
    while (i < length) {
        size_t n = 0;
        ++p.line_number;
        while (i < length && text[i] != '\n') {
            if (text[i] == '\0' || n == PADR_MAX_LINE) {
                fail(&p, text[i] == '\0' ? PADR_ERR_NUL : PADR_ERR_LINE_LONG);
                free(p.events);
                report(&p, err);
                return NULL;
            }
            line[n++] = text[i++];
        }
        if (i < length) {
            ++i; /* the line break */
        }
        line[n] = '\0';
        if (!parse_line(&p, line)) {
            free(p.events);
            report(&p, err);
            return NULL;
        }
    }
    return finish(&p, err);
}

/* ---- playing ------------------------------------------------------------------------------------------------- */

void padr_free(PadrScript *script)
{
    if (script) {
        free(script->events);
        free(script->state);
        free(script);
    }
}

unsigned padr_slots(const PadrScript *script)
{
    return script ? script->slots : 0;
}

unsigned padr_event_count(const PadrScript *script)
{
    return script ? script->count : 0;
}

unsigned padr_last_frame(const PadrScript *script)
{
    return script && script->count ? script->events[script->count - 1].frame : 0;
}

void padr_rewind(PadrScript *script)
{
    unsigned i;
    if (!script) {
        return;
    }
    script->position = 0;
    for (i = 0; i < script->slots; ++i) {
        script->state[i] = padr_neutral(PADR_ABSENT);
    }
}

void padr_sample(PadrScript *script, unsigned frame, PadrSample *out, unsigned out_slots)
{
    unsigned i, known = 0;

    if (script) {
        while (script->position < script->count && script->events[script->position].frame <= frame) {
            const Event *e = &script->events[script->position++];
            PadrSample *s = &script->state[e->slot];
            s->buttons = e->buttons;
            s->lx = e->lx;
            s->ly = e->ly;
            s->connected = e->connected;
        }
        known = out_slots < script->slots ? out_slots : script->slots;
        for (i = 0; i < known; ++i) {
            out[i] = script->state[i];
        }
    }
    for (i = known; i < out_slots; ++i) {
        out[i] = padr_neutral(PADR_ABSENT);
    }
}

/* ---- one line ------------------------------------------------------------------------------------------------ */

size_t padr_format_event(char *buf, size_t cap, unsigned frame, unsigned slot, const PadrSample *sample)
{
    int n;
    PadrSample s = padr_neutral(PADR_ABSENT);
    if (sample && sample->connected == PADR_FRESH) {
        s = *sample;
    }
    n = snprintf(buf, cap, "%u %u %04x %u %u %u\n", frame, slot, (unsigned)s.buttons, (unsigned)s.lx, (unsigned)s.ly,
                 (unsigned)(s.connected == PADR_FRESH));
    return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

/* ---- button names -------------------------------------------------------------------------------------------- */

static int same_name(const char *a, size_t a_len, const char *b)
{
    size_t i;
    if (strlen(b) != a_len) {
        return 0;
    }
    for (i = 0; i < a_len; ++i) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
    }
    return 1;
}

int padr_buttons_parse(const PadrButton *table, size_t count, const char *text, uint16_t *mask)
{
    unsigned total = 0;
    const char *cursor;

    if (!text || !mask || (count && !table)) {
        return 0;
    }
    cursor = skip_space(text);
    for (;;) {
        const char *end = cursor;
        size_t len, i;
        int found = 0;

        while (*end && *end != '+') {
            ++end;
        }
        while (end > cursor && isspace((unsigned char)end[-1])) {
            --end;
        }
        len = (size_t)(end - cursor);
        if (len == 0) {
            return 0;
        }
        if (same_name(cursor, len, "none")) {
            found = 1;
        } else if (len > 2 && cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
            char digits[16];
            char *stop;
            unsigned long v;
            if (len >= sizeof(digits)) {
                return 0;
            }
            memcpy(digits, cursor, len);
            digits[len] = '\0';
            v = strtoul(digits, &stop, 16);
            if (*stop || v > UINT16_MAX) {
                return 0;
            }
            total |= (unsigned)v;
            found = 1;
        } else {
            for (i = 0; i < count; ++i) {
                if (same_name(cursor, len, table[i].name)) {
                    total |= table[i].mask;
                    found = 1;
                    break;
                }
            }
        }
        if (!found) {
            return 0;
        }
        cursor = end;
        while (isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (!*cursor) {
            break;
        }
        ++cursor; /* the plus */
        cursor = skip_space(cursor);
    }
    *mask = (uint16_t)total;
    return 1;
}

size_t padr_buttons_format(const PadrButton *table, size_t count, uint16_t mask, char *buf, size_t cap)
{
    size_t used = 0, i;
    unsigned covered = 0;
    int n;

    if (!buf || !cap) {
        return 0;
    }
    buf[0] = '\0';
    if (mask == 0) {
        n = snprintf(buf, cap, "none");
        return n > 0 && (size_t)n < cap ? (size_t)n : 0;
    }
    for (i = 0; i < count; ++i) {
        if (table[i].mask && (mask & table[i].mask) == table[i].mask && (covered & table[i].mask) != table[i].mask) {
            n = snprintf(buf + used, cap - used, "%s%s", used ? "+" : "", table[i].name);
            if (n < 0 || (size_t)n >= cap - used) {
                buf[0] = '\0';
                return 0;
            }
            used += (size_t)n;
            covered |= table[i].mask;
        }
    }
    if (mask & ~covered) {
        n = snprintf(buf + used, cap - used, "%s0x%x", used ? "+" : "", (unsigned)(mask & ~covered));
        if (n < 0 || (size_t)n >= cap - used) {
            buf[0] = '\0';
            return 0;
        }
        used += (size_t)n;
    }
    return used;
}
