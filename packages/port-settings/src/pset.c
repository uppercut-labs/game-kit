/* port-settings (pset.h). */
#include "pset/pset.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

typedef struct {
    char *key, *value;
} Entry;

struct PortSettings {
    char *path;
    char *name, *header;
    void (*log)(void *user, const char *line);
    void *log_user;
    Entry *e;
    size_t n, cap;
};

static char *dup_n(const char *s, size_t n)
{
    char *d = malloc(n + 1);

    if (d) {
        memcpy(d, s, n);
        d[n] = 0;
    }
    return d;
}

static void warn(const PortSettings *s, const char *fmt, ...)
{
    char line[1200];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (s->log) {
        s->log(s->log_user, line);
    } else {
        fprintf(stderr, "%s: %s\n", s->name, line);
    }
}

/* ------------------------------------------------------------------ files (UTF-8 paths on Windows) */

#ifdef _WIN32
static wchar_t *widen(const char *u8)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, u8, -1, NULL, 0);
    wchar_t *w = n > 0 ? malloc((size_t)n * sizeof(wchar_t)) : NULL;

    if (w && MultiByteToWideChar(CP_UTF8, 0, u8, -1, w, n) <= 0) {
        free(w);
        w = NULL;
    }
    return w;
}
#endif

static FILE *file_open(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t *w = widen(path), wm[8];
    FILE *f = NULL;
    size_t i;

    for (i = 0; mode[i] && i < 7; i++) {
        wm[i] = (wchar_t)mode[i];
    }
    wm[i] = 0;
    if (w) {
        f = _wfopen(w, wm);
    }
    free(w);
    return f;
#else
    return fopen(path, mode);
#endif
}

static int make_dir(const char *path)
{
#ifdef _WIN32
    wchar_t *w = widen(path);
    int rc;

    if (!w) {
        return -1;
    }
    rc = _wmkdir(w);
    free(w);
#else
    int rc = mkdir(path, 0777);
#endif
    return rc == 0 || errno == EEXIST ? 0 : -1;
}

/* every folder of dir, outermost first */
static int make_dirs(const char *dir)
{
    char buf[1100];
    size_t i, len = strlen(dir);

    if (len >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, dir, len + 1);
    for (i = 1; i < len; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char c = buf[i];

            if (buf[i - 1] == ':' || buf[i - 1] == '/' || buf[i - 1] == '\\') {
                continue;   /* "C:\" or "//" */
            }
            buf[i] = 0;
            if (make_dir(buf) != 0) {
                return -1;
            }
            buf[i] = c;
        }
    }
    return make_dir(buf);
}

static int replace_file(const char *from, const char *to)
{
#ifdef _WIN32
    wchar_t *a = widen(from), *b = widen(to);
    int rc = a && b && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;

    free(a);
    free(b);
    return rc;
#else
    return rename(from, to) == 0 ? 0 : -1;
#endif
}

/* the whole file, NUL-terminated; NULL if it cannot be read */
static char *read_file(const char *path, size_t *size)
{
    FILE *f = file_open(path, "rb");
    char *buf = NULL;
    size_t len = 0, cap = 0;

    if (!f) {
        return NULL;
    }
    for (;;) {
        size_t got;

        if (len + 4096 + 1 > cap) {
            char *nb;

            cap = cap ? cap * 2 : 8192;
            nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = nb;
        }
        got = fread(buf + len, 1, cap - len - 1, f);
        len += got;
        if (got == 0) {
            break;
        }
    }
    fclose(f);
    if (buf) {
        buf[len] = 0;
    }
    *size = len;
    return buf;
}

/* ------------------------------------------------------------------ the object */

static void trim(const char **s, size_t *n)
{
    while (*n && (**s == ' ' || **s == '\t')) {
        (*s)++, (*n)--;
    }
    while (*n && ((*s)[*n - 1] == ' ' || (*s)[*n - 1] == '\t' || (*s)[*n - 1] == '\r')) {
        (*n)--;
    }
}

PortSettings *pset_open(const char *path, const PsetOptions *opt)
{
    PortSettings *s = calloc(1, sizeof(PortSettings));
    size_t size = 0;
    char *text, *line, *next;
    int no = 0;

    if (!s) {
        return NULL;
    }
    s->path = dup_n(path, strlen(path));
    s->name = dup_n(opt && opt->name ? opt->name : "settings", strlen(opt && opt->name ? opt->name : "settings"));
    if (opt && opt->header) {
        s->header = dup_n(opt->header, strlen(opt->header));
    }
    if (opt) {
        s->log = opt->log;
        s->log_user = opt->log_user;
    }
    if (!s->path || !s->name || (opt && opt->header && !s->header)) {
        pset_free(s);
        return NULL;
    }
    text = read_file(path, &size);
    if (!text) {
        return s;
    }
    line = text;
    if (size >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) {
        line += 3;   /* a byte order mark from an editor */
    }
    for (; line && *line; line = next) {
        const char *k = line, *v, *eq;
        char *hash, *key, *value;
        size_t kn, vn;

        next = strchr(line, '\n');
        if (next) {
            *next++ = 0;
        }
        no++;
        if ((hash = strchr(line, '#')) != NULL) {
            *hash = 0;
        }
        eq = strchr(line, '=');
        kn = eq ? (size_t)(eq - line) : strlen(line);
        trim(&k, &kn);
        if (!eq) {
            if (kn) {
                warn(s, "%s:%d: not \"key = value\": ignored", path, no);
            }
            continue;
        }
        v = eq + 1;
        vn = strlen(v);
        trim(&v, &vn);
        if (!kn) {
            warn(s, "%s:%d: no key: ignored", path, no);
            continue;
        }
        key = dup_n(k, kn);
        value = dup_n(v, vn);
        if (key && value) {
            pset_set(s, key, value);
        }
        free(key);
        free(value);
    }
    free(text);
    return s;
}

PortSettings *pset_load(const char *path)
{
    return pset_open(path, NULL);
}

void pset_free(PortSettings *s)
{
    size_t i;

    if (!s) {
        return;
    }
    for (i = 0; i < s->n; i++) {
        free(s->e[i].key);
        free(s->e[i].value);
    }
    free(s->e);
    free(s->path);
    free(s->name);
    free(s->header);
    free(s);
}

static Entry *find(const PortSettings *s, const char *key)
{
    size_t i;

    for (i = 0; s && key && i < s->n; i++) {
        if (!strcmp(s->e[i].key, key)) {
            return &s->e[i];
        }
    }
    return NULL;
}

const char *pset_get(const PortSettings *s, const char *key)
{
    const Entry *e = find(s, key);

    return e ? e->value : NULL;
}

const char *pset_get_or(const PortSettings *s, const char *key, const char *fallback)
{
    const char *v = pset_get(s, key);

    return v ? v : fallback;
}

int pset_get_int(const PortSettings *s, const char *key, int fallback)
{
    const char *v = pset_get(s, key);
    char *end;
    long n;

    if (!v || !*v) {
        return fallback;
    }
    errno = 0;
    n = strtol(v, &end, 10);
    return *end || errno || n < -2147483647L || n > 2147483647L ? fallback : (int)n;
}

int pset_get_int_step(const PortSettings *s, const char *key, int fallback, int lo, int hi, int step)
{
    int v = pset_get_int(s, key, fallback);

    if (step > 1) {
        v = (v + (v < 0 ? -step / 2 : step / 2)) / step * step;
    }
    return v < lo ? lo : v > hi ? hi : v;
}

static int same_word(const char *a, const char *b)
{
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++, b++;
    }
    return !*a && !*b;
}

int pset_get_bool(const PortSettings *s, const char *key, int fallback)
{
    static const char *const ON[] = { "on", "true", "yes", "1" };
    static const char *const OFF[] = { "off", "false", "no", "0" };
    const char *v = pset_get(s, key);
    size_t i;

    for (i = 0; v && i < 4; i++) {
        if (same_word(v, ON[i])) {
            return 1;
        }
        if (same_word(v, OFF[i])) {
            return 0;
        }
    }
    return fallback;
}

int pset_get_choice(const PortSettings *s, const char *key, const char *const *names, int n, int fallback)
{
    const char *v = pset_get(s, key);
    int i;

    for (i = 0; v && i < n; i++) {
        if (!strcmp(v, names[i])) {
            return i;
        }
    }
    return fallback;
}

int pset_set(PortSettings *s, const char *key, const char *value)
{
    char *v;
    Entry *e;

    if (!key || !*key || strpbrk(key, "=#\r\n") || !value || strpbrk(value, "#\r\n")) {
        return -1;
    }
    v = dup_n(value, strlen(value));
    if (!v) {
        return -1;
    }
    if ((e = find(s, key)) != NULL) {
        free(e->value);
        e->value = v;
        return 0;
    }
    if (s->n == s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 32;
        Entry *ne = realloc(s->e, cap * sizeof(Entry));

        if (!ne) {
            free(v);
            return -1;
        }
        s->e = ne;
        s->cap = cap;
    }
    s->e[s->n].key = dup_n(key, strlen(key));
    s->e[s->n].value = v;
    if (!s->e[s->n].key) {
        free(v);
        return -1;
    }
    s->n++;
    return 0;
}

int pset_set_int(PortSettings *s, const char *key, int value)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%d", value);
    return pset_set(s, key, buf);
}

int pset_set_bool(PortSettings *s, const char *key, int on)
{
    return pset_set(s, key, on ? "on" : "off");
}

int pset_remove(PortSettings *s, const char *key)
{
    Entry *e = find(s, key);

    if (!e) {
        return 0;
    }
    free(e->key);
    free(e->value);
    memmove(e, e + 1, (size_t)(s->e + s->n - e - 1) * sizeof(Entry));
    s->n--;
    return 1;
}

size_t pset_count(const PortSettings *s)
{
    return s ? s->n : 0;
}

const char *pset_key_at(const PortSettings *s, size_t i)
{
    return s && i < s->n ? s->e[i].key : NULL;
}

const char *pset_value_at(const PortSettings *s, size_t i)
{
    return s && i < s->n ? s->e[i].value : NULL;
}

const char *pset_path(const PortSettings *s)
{
    return s->path;
}

int pset_save(const PortSettings *s)
{
    char dir[1100], tmp[1200];
    const char *slash = strrchr(s->path, '/');
    FILE *f;
    size_t i;
    int rc = 0;

    {
        const char *b = strrchr(s->path, '\\');

        if (b && (!slash || b > slash)) {
            slash = b;
        }
    }
    if (slash && slash > s->path) {
        size_t n = (size_t)(slash - s->path);

        if (n >= sizeof(dir)) {
            return -1;
        }
        memcpy(dir, s->path, n);
        dir[n] = 0;
        if (make_dirs(dir) != 0) {
            return -1;
        }
    }
    if (snprintf(tmp, sizeof(tmp), "%s.part", s->path) >= (int)sizeof(tmp) || !(f = file_open(tmp, "wb"))) {
        return -1;
    }
    if (s->header && fprintf(f, "# %s\n", s->header) < 0) {
        rc = -1;
    }
    for (i = 0; i < s->n; i++) {
        if (fprintf(f, "%s = %s\n", s->e[i].key, s->e[i].value) < 0) {
            rc = -1;
        }
    }
    if (fflush(f) != 0) {
        rc = -1;
    }
    if (fclose(f) != 0) {
        rc = -1;
    }
    if (rc == 0) {
        rc = replace_file(tmp, s->path);
    }
    if (rc != 0) {
        remove(tmp);
    }
    return rc;
}

const char *pset_store_get(void *ctx, const char *key)
{
    return pset_get((const PortSettings *)ctx, key);
}

void pset_store_set(void *ctx, const char *key, const char *value)
{
    pset_set((PortSettings *)ctx, key, value);
}

int pset_store_save(void *ctx)
{
    return pset_save((const PortSettings *)ctx);
}

int pset_user_dir(const char *app, char *out, size_t cap)
{
    char base[900];
    const char *e;
    int n;

    if (!app || !*app || !out || !cap) {
        return -1;
    }
#ifdef _WIN32
    e = getenv("APPDATA");
    if (!e || !*e) {
        return -1;
    }
    n = snprintf(base, sizeof(base), "%s\\%s\\", e, app);
#elif defined(__APPLE__)
    e = getenv("HOME");
    if (!e || !*e) {
        return -1;
    }
    n = snprintf(base, sizeof(base), "%s/Library/Application Support/%s/", e, app);
#else
    e = getenv("XDG_DATA_HOME");
    if (e && *e == '/') {
        n = snprintf(base, sizeof(base), "%s/%s/", e, app);
    } else {
        e = getenv("HOME");
        if (!e || !*e) {
            return -1;
        }
        n = snprintf(base, sizeof(base), "%s/.local/share/%s/", e, app);
    }
#endif
    if (n < 0 || (size_t)n >= sizeof(base) || (size_t)n >= cap) {
        return -1;
    }
    if (make_dirs(base) != 0) {
        return -1;
    }
    memcpy(out, base, (size_t)n + 1);
    return 0;
}
