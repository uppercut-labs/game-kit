/* Guest addresses: a RAM window (mask and mirror bases), symbol lookup in an nm listing, and 32-bit word reads. */
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "lgp/lgp.h"

void lgp_ram_window_init(LgpRamWindow *w, uint32_t mask)
{
    memset(w, 0, sizeof(*w));
    w->mask = mask;
}

int lgp_ram_window_add_base(LgpRamWindow *w, uint32_t base)
{
    if (!w || w->base_count >= LGP_MAX_RAM_BASES || (base & w->mask)) {
        return 0;
    }
    w->bases[w->base_count++] = base;
    return 1;
}

int lgp_ram_offset(const LgpRamWindow *w, uint32_t address, uint32_t *offset)
{
    unsigned i;
    if (!w || !offset) {
        return 0;
    }
    if (w->base_count == 0 || address <= w->mask) {
        *offset = address & w->mask;
        return 1;
    }
    for (i = 0; i < w->base_count; ++i) {
        if ((address & ~w->mask) == w->bases[i]) {
            *offset = address & w->mask;
            return 1;
        }
    }
    return 0;
}

int lgp_ram_parse_offset(const LgpRamWindow *w, const char *text, uint32_t *offset)
{
    char *end;
    unsigned long v;
    if (!text) {
        return 0;
    }
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    if (*text == '+') {
        ++text;
    }
    if (!isdigit((unsigned char)*text)) {
        return 0;
    }
    errno = 0;
    v = strtoul(text, &end, 0);
    if (*end || errno == ERANGE || v > 0xfffffffful) {
        return 0;
    }
    return lgp_ram_offset(w, (uint32_t)v, offset);
}

int lgp_nm_lookup(const char *nm_path, const char *symbol, uint32_t *address)
{
    FILE *f;
    char line[1024];
    int found = 0;
    if (!nm_path || !symbol || !*symbol || !address) {
        return 0;
    }
    f = fopen(nm_path, "r");
    if (!f) {
        return 0;
    }
    while (!found && fgets(line, sizeof(line), f)) {
        char *p = line, *end, *name;
        unsigned long v;
        size_t n;
        if (!isxdigit((unsigned char)*p)) {
            continue; /* an undefined symbol ("         U name") has no address */
        }
        errno = 0;
        v = strtoul(p, &end, 16);
        if (end == p || errno == ERANGE || v > 0xfffffffful || !isspace((unsigned char)*end)) {
            continue;
        }
        p = end;
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (!*p || isspace((unsigned char)*p)) {
            continue;
        }
        ++p; /* the symbol type letter */
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        name = p;
        n = strcspn(name, "\r\n");
        if (n == strlen(symbol) && strncmp(name, symbol, n) == 0) {
            *address = (uint32_t)v;
            found = 1;
        }
    }
    fclose(f);
    return found;
}

int lgp_read_words(const uint8_t *memory, size_t size, uint32_t offset, void *out, size_t count, int big_endian)
{
    uint8_t *o = (uint8_t *)out;
    size_t i;
    if (!memory || !out || offset > size || count > (size - offset) / 4u) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        const uint8_t *b = memory + offset + 4u * i;
        uint32_t word = big_endian ? ((uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3])
                                   : ((uint32_t)b[3] << 24 | (uint32_t)b[2] << 16 | (uint32_t)b[1] << 8 | b[0]);
        memcpy(o + 4u * i, &word, sizeof(word));
    }
    return 1;
}
