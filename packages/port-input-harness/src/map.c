/* A port's table from controls to its state bits (pih.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pih/pih.h"

typedef struct {
    char who[32];
    char control[32];
    uint32_t bits;
    int primary;
} Row;

struct PihMap {
    Row *rows;
    int n, cap;
    PihMapEntry out[PIH_MAX_CONTROLS];
};

PihMap *pih_map_load(const char *path, char *err, size_t cap)
{
    size_t len = 0;
    char *raw = SDL_LoadFile(path, &len), *text, *line, *next;
    PihMap *m;
    int ln = 0;

    if (!raw) {
        if (err && cap) {
            snprintf(err, cap, "cannot read %s: %s", path, SDL_GetError());
        }
        return NULL;
    }
    text = malloc(len + 1);
    m = calloc(1, sizeof(*m));
    if (!text || !m) {
        SDL_free(raw);
        free(text);
        free(m);
        return NULL;
    }
    memcpy(text, raw, len);
    text[len] = 0;
    SDL_free(raw);
    for (line = text; line; line = next) {
        char who[32], control[32], word[32] = "", *c;
        unsigned bits;
        int n;

        next = strchr(line, '\n');
        if (next) {
            *next++ = 0;
        }
        ln++;
        c = strchr(line, '#');
        if (c) {
            *c = 0;
        }
        n = sscanf(line, "%31s %31s %i %31s", who, control, &bits, word);
        if (n == EOF || n <= 0) {
            continue;
        }
        if (n < 3 || (n == 4 && strcmp(word, "primary"))) {
            if (err && cap) {
                snprintf(err, cap, "%s:%d: a line is <who> <control> <bits> [primary]", path, ln);
            }
            goto bad;
        }
        if (m->n == m->cap) {
            Row *r = realloc(m->rows, sizeof(Row) * (size_t)(m->cap ? 2 * m->cap : 64));

            if (!r) {
                goto bad;
            }
            m->rows = r;
            m->cap = m->cap ? 2 * m->cap : 64;
        }
        snprintf(m->rows[m->n].who, sizeof(m->rows[0].who), "%s", who);
        snprintf(m->rows[m->n].control, sizeof(m->rows[0].control), "%s", control);
        m->rows[m->n].bits = bits;
        m->rows[m->n].primary = n == 4;
        m->n++;
    }
    free(text);
    return m;
bad:
    free(text);
    pih_map_free(m);
    return NULL;
}

void pih_map_free(PihMap *m)
{
    if (m) {
        free(m->rows);
        free(m);
    }
}

const PihMapEntry *pih_map_entries(PihMap *m, const PihProfile *p, int *n)
{
    int i, j;

    for (i = 0; i < p->ncontrols; i++) {
        const Row *best = NULL;

        for (j = 0; j < m->n; j++) {
            const Row *r = &m->rows[j];

            if (strcmp(r->control, p->controls[i].name)) {
                continue;
            }
            if (!strcmp(r->who, p->name)) {
                best = r;   /* the profile's own line wins */
                break;
            }
            if (!strcmp(r->who, p->layout)) {
                best = r;
            }
        }
        m->out[i].control = &p->controls[i];
        m->out[i].bits = best ? best->bits : 0;
        m->out[i].primary = best ? best->primary : 0;
    }
    *n = p->ncontrols;
    return m->out;
}

uint32_t pih_hold(PihDevice *d, PihMap *m, uint32_t want)
{
    int n, i;
    const PihMapEntry *e = pih_map_entries(m, pih_profile(d), &n);
    uint32_t can = 0;

    for (i = 0; i < n; i++) {
        if (e[i].primary && e[i].bits) {
            pih_set_control(d, e[i].control, (e[i].bits & want) != 0);
            can |= e[i].bits;
        }
    }
    return want & ~can;
}
