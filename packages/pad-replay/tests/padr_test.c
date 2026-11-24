/* padr_test: the reader, the player, the line writer, the button names and the recorder, with made-up files in a
 * scratch folder. No display, no hardware, no game data.
 *
 *   padr_test <scratch dir>
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "padr/padr.h"

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

static char scratch[1024];

static const char *path_of(const char *name)
{
    static char path[1200];
    snprintf(path, sizeof(path), "%s/%s", scratch, name);
    return path;
}

static void write_bytes(const char *name, const char *bytes, size_t length)
{
    FILE *f = fopen(path_of(name), "wb");
    if (!f || fwrite(bytes, 1, length, f) != length || fclose(f) != 0) {
        fprintf(stderr, "cannot write %s\n", path_of(name));
        exit(2);
    }
}

static void write_text(const char *name, const char *text)
{
    write_bytes(name, text, strlen(text));
}

static int same(PadrSample a, PadrSample b)
{
    return a.buttons == b.buttons && a.lx == b.lx && a.ly == b.ly && a.connected == b.connected;
}

static PadrSample mk(unsigned buttons, unsigned lx, unsigned ly, unsigned connected)
{
    PadrSample s;
    s.buttons = (uint16_t)buttons;
    s.lx = (uint8_t)lx;
    s.ly = (uint8_t)ly;
    s.connected = (uint8_t)connected;
    return s;
}

/* ---- the reader and the player --------------------------------------------------------------------------------- */

static void test_playback(void)
{
    PadrError err;
    PadrScript *s;
    PadrSample out[8];
    unsigned slot;
    const PadrSample neutral = mk(0, 128, 128, 0);

    write_text("play.input", "  # first frame includes both physical ports\r\n"
                             "\n"
                             "0 0 0008 0 255 1\r\n"
                             "0 7 0x4000 240 30 1\n"
                             "3 0 0000 128 128 1\n"
                             "5 7 4000 12 245 0\n"
                             "6 4 0080 128 128 1");
    s = padr_load_file(path_of("play.input"), NULL, &err);
    CHECK(s != NULL && err.status == PADR_OK, "a valid file loads");
    if (!s) {
        return;
    }
    CHECK(padr_event_count(s) == 5, "event count is %u", padr_event_count(s));
    CHECK(padr_slots(s) == 8, "default slots is 8");
    CHECK(padr_last_frame(s) == 6, "last frame is %u", padr_last_frame(s));

    padr_sample(s, 0, out, 8);
    CHECK(same(out[0], mk(0x0008, 0, 255, 1)), "slot 0 at frame 0");
    CHECK(same(out[7], mk(0x4000, 240, 30, 1)), "slot 7 at frame 0 (0x prefix accepted)");
    for (slot = 1; slot < 7; ++slot) {
        CHECK(same(out[slot], neutral), "untouched slot %u is neutral and absent", slot);
    }
    padr_sample(s, 2, out, 8);
    CHECK(same(out[0], mk(0x0008, 0, 255, 1)), "a frame with no event keeps the state");
    padr_sample(s, 3, out, 8);
    CHECK(same(out[0], mk(0, 128, 128, 1)), "one slot is released at frame 3");
    CHECK(same(out[7], mk(0x4000, 240, 30, 1)), "the other is not");
    padr_sample(s, 6, out, 8);
    CHECK(same(out[7], neutral), "a disconnect line gives a neutral absent pad whatever its other fields say");
    CHECK(same(out[4], mk(0x0080, 128, 128, 1)), "a later slot connects");

    /* a frame that goes down keeps what was reached */
    padr_sample(s, 1, out, 8);
    CHECK(same(out[4], mk(0x0080, 128, 128, 1)), "a lower frame does not undo events");
    padr_rewind(s);
    padr_sample(s, 0, out, 8);
    CHECK(same(out[4], neutral) && same(out[0], mk(0x0008, 0, 255, 1)), "rewind starts again");

    /* the caller asks for fewer or more slots than the script has */
    padr_sample(s, 100, out, 2);
    CHECK(same(out[0], mk(0, 128, 128, 1)), "two slots out of eight");
    {
        PadrSample wide[12];
        padr_sample(s, 100, wide, 12);
        CHECK(same(wide[4], mk(0x0080, 128, 128, 1)) && same(wide[11], neutral), "slots past the script's are absent");
    }
    padr_free(s);

    /* no script: every slot absent */
    padr_sample(NULL, 5, out, 8);
    CHECK(same(out[0], neutral) && same(out[7], neutral), "a NULL script gives absent pads");
    CHECK(padr_slots(NULL) == 0 && padr_event_count(NULL) == 0 && padr_last_frame(NULL) == 0, "NULL queries");
    padr_free(NULL);
}

static void test_rejections(void)
{
    static const struct {
        const char *label;
        const char *contents;
        PadrStatus status;
    } invalid[] = {
        {"zero events", "# nothing to apply\n\n", PADR_ERR_EMPTY},
        {"missing field", "0 0 0000 128 128\n", PADR_ERR_FIELD},
        {"out of order", "1 0 0008 128 128 1\n0 7 4000 128 128 1\n", PADR_ERR_ORDER},
        {"unknown trailing token", "0 0 0000 128 128 1 extra\n", PADR_ERR_TRAILING},
        {"trailing comment", "0 0 0000 128 128 1 # note\n", PADR_ERR_TRAILING},
        {"slot out of range", "0 8 0000 128 128 1\n", PADR_ERR_FIELD},
        {"button mask out of range", "0 0 10000 128 128 1\n", PADR_ERR_FIELD},
        {"axis out of range", "0 0 0000 256 128 1\n", PADR_ERR_FIELD},
        {"connection flag out of range", "0 0 0000 128 128 2\n", PADR_ERR_FIELD},
        {"negative frame", "-1 0 0000 128 128 1\n", PADR_ERR_FIELD},
        {"explicit plus sign", "+0 0 0000 128 128 1\n", PADR_ERR_FIELD},
        {"frame overflow", "4294967296 0 0000 128 128 1\n", PADR_ERR_FIELD},
        {"button mask overflow", "0 0 100000000 128 128 1\n", PADR_ERR_FIELD},
        {"digits glued to letters", "0 0 0000 128abc 128 1\n", PADR_ERR_FIELD},
        {"hex prefix without digits", "0 0 0x 128 128 1\n", PADR_ERR_FIELD},
    };
    unsigned i;
    PadrError err;

    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        write_text("bad.input", invalid[i].contents);
        err.status = PADR_OK;
        CHECK(padr_load_file(path_of("bad.input"), NULL, &err) == NULL, "%s is rejected", invalid[i].label);
        CHECK(err.status == invalid[i].status, "%s: status %s, wanted %s", invalid[i].label,
              padr_status_text(err.status), padr_status_text(invalid[i].status));
    }

    /* the line number of the problem */
    write_text("bad.input", "0 0 0008 0 255 1\n# c\ninvalid\n");
    CHECK(padr_load_file(path_of("bad.input"), NULL, &err) == NULL && err.status == PADR_ERR_FIELD && err.line == 3,
          "a valid prefix does not save a bad third line (line %u)", err.line);

    /* an embedded NUL: in a file and in memory */
    write_bytes("nul.input", "0 0 0008 0 255 1\0junk\n", 22);
    CHECK(padr_load_file(path_of("nul.input"), NULL, &err) == NULL && err.status == PADR_ERR_NUL && err.line == 1,
          "a NUL byte in a file is rejected (%s)", padr_status_text(err.status));
    CHECK(padr_load_text("0 0 0008 0 255 1\0junk\n", 22, NULL, &err) == NULL && err.status == PADR_ERR_NUL,
          "a NUL byte in text is rejected too");
    {
        /* a C string stops at its NUL, so the length decides, not strlen */
        PadrScript *s = padr_load_text("0 0 0008 0 255 1\n\0", 17, NULL, &err);
        CHECK(s != NULL, "length 17 stops before the NUL");
        padr_free(s);
    }

    /* line length: 255 bytes is fine, 256 is not (file and text) */
    {
        char line[400];
        PadrScript *s;
        memset(line, ' ', sizeof(line));
        memcpy(line, "0 0 0000 128 128 1", 18);
        line[255] = '\n';
        line[256] = '\0';
        write_text("long.input", line);
        s = padr_load_file(path_of("long.input"), NULL, &err);
        CHECK(s != NULL, "a 255 byte line loads (%s)", padr_status_text(err.status));
        padr_free(s);
        s = padr_load_text(line, 256, NULL, &err);
        CHECK(s != NULL, "a 255 byte line loads from text");
        padr_free(s);
        line[255] = ' ';
        line[256] = '\n';
        line[257] = '\0';
        write_text("long.input", line);
        CHECK(padr_load_file(path_of("long.input"), NULL, &err) == NULL && err.status == PADR_ERR_LINE_LONG,
              "a 256 byte line is rejected");
        CHECK(padr_load_text(line, 257, NULL, &err) == NULL && err.status == PADR_ERR_LINE_LONG,
              "a 256 byte line is rejected from text");
    }

    CHECK(padr_load_file(path_of("missing.input"), NULL, &err) == NULL && err.status == PADR_ERR_OPEN, "missing file");
    CHECK(padr_load_file(NULL, NULL, &err) == NULL && err.status == PADR_ERR_ARGUMENT, "NULL path");
    CHECK(padr_load_text(NULL, 0, NULL, NULL) == NULL, "NULL text and NULL error are fine");
    CHECK(padr_load_text("", 0, NULL, &err) == NULL && err.status == PADR_ERR_EMPTY, "empty text");
}

static char *many_events(unsigned count, unsigned slots, size_t *length)
{
    char *text = (char *)malloc((size_t)count * 24u + 1u);
    size_t used = 0;
    unsigned i;
    if (!text) {
        exit(2);
    }
    for (i = 0; i < count; ++i) {
        used += (size_t)sprintf(text + used, "%u %u 0001 128 128 1\n", i, i % slots);
    }
    *length = used;
    return text;
}

static void test_event_cap_and_slots(void)
{
    PadrError err;
    PadrScript *s;
    PadrOptions opt;
    size_t length;
    char *text;

    /* the cap is configurable: the labs ran at 4096, uphill at 32768 */
    memset(&opt, 0, sizeof(opt));
    opt.max_events = 4096;
    text = many_events(4096, 8, &length);
    s = padr_load_text(text, length, &opt, &err);
    CHECK(s != NULL && padr_event_count(s) == 4096, "4096 events at a cap of 4096");
    padr_free(s);
    free(text);
    text = many_events(4097, 8, &length);
    s = padr_load_text(text, length, &opt, &err);
    CHECK(s == NULL && err.status == PADR_ERR_TOO_MANY && err.line == 4097, "4097 events at a cap of 4096 (line %u)",
          err.line);
    padr_free(s);
    s = padr_load_text(text, length, NULL, &err);
    CHECK(s != NULL && padr_event_count(s) == 4097, "the default cap takes 4097");
    padr_free(s);
    free(text);

    text = many_events(PADR_DEFAULT_MAX_EVENTS, 8, &length);
    s = padr_load_text(text, length, NULL, &err);
    CHECK(s != NULL && padr_event_count(s) == PADR_DEFAULT_MAX_EVENTS, "the default cap is %u", PADR_DEFAULT_MAX_EVENTS);
    padr_free(s);
    free(text);
    text = many_events(PADR_DEFAULT_MAX_EVENTS + 1, 8, &length);
    s = padr_load_text(text, length, NULL, &err);
    CHECK(s == NULL && err.status == PADR_ERR_TOO_MANY, "one over the default cap");
    free(text);

    opt.max_events = 3;
    {
        static const char three[] = "0 0 0001 128 128 1\n1 0 0001 128 128 1\n2 0 0001 128 128 1\n";
        PadrScript *t3 = padr_load_text(three, sizeof(three) - 1, &opt, &err);
        CHECK(t3 != NULL && padr_event_count(t3) == 3, "three at a cap of three");
        padr_free(t3);
    }

    /* slots */
    memset(&opt, 0, sizeof(opt));
    opt.slots = 2;
    CHECK(padr_load_text("0 2 0000 128 128 1\n", 19, &opt, &err) == NULL && err.status == PADR_ERR_FIELD,
          "slot 2 with two slots");
    s = padr_load_text("0 1 0001 128 128 1\n", 19, &opt, &err);
    CHECK(s != NULL && padr_slots(s) == 2, "slot 1 with two slots");
    padr_free(s);
    opt.slots = 16;
    s = padr_load_text("0 15 0001 128 128 1\n", 20, &opt, &err);
    CHECK(s != NULL && padr_slots(s) == 16, "slot 15 with sixteen slots");
    if (s) {
        PadrSample out[16];
        padr_sample(s, 0, out, 16);
        CHECK(out[15].buttons == 1 && out[15].connected == PADR_FRESH, "slot 15 plays");
    }
    padr_free(s);
    opt.slots = PADR_MAX_SLOTS + 1;
    CHECK(padr_load_text("0 0 0000 128 128 1\n", 19, &opt, &err) == NULL && err.status == PADR_ERR_ARGUMENT,
          "too many slots");
}

static void test_format_and_buttons(void)
{
    char buf[64];
    size_t n;
    uint16_t mask = 0xffff;
    static const PadrButton table[] = {{"Select", 0x0001}, {"Start", 0x0008}, {"Left", 0x0080}, {"Cross", 0x4000},
                                       {"Square", 0x8000}};
    const size_t count = sizeof(table) / sizeof(table[0]);

    n = padr_format_event(buf, sizeof(buf), 12, 3, &(PadrSample){0x4080, 33, 201, PADR_FRESH});
    CHECK(n == strlen("12 3 4080 33 201 1\n") && strcmp(buf, "12 3 4080 33 201 1\n") == 0, "a fresh sample: %s", buf);
    n = padr_format_event(buf, sizeof(buf), 7, 0, &(PadrSample){0x00ff, 1, 2, PADR_ABSENT});
    CHECK(strcmp(buf, "7 0 0000 128 128 0\n") == 0, "an absent sample is written at rest: %s", buf);
    n = padr_format_event(buf, sizeof(buf), 7, 0, NULL);
    CHECK(n > 0 && strcmp(buf, "7 0 0000 128 128 0\n") == 0, "NULL is an absent pad");
    CHECK(padr_format_event(buf, 8, 1, 1, NULL) == 0, "a buffer too small gives 0");

    CHECK(padr_buttons_parse(table, count, "Left+Cross", &mask) && mask == 0x4080, "Left+Cross");
    CHECK(padr_buttons_parse(table, count, "  circle-less ", &mask) == 0, "an unknown name");
    CHECK(padr_buttons_parse(table, count, "start + square", &mask) && mask == 0x8008, "case and spaces");
    CHECK(padr_buttons_parse(table, count, "none", &mask) && mask == 0, "none");
    CHECK(padr_buttons_parse(table, count, "0x0100+Start", &mask) && mask == 0x0108, "a hex literal");
    CHECK(padr_buttons_parse(table, count, "Start+", &mask) == 0, "a dangling plus");
    CHECK(padr_buttons_parse(table, count, "", &mask) == 0, "an empty string");
    CHECK(padr_buttons_parse(NULL, 0, "Start", &mask) == 0, "no table, no names");

    n = padr_buttons_format(table, count, 0x4080, buf, sizeof(buf));
    CHECK(strcmp(buf, "Left+Cross") == 0 && n == 10, "format Left+Cross: %s", buf);
    padr_buttons_format(table, count, 0, buf, sizeof(buf));
    CHECK(strcmp(buf, "none") == 0, "format none");
    padr_buttons_format(table, count, 0x0108, buf, sizeof(buf));
    CHECK(strcmp(buf, "Start+0x100") == 0, "unnamed bits in hex: %s", buf);
    CHECK(padr_buttons_format(table, count, 0x4080, buf, 5) == 0, "format into a buffer too small");
}

/* ---- the recorder ---------------------------------------------------------------------------------------------- */

static unsigned count_lines(const char *name)
{
    FILE *f = fopen(path_of(name), "rb");
    unsigned lines = 0;
    int c;
    if (!f) {
        return 0;
    }
    while ((c = fgetc(f)) != EOF) {
        lines += c == '\n';
    }
    fclose(f);
    return lines;
}

static void test_trace_round_trip(void)
{
    PadrTrace *t;
    PadrSample in[2], out[8];
    PadrScript *s;
    PadrError err;
    unsigned slot;

    remove(path_of("rt.input"));
    t = padr_trace_open(path_of("rt.input"), 2, NULL);
    CHECK(t != NULL, "a new trace opens");
    if (!t) {
        return;
    }
    in[0] = mk(0x0008, 128, 128, PADR_FRESH);
    in[1] = mk(0, 128, 128, PADR_ABSENT);
    CHECK(padr_trace_sample(t, 0, in, 2), "frame 0");
    in[0].connected = PADR_UNREADABLE;
    CHECK(padr_trace_sample(t, 1, in, 2), "an unreadable pad is not an error");
    in[0] = mk(0x4000, 33, 201, PADR_FRESH);
    CHECK(padr_trace_sample(t, 2, in, 2), "frame 2");
    CHECK(padr_trace_sample(t, 60, in, 2), "frame 60, nothing changed");
    CHECK(padr_trace_records(t) == 3, "three lines: two first sightings and one change, got %u", padr_trace_records(t));
    CHECK(padr_trace_close(t), "close");
    CHECK(count_lines("rt.input") == 3, "no line is written for a pad left alone (frame 60) or an unreadable one");

    {
        PadrTrace *again;
        errno = 0;
        again = padr_trace_open(path_of("rt.input"), 2, NULL);
        CHECK(again == NULL && errno == EEXIST, "exclusive create: an earlier trace is never overwritten");
        padr_trace_close(again);
    }

    s = padr_load_file(path_of("rt.input"), NULL, &err);
    CHECK(s != NULL, "the trace reads back (%s)", padr_status_text(err.status));
    if (s) {
        padr_sample(s, 0, out, 8);
        CHECK(same(out[0], mk(0x0008, 128, 128, 1)), "frame 0 replays");
        padr_sample(s, 1, out, 8);
        CHECK(out[0].buttons == 0x0008, "the unreadable frame kept the state");
        padr_sample(s, 2, out, 8);
        CHECK(same(out[0], mk(0x4000, 33, 201, 1)), "frame 2 replays");
        padr_sample(s, 60, out, 8);
        CHECK(same(out[0], mk(0x4000, 33, 201, 1)) && out[1].connected == PADR_ABSENT, "frame 60 replays");
        for (slot = 2; slot < 8; ++slot) {
            CHECK(out[slot].connected == PADR_ABSENT, "slot %u was never sampled", slot);
        }
    }
    padr_free(s);
}

static void test_trace_empty_and_errors(void)
{
    PadrTrace *t;
    PadrError err;
    FILE *f;
    PadrSample in[3];

    remove(path_of("empty.input"));
    t = padr_trace_open(path_of("empty.input"), 2, NULL);
    CHECK(t && padr_trace_close(t), "open and close with no sample");
    f = fopen(path_of("empty.input"), "rb");
    CHECK(f && fgetc(f) == EOF, "an empty trace is an empty file");
    if (f) {
        fclose(f);
    }
    CHECK(padr_load_file(path_of("empty.input"), NULL, &err) == NULL && err.status == PADR_ERR_EMPTY,
          "an empty trace does not load as a replay");

    remove(path_of("over.input"));
    t = padr_trace_open(path_of("over.input"), 2, NULL);
    memset(in, 0, sizeof(in));
    CHECK(t && !padr_trace_sample(t, 0, in, 3) && errno == EINVAL, "more samples than slots");
    CHECK(!padr_trace_sample(t, 1, in, 1), "a failed trace stays failed");
    CHECK(!padr_trace_close(t), "and close says so");

    errno = 0;
    CHECK(padr_trace_open(path_of("x.input"), 0, NULL) == NULL && errno == EINVAL, "no slots");
    CHECK(padr_trace_open(path_of("x.input"), PADR_MAX_SLOTS + 1, NULL) == NULL, "too many slots");
    CHECK(padr_trace_open(NULL, 2, NULL) == NULL, "no path");
    CHECK(padr_trace_close(NULL) == 1, "closing NULL is fine");
    CHECK(padr_trace_records(NULL) == 0, "records of NULL");
}

/* The labs' recorder wrote a line per slot per second even when nothing changed, so a long session with eight pads
 * passed the reader's old cap of 4096 events and could no longer be replayed. */
static void test_trace_idle_session_stays_replayable(void)
{
    PadrTrace *t;
    PadrSample pads[8];
    PadrScript *s;
    PadrOptions opt;
    PadrError err;
    unsigned frame, slot;
    const unsigned frames = 60u * 60u * 10u; /* ten minutes at 60 fps */

    remove(path_of("idle.input"));
    t = padr_trace_open(path_of("idle.input"), 8, NULL);
    CHECK(t != NULL, "open");
    if (!t) {
        return;
    }
    for (slot = 0; slot < 8; ++slot) {
        pads[slot] = mk(0, 128, 128, slot < 4 ? PADR_FRESH : PADR_ABSENT);
    }
    for (frame = 0; frame < frames; ++frame) {
        if (frame == 20000) {
            pads[3].buttons = 0x0008;
        }
        if (frame == 20002) {
            pads[3].buttons = 0;
        }
        if (!padr_trace_sample(t, frame, pads, 8)) {
            CHECK(0, "frame %u failed", frame);
            break;
        }
    }
    CHECK(padr_trace_records(t) == 8 + 2, "an idle ten minutes costs one line per slot plus the changes, got %u",
          padr_trace_records(t));
    CHECK(padr_trace_close(t), "close");
    CHECK(count_lines("idle.input") == 10, "ten lines in the file, got %u", count_lines("idle.input"));

    memset(&opt, 0, sizeof(opt));
    opt.max_events = 4096; /* the labs' old cap */
    s = padr_load_file(path_of("idle.input"), &opt, &err);
    CHECK(s != NULL, "the trace replays under the labs' cap of 4096 (%s)", padr_status_text(err.status));
    if (s) {
        PadrSample out[8];
        padr_sample(s, 20001, out, 8);
        CHECK(out[3].buttons == 0x0008 && out[0].connected == PADR_FRESH && out[5].connected == PADR_ABSENT,
              "and plays what was recorded");
        padr_sample(s, frames, out, 8);
        CHECK(out[3].buttons == 0, "released again");
    }
    padr_free(s);
}

static void test_trace_flush(void)
{
    PadrTraceOptions opt;
    PadrTrace *t;
    PadrSample in = mk(0x0001, 128, 128, PADR_FRESH);
    FILE *f;
    char buf[64];

    opt.flush_every = 5;
    remove(path_of("flush.input"));
    t = padr_trace_open(path_of("flush.input"), 1, &opt);
    CHECK(t != NULL, "open");
    if (!t) {
        return;
    }
    CHECK(padr_trace_sample(t, 3, &in, 1), "frame 3 is not a flush frame");
    f = fopen(path_of("flush.input"), "rb");
    CHECK(f && fread(buf, 1, sizeof(buf), f) == 0, "nothing is on disk yet");
    if (f) {
        fclose(f);
    }
    in.buttons = 0x0002;
    CHECK(padr_trace_sample(t, 5, &in, 1), "frame 5 flushes");
    f = fopen(path_of("flush.input"), "rb");
    CHECK(f && fread(buf, 1, sizeof(buf), f) > 0, "the file is on disk before the close");
    if (f) {
        fclose(f);
    }
    CHECK(padr_trace_close(t), "close");
}

int main(int argc, char **argv)
{
    snprintf(scratch, sizeof(scratch), "%s", argc > 1 ? argv[1] : ".");
    test_playback();
    test_rejections();
    test_event_cap_and_slots();
    test_format_and_buttons();
    test_trace_round_trip();
    test_trace_empty_and_errors();
    test_trace_idle_session_stays_replayable();
    test_trace_flush();
    printf("padr_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
