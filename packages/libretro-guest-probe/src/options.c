/* The core option table: key patterns with `*`, first match wins. */
#include <stdlib.h>
#include <string.h>

#include "lgp/lgp.h"

typedef struct Entry {
    char *pattern;
    char *value;
} Entry;

struct LgpOptions {
    Entry *entries;
    size_t count, capacity;
    char **answered; /* the keys already answered once */
    size_t answered_count, answered_capacity;
};

static char *copy_text(const char *text)
{
    size_t n = strlen(text) + 1;
    char *copy = (char *)malloc(n);
    if (copy) {
        memcpy(copy, text, n);
    }
    return copy;
}

int lgp_glob_match(const char *pattern, const char *text)
{
    const char *star = NULL, *mark = text;
    while (*text) {
        if (*pattern == '*') {
            star = pattern++;
            mark = text;
        } else if (*pattern == *text) {
            ++pattern;
            ++text;
        } else if (star) {
            pattern = star + 1;
            text = ++mark;
        } else {
            return 0;
        }
    }
    while (*pattern == '*') {
        ++pattern;
    }
    return *pattern == '\0';
}

LgpOptions *lgp_options_new(void)
{
    return (LgpOptions *)calloc(1, sizeof(LgpOptions));
}

void lgp_options_free(LgpOptions *o)
{
    size_t i;
    if (!o) {
        return;
    }
    for (i = 0; i < o->count; ++i) {
        free(o->entries[i].pattern);
        free(o->entries[i].value);
    }
    for (i = 0; i < o->answered_count; ++i) {
        free(o->answered[i]);
    }
    free(o->entries);
    free(o->answered);
    free(o);
}

int lgp_options_add(LgpOptions *o, const char *pattern, const char *value)
{
    if (!o || !pattern || !*pattern || !value) {
        return 0;
    }
    if (o->count == o->capacity) {
        size_t grown = o->capacity ? o->capacity * 2 : 8;
        Entry *bigger = (Entry *)realloc(o->entries, grown * sizeof(Entry));
        if (!bigger) {
            return 0;
        }
        o->entries = bigger;
        o->capacity = grown;
    }
    o->entries[o->count].pattern = copy_text(pattern);
    o->entries[o->count].value = copy_text(value);
    if (!o->entries[o->count].pattern || !o->entries[o->count].value) {
        free(o->entries[o->count].pattern);
        free(o->entries[o->count].value);
        return 0;
    }
    o->count++;
    return 1;
}

int lgp_options_parse(LgpOptions *o, const char *text)
{
    const char *eq = text ? strchr(text, '=') : NULL;
    char *pattern;
    int ok;
    if (!eq || eq == text) {
        return 0;
    }
    pattern = (char *)malloc((size_t)(eq - text) + 1);
    if (!pattern) {
        return 0;
    }
    memcpy(pattern, text, (size_t)(eq - text));
    pattern[eq - text] = '\0';
    ok = lgp_options_add(o, pattern, eq + 1);
    free(pattern);
    return ok;
}

const char *lgp_options_get(LgpOptions *o, const char *key, int *first)
{
    size_t i;
    if (first) {
        *first = 0;
    }
    if (!o || !key) {
        return NULL;
    }
    for (i = 0; i < o->count; ++i) {
        if (lgp_glob_match(o->entries[i].pattern, key)) {
            if (first) {
                size_t k;
                int seen = 0;
                for (k = 0; k < o->answered_count; ++k) {
                    if (strcmp(o->answered[k], key) == 0) {
                        seen = 1;
                        break;
                    }
                }
                if (!seen) {
                    if (o->answered_count == o->answered_capacity) {
                        size_t grown = o->answered_capacity ? o->answered_capacity * 2 : 16;
                        char **bigger = (char **)realloc(o->answered, grown * sizeof(char *));
                        if (bigger) {
                            o->answered = bigger;
                            o->answered_capacity = grown;
                        }
                    }
                    if (o->answered_count < o->answered_capacity) {
                        o->answered[o->answered_count] = copy_text(key);
                        if (o->answered[o->answered_count]) {
                            o->answered_count++;
                            *first = 1;
                        }
                    }
                }
            }
            return o->entries[i].value;
        }
    }
    return NULL;
}

size_t lgp_options_count(const LgpOptions *o)
{
    return o ? o->count : 0;
}
