/* The memory probe: named values at guest addresses, a guest tick, stop rules and expectations. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "lgp/lgp.h"

#define NAME_MAX_CHARS 32
#define LIMIT 32

typedef struct Watch {
    char name[NAME_MAX_CHARS];
    uint32_t address;
    unsigned size;
    int big_endian;
    int have;
    uint32_t value;
} Watch;

typedef struct Rule {
    int watch; /* index into watches */
    uint32_t value;
    unsigned min_tick;
    int hit;
} Rule;

struct LgpProbe {
    Watch watches[LIMIT];
    unsigned watch_count;
    Rule expects[LIMIT];
    unsigned expect_count;
    Rule stops[LIMIT];
    unsigned stop_count;
    uint32_t mask;
    int has_tick;
    Watch tick;
    int warned;
};

LgpProbe *lgp_probe_new(void)
{
    LgpProbe *p = (LgpProbe *)calloc(1, sizeof(*p));
    if (p) {
        p->mask = 0xffffffffu;
    }
    return p;
}

void lgp_probe_free(LgpProbe *p)
{
    free(p);
}

void lgp_probe_set_mask(LgpProbe *p, uint32_t mask)
{
    if (p) {
        p->mask = mask;
    }
}

int lgp_probe_has_tick(const LgpProbe *p)
{
    return p && p->has_tick;
}

static int size_ok(unsigned size)
{
    return size == 1 || size == 2 || size == 4;
}

static int find_watch(const LgpProbe *p, const char *name)
{
    unsigned i;
    for (i = 0; i < p->watch_count; ++i) {
        if (strcmp(p->watches[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int lgp_probe_watch(LgpProbe *p, const char *name, uint32_t address, unsigned size, int big_endian)
{
    Watch *w;
    if (!p || !name || !*name || strlen(name) >= NAME_MAX_CHARS || !size_ok(size) || p->watch_count == LIMIT ||
        find_watch(p, name) >= 0) {
        return 0;
    }
    w = &p->watches[p->watch_count++];
    memset(w, 0, sizeof(*w));
    strcpy(w->name, name);
    w->address = address;
    w->size = size;
    w->big_endian = big_endian != 0;
    return 1;
}

int lgp_probe_tick(LgpProbe *p, uint32_t address, unsigned size, int big_endian)
{
    if (!p || !size_ok(size)) {
        return 0;
    }
    memset(&p->tick, 0, sizeof(p->tick));
    strcpy(p->tick.name, "tick");
    p->tick.address = address;
    p->tick.size = size;
    p->tick.big_endian = big_endian != 0;
    p->has_tick = 1;
    return 1;
}

int lgp_probe_expect(LgpProbe *p, const char *name, uint32_t value)
{
    int w;
    if (!p || !name || p->expect_count == LIMIT || (w = find_watch(p, name)) < 0) {
        return 0;
    }
    p->expects[p->expect_count].watch = w;
    p->expects[p->expect_count].value = value;
    p->expects[p->expect_count].min_tick = 0;
    p->expects[p->expect_count].hit = 0;
    p->expect_count++;
    return 1;
}

int lgp_probe_stop(LgpProbe *p, const char *name, uint32_t value, unsigned min_tick)
{
    int w;
    if (!p || !name || p->stop_count == LIMIT || (w = find_watch(p, name)) < 0) {
        return 0;
    }
    p->stops[p->stop_count].watch = w;
    p->stops[p->stop_count].value = value;
    p->stops[p->stop_count].min_tick = min_tick;
    p->stops[p->stop_count].hit = 0;
    p->stop_count++;
    return 1;
}

/* ---- text forms ------------------------------------------------------------------------------------------------ */

static int parse_number(const char *text, size_t length, uint32_t *value)
{
    char buf[24];
    char *end;
    unsigned long v;
    if (length == 0 || length >= sizeof(buf)) {
        return 0;
    }
    memcpy(buf, text, length);
    buf[length] = '\0';
    if (buf[0] == '-' || buf[0] == '+' || buf[0] == ' ') {
        return 0;
    }
    errno = 0; /* unsigned long is 32 bits on Windows: an overflow there reads as ULONG_MAX with ERANGE */
    v = strtoul(buf, &end, 0);
    if (*end || errno == ERANGE || v > 0xfffffffful) {
        return 0;
    }
    *value = (uint32_t)v;
    return 1;
}

/* "ADDR[:SIZE]" with an optional trailing "be" on the size: "0x100:2be". */
static int parse_address(const char *text, uint32_t *address, unsigned *size, int *big_endian)
{
    const char *colon = strchr(text, ':');
    uint32_t n = 4;
    size_t addr_len = colon ? (size_t)(colon - text) : strlen(text);
    *big_endian = 0;
    if (!parse_number(text, addr_len, address)) {
        return 0;
    }
    if (colon) {
        size_t len = strlen(colon + 1);
        if (len > 2 && strcmp(colon + 1 + len - 2, "be") == 0) {
            *big_endian = 1;
            len -= 2;
        }
        if (!parse_number(colon + 1, len, &n)) {
            return 0;
        }
    }
    *size = n;
    return size_ok(n);
}

int lgp_probe_parse_watch(LgpProbe *p, const char *text)
{
    const char *eq = text ? strchr(text, '=') : NULL;
    char name[NAME_MAX_CHARS];
    uint32_t address;
    unsigned size;
    int be;
    if (!eq || eq == text || (size_t)(eq - text) >= sizeof(name) || !parse_address(eq + 1, &address, &size, &be)) {
        return 0;
    }
    memcpy(name, text, (size_t)(eq - text));
    name[eq - text] = '\0';
    return lgp_probe_watch(p, name, address, size, be);
}

int lgp_probe_parse_tick(LgpProbe *p, const char *text)
{
    uint32_t address;
    unsigned size;
    int be;
    return text && parse_address(text, &address, &size, &be) && lgp_probe_tick(p, address, size, be);
}

int lgp_probe_parse_expect(LgpProbe *p, const char *text)
{
    const char *eq = text ? strchr(text, '=') : NULL;
    char name[NAME_MAX_CHARS];
    uint32_t value;
    if (!eq || eq == text || (size_t)(eq - text) >= sizeof(name) || !parse_number(eq + 1, strlen(eq + 1), &value)) {
        return 0;
    }
    memcpy(name, text, (size_t)(eq - text));
    name[eq - text] = '\0';
    return lgp_probe_expect(p, name, value);
}

int lgp_probe_parse_stop(LgpProbe *p, const char *text)
{
    const char *eq = text ? strchr(text, '=') : NULL;
    const char *at;
    char name[NAME_MAX_CHARS];
    uint32_t value, min_tick = 0;
    if (!eq || eq == text || (size_t)(eq - text) >= sizeof(name)) {
        return 0;
    }
    at = strchr(eq + 1, '@');
    if (!parse_number(eq + 1, at ? (size_t)(at - eq - 1) : strlen(eq + 1), &value) ||
        (at && !parse_number(at + 1, strlen(at + 1), &min_tick))) {
        return 0;
    }
    memcpy(name, text, (size_t)(eq - text));
    name[eq - text] = '\0';
    return lgp_probe_stop(p, name, value, min_tick);
}

/* ---- reading --------------------------------------------------------------------------------------------------- */

int lgp_memory_read(const uint8_t *memory, size_t size, uint32_t address, uint32_t mask, unsigned width, int big_endian,
                    uint32_t *value)
{
    uint32_t offset = address & mask;
    uint32_t v = 0;
    unsigned i;
    if (!memory || !size_ok(width) || offset > size || width > size - offset) {
        return 0;
    }
    for (i = 0; i < width; ++i) {
        unsigned byte = memory[offset + (big_endian ? width - 1 - i : i)];
        v |= (uint32_t)byte << (8 * i);
    }
    *value = v;
    return 1;
}

LgpSample lgp_probe_sample(LgpProbe *p, const uint8_t *memory, size_t size, unsigned host_frame, FILE *log)
{
    LgpSample out;
    unsigned i;
    uint32_t v;

    out.ready = 1;
    out.tick = host_frame;
    out.stop = 0;
    if (!p) {
        return out;
    }
    if (p->has_tick) {
        if (!lgp_memory_read(memory, size, p->tick.address, p->mask, p->tick.size, p->tick.big_endian, &v)) {
            if (log && !p->warned) {
                fprintf(log, "GUEST MEMORY unavailable size=%zu offset=%x\n", size, (unsigned)(p->tick.address & p->mask));
            }
            p->warned = 1;
            out.ready = 0;
            return out;
        }
        if (v == 0) { /* the guest has not started: nothing in its memory is meaningful yet */
            out.ready = 0;
            return out;
        }
        out.tick = v;
    }
    for (i = 0; i < p->watch_count; ++i) {
        Watch *w = &p->watches[i];
        if (!lgp_memory_read(memory, size, w->address, p->mask, w->size, w->big_endian, &v)) {
            if (log && !p->warned) {
                fprintf(log, "GUEST MEMORY unavailable size=%zu offset=%x\n", size, (unsigned)(w->address & p->mask));
            }
            p->warned = 1;
            continue;
        }
        if (!w->have || w->value != v) {
            if (log) {
                fprintf(log, "WATCH %s host_frame=%u tick=%u value=0x%x\n", w->name, host_frame, out.tick, (unsigned)v);
            }
            w->have = 1;
            w->value = v;
        }
    }
    for (i = 0; i < p->expect_count; ++i) {
        const Watch *w = &p->watches[p->expects[i].watch];
        if (w->have && w->value == p->expects[i].value) {
            p->expects[i].hit = 1;
        }
    }
    for (i = 0; i < p->stop_count; ++i) {
        const Watch *w = &p->watches[p->stops[i].watch];
        if (w->have && w->value == p->stops[i].value && out.tick >= p->stops[i].min_tick) {
            p->stops[i].hit = 1;
            out.stop = 1;
        }
    }
    return out;
}

int lgp_probe_report(const LgpProbe *p, FILE *out)
{
    unsigned i;
    int all = 1;
    if (!p) {
        return 1;
    }
    for (i = 0; i < p->expect_count; ++i) {
        if (out) {
            fprintf(out, "EXPECT %s=0x%x %s\n", p->watches[p->expects[i].watch].name, (unsigned)p->expects[i].value,
                    p->expects[i].hit ? "PASS" : "FAIL");
        }
        all = all && p->expects[i].hit;
    }
    return all;
}
