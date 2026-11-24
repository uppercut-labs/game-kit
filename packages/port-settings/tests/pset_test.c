/* pset_test: port-settings with made-up files in a scratch folder. No SDL, no display.
 *
 *   pset_test [<scratch dir>]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define SEP "\\"
#else
#include <sys/stat.h>
#define SEP "/"
#endif

#include "pset/pset.h"

static int checks, failures;

#define CHECK(cond, ...)                                 \
    do {                                                 \
        checks++;                                        \
        if (!(cond)) {                                   \
            failures++;                                  \
            fprintf(stderr, "FAIL line %d: ", __LINE__); \
            fprintf(stderr, __VA_ARGS__);                \
            fputc('\n', stderr);                         \
        }                                                \
    } while (0)

static char warnings[2048];

static void collect(void *user, const char *line)
{
    (void)user;
    strncat(warnings, line, sizeof(warnings) - strlen(warnings) - 2);
    strcat(warnings, "\n");
}

static void write_file(const char *path, const char *text, size_t len)
{
    FILE *f = fopen(path, "wb");

    if (!f || fwrite(text, 1, len, f) != len) {
        fprintf(stderr, "pset_test: cannot write %s\n", path);
        exit(2);
    }
    fclose(f);
}

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    static char buf[16384];
    size_t n;

    if (!f) {
        return NULL;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    char dir[900], path[1000], deep[1100], text[1100];
    const char *scratch = argc > 1 ? argv[1] : ".";
    PortSettings *s;
    PsetOptions opt;
    static char big[6000];

    snprintf(dir, sizeof(dir), "%s" SEP "pset-test-files", scratch);
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0777);
#endif
    snprintf(path, sizeof(path), "%s" SEP "a.cfg", dir);

    /* a file that does not exist: empty, bound to its path */
    remove(path);
    s = pset_load(path);
    CHECK(s != NULL && pset_count(s) == 0, "a missing file is an empty settings object");
    CHECK(!strcmp(pset_path(s), path), "path kept");
    CHECK(pset_get(s, "x") == NULL && !strcmp(pset_get_or(s, "x", "d"), "d"), "unset key");
    pset_free(s);

    /* reading: comments, spaces, tabs, CRLF, a byte order mark, bad lines, duplicates, a line of any length */
    memset(big, 'v', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    {
        char file[8192];
        int n = snprintf(file, sizeof(file),
                         "\xEF\xBB\xBF# a comment\r\n"
                         "window = windowed   # trailing comment\r\n"
                         "\tp1.x=key:Z, pad:south\t\n"
                         "this line has no equals sign\n"
                         "= no key\n"
                         "   \n"
                         "window = fullscreen\n"
                         "long = %s\n"
                         "last = 1",
                         big);

        write_file(path, file, (size_t)n);
    }
    warnings[0] = 0;
    memset(&opt, 0, sizeof(opt));
    opt.name = "myport";
    opt.log = collect;
    s = pset_open(path, &opt);
    CHECK(s != NULL, "open");
    CHECK(!strcmp(pset_get_or(s, "window", ""), "fullscreen"), "the later duplicate wins: %s", pset_get_or(s, "window", ""));
    CHECK(!strcmp(pset_get_or(s, "p1.x", ""), "key:Z, pad:south"), "spaces and tabs trimmed: [%s]", pset_get_or(s, "p1.x", ""));
    CHECK(pset_get(s, "# a comment") == NULL && pset_get(s, "\xEF\xBB\xBF# a comment") == NULL, "the comment is not a key");
    CHECK(strlen(pset_get_or(s, "long", "")) == sizeof(big) - 1, "a 6000 character value is kept whole: %zu",
          strlen(pset_get_or(s, "long", "")));
    CHECK(!strcmp(pset_get_or(s, "last", ""), "1"), "last line without a newline");
    CHECK(pset_count(s) == 4, "four keys: %zu", pset_count(s));
    CHECK(strstr(warnings, "not \"key = value\"") && strstr(warnings, "no key"), "two warnings: %s", warnings);
    CHECK(strstr(warnings, ":4:") && strstr(warnings, ":5:"), "with their line numbers: %s", warnings);
    pset_free(s);

    /* typed getters */
    s = pset_load(path);
    pset_set(s, "n", "42");
    pset_set(s, "neg", "-7");
    pset_set(s, "half", "4x");
    pset_set(s, "empty", "");
    pset_set(s, "big", "99999999999");
    pset_set(s, "b1", "On");
    pset_set(s, "b2", "FALSE");
    pset_set(s, "b3", "yes");
    pset_set(s, "b4", "0");
    pset_set(s, "b5", "maybe");
    pset_set(s, "size", "73");
    pset_set(s, "mode", "original");
    CHECK(pset_get_int(s, "n", 0) == 42 && pset_get_int(s, "neg", 0) == -7, "ints");
    CHECK(pset_get_int(s, "half", 5) == 5 && pset_get_int(s, "empty", 6) == 6 && pset_get_int(s, "nope", 7) == 7,
          "bad ints fall back");
    CHECK(pset_get_int(s, "big", 8) == 8, "an int that does not fit falls back: %d", pset_get_int(s, "big", 8));
    CHECK(pset_get_bool(s, "b1", 0) == 1 && pset_get_bool(s, "b2", 1) == 0 && pset_get_bool(s, "b3", 0) == 1 &&
              pset_get_bool(s, "b4", 1) == 0,
          "bools");
    CHECK(pset_get_bool(s, "b5", 1) == 1 && pset_get_bool(s, "b5", 0) == 0 && pset_get_bool(s, "nope", 1) == 1,
          "a bad bool falls back");
    CHECK(pset_get_int_step(s, "size", 100, 50, 150, 10) == 70, "73 to the nearest 10: %d", pset_get_int_step(s, "size", 100, 50, 150, 10));
    pset_set(s, "size", "75");
    CHECK(pset_get_int_step(s, "size", 100, 50, 150, 10) == 80, "75 rounds up");
    pset_set(s, "size", "400");
    CHECK(pset_get_int_step(s, "size", 100, 50, 150, 10) == 150, "held at the top");
    pset_set(s, "size", "-400");
    CHECK(pset_get_int_step(s, "size", 100, 50, 150, 10) == 50, "held at the bottom");
    CHECK(pset_get_int_step(s, "nope", 100, 50, 150, 10) == 100, "the fallback is stepped and held too");
    {
        static const char *const NAMES[] = { "enhanced", "original" };

        CHECK(pset_get_choice(s, "mode", NAMES, 2, 0) == 1, "choice");
        CHECK(pset_get_choice(s, "nope", NAMES, 2, 0) == 0, "unset choice");
        pset_set(s, "mode", "Original");
        CHECK(pset_get_choice(s, "mode", NAMES, 2, 0) == 0, "choices are exact");
    }

    /* set refuses text the file could not read back */
    CHECK(pset_set(s, "a", "x # y") == -1 && pset_get(s, "a") == NULL, "a value with # is refused");
    CHECK(pset_set(s, "a", "x\ny") == -1, "a value with a line break is refused");
    CHECK(pset_set(s, "", "x") == -1 && pset_set(s, "a=b", "x") == -1 && pset_set(s, "a#b", "x") == -1, "bad keys refused");
    CHECK(pset_set_int(s, "k.int", -12) == 0 && !strcmp(pset_get_or(s, "k.int", ""), "-12"), "set_int");
    CHECK(pset_set_bool(s, "k.bool", 1) == 0 && !strcmp(pset_get_or(s, "k.bool", ""), "on"), "set_bool");
    CHECK(pset_remove(s, "k.bool") == 1 && pset_get(s, "k.bool") == NULL && pset_remove(s, "k.bool") == 0, "remove");
    CHECK(pset_get(s, "k.int") != NULL, "the neighbour of a removed key stays");
    pset_free(s);

    /* saving: order kept, new keys last, header first, the file reads back the same, no .part left */
    remove(path);
    memset(&opt, 0, sizeof(opt));
    opt.header = "myport settings. The port rewrites this file: values are kept, comments are not.";
    s = pset_open(path, &opt);
    pset_set(s, "zeta", "1");
    pset_set(s, "alpha", "2");
    pset_set(s, "zeta", "3");
    pset_set(s, "mid", "key:Non-US \\, pad:a");
    CHECK(pset_save(s) == 0, "save");
    snprintf(text, sizeof(text), "%s.part", path);
    CHECK(slurp(text) == NULL, "no temporary file left");
    CHECK(!strcmp(slurp(path), "# myport settings. The port rewrites this file: values are kept, comments are not.\n"
                               "zeta = 3\nalpha = 2\nmid = key:Non-US \\, pad:a\n"),
          "the file: %s", slurp(path));
    pset_free(s);
    s = pset_load(path);
    CHECK(pset_count(s) == 3 && !strcmp(pset_key_at(s, 0), "zeta") && !strcmp(pset_key_at(s, 2), "mid") &&
              !strcmp(pset_value_at(s, 1), "2") && pset_key_at(s, 3) == NULL,
          "read back in order");
    /* a second save over an existing file */
    pset_set(s, "alpha", "9");
    CHECK(pset_save(s) == 0 && strstr(slurp(path), "alpha = 9\n") != NULL, "save over an existing file");
    pset_free(s);

    /* saving makes the folders on the way, with a path that has spaces and non-ASCII text */
    snprintf(deep, sizeof(deep), "%s" SEP "new one" SEP "caf\xC3\xA9" SEP "deep" SEP "b.cfg", dir);
    s = pset_load(deep);
    pset_set(s, "k", "v");
    CHECK(pset_save(s) == 0, "save into folders that do not exist yet");
    pset_free(s);
    s = pset_load(deep);
    CHECK(!strcmp(pset_get_or(s, "k", ""), "v"), "and read back from there");
    pset_free(s);

    /* a path that cannot be written */
    snprintf(deep, sizeof(deep), "%s" SEP "a.cfg" SEP "inside.cfg", dir);   /* a.cfg is a file */
    s = pset_load(deep);
    pset_set(s, "k", "v");
    CHECK(pset_save(s) != 0, "save under a file fails, not crashes");
    pset_free(s);

    /* the store functions */
    s = pset_load(path);
    CHECK(!strcmp(pset_store_get(s, "zeta"), "3"), "store get");
    pset_store_set(s, "fresh", "yes");
    CHECK(pset_store_save(s) == 0 && strstr(slurp(path), "fresh = yes") != NULL, "store set and save");
    pset_free(s);

    /* the per-user folder */
    {
        char out[1000], tiny[4];
#if defined(_WIN32) || defined(__APPLE__)
        char env[1000];
#endif

#ifdef _WIN32
        snprintf(env, sizeof(env), "APPDATA=%s", dir);
        _putenv(env);
#elif defined(__APPLE__)
        snprintf(env, sizeof(env), "%s", dir);
        setenv("HOME", env, 1);
#else
        setenv("XDG_DATA_HOME", dir, 1);
#endif
        CHECK(pset_user_dir("my app", out, sizeof(out)) == 0, "user dir");
        CHECK(strstr(out, "my app") != NULL && (out[strlen(out) - 1] == '/' || out[strlen(out) - 1] == '\\'),
              "ends with a separator: %s", out);
        snprintf(deep, sizeof(deep), "%smarker.cfg", out);
        write_file(deep, "x", 1);
        CHECK(slurp(deep) != NULL, "the folder exists");
        CHECK(pset_user_dir("my app", tiny, sizeof(tiny)) == -1, "a small buffer is refused");
        CHECK(pset_user_dir("", out, sizeof(out)) == -1, "no app name");
    }

    printf("pset_test: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
