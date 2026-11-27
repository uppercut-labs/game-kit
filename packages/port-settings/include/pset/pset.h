/* port-settings: a plain "key = value" settings file for a game port.
 *
 *   # comments
 *   window = windowed
 *   p1.x = key:Z, pad:south
 *
 * One "key = value" per line. A '#' starts a comment anywhere on a line. Keys keep the order they were read in; set
 * adds a new key at the end; save writes every key back through a temporary file, so a hand-edited file keeps its
 * order (its comments are not kept: the file is the port's, the header line says so). No dependencies beyond the C
 * library; the file is read and written whole, so a line of any length works, and a UTF-8 path works on Windows.
 */
#ifndef PSET_H
#define PSET_H

#include <stddef.h>

#define PSET_VERSION "1.0.0"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortSettings PortSettings;

/* Everything is optional. */
typedef struct {
    const char *name;    /* prefix of the warnings on stderr ("myport"); default "settings" */
    const char *header;  /* the comment line save writes first, without the '#'; default none */
    /* where warnings go instead of stderr (one line, no newline) */
    void (*log)(void *user, const char *line);
    void *log_user;
} PsetOptions;

/* An empty settings object bound to path; the file is read if it exists. A line that is not "key = value" is
   reported and dropped. NULL only when out of memory. opt may be NULL. */
PortSettings *pset_open(const char *path, const PsetOptions *opt);
PortSettings *pset_load(const char *path);   /* pset_open(path, NULL) */
void pset_free(PortSettings *s);

/* NULL if the key is not set. */
const char *pset_get(const PortSettings *s, const char *key);
const char *pset_get_or(const PortSettings *s, const char *key, const char *fallback);
/* A whole decimal number, or the fallback (also for an empty or half-numeric value). */
int pset_get_int(const PortSettings *s, const char *key, int fallback);
/* The number rounded to the nearest multiple of step and held between lo and hi: for a slider a person sets. */
int pset_get_int_step(const PortSettings *s, const char *key, int fallback, int lo, int hi, int step);
/* on/off, true/false, yes/no, 1/0 in any case; the fallback for anything else. */
int pset_get_bool(const PortSettings *s, const char *key, int fallback);
/* The index of the value among names (an exact match), or the fallback. */
int pset_get_choice(const PortSettings *s, const char *key, const char *const *names, int n, int fallback);

/* 0 on success, -1 when out of memory or when the text could not be read back: an empty key, a key with '=' or '#',
   and a value with '#' or a line break are refused and nothing is stored. */
int pset_set(PortSettings *s, const char *key, const char *value);
int pset_set_int(PortSettings *s, const char *key, int value);
int pset_set_bool(PortSettings *s, const char *key, int on);    /* "on" or "off" */
/* Removes a key; 1 if it was there. */
int pset_remove(PortSettings *s, const char *key);

size_t pset_count(const PortSettings *s);
const char *pset_key_at(const PortSettings *s, size_t i);      /* in file order */
const char *pset_value_at(const PortSettings *s, size_t i);

/* Writes the file, creating its folder, through "<path>.part" and a replace. 0 on success. */
int pset_save(const PortSettings *s);
const char *pset_path(const PortSettings *s);

/* The three functions an input layer's store takes (port-sdl-input's PsiStore): pass the PortSettings as ctx. */
const char *pset_store_get(void *ctx, const char *key);
void pset_store_set(void *ctx, const char *key, const char *value);
int pset_store_save(void *ctx);

/* The per-user folder for app, with a trailing separator, created if missing: %APPDATA%\<app>\ on Windows,
   ~/Library/Application Support/<app>/ on macOS, $XDG_DATA_HOME/<app>/ (else ~/.local/share/<app>/) elsewhere.
   0 on success, -1 when the base folder is unknown or out does not fit. (A mobile app's own folder is the port's.) */
int pset_user_dir(const char *app, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
