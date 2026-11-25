/* The memory probe against a fake memory map: addresses and masks, sizes and byte order, the tick guard, stop rules,
 * expectations and the text forms. No core. */
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "lgp/lgp.h"

static void put32(uint8_t *m, unsigned at, uint32_t v)
{
    m[at] = (uint8_t)v;
    m[at + 1] = (uint8_t)(v >> 8);
    m[at + 2] = (uint8_t)(v >> 16);
    m[at + 3] = (uint8_t)(v >> 24);
}

static FILE *scratch(void)
{
    FILE *f = tmpfile();
    if (!f) {
        exit(2);
    }
    return f;
}

static int log_has(FILE *f, const char *text)
{
    char buf[4096];
    size_t n;
    rewind(f);
    n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    return strstr(buf, text) != NULL;
}

static void test_reads(void)
{
    uint8_t mem[64];
    uint32_t v = 0;
    unsigned i;
    for (i = 0; i < sizeof(mem); ++i) {
        mem[i] = (uint8_t)(0x10 + i);
    }
    CHECK(lgp_memory_read(mem, sizeof(mem), 4, 0xffffffffu, 1, 0, &v) && v == 0x14, "a byte");
    CHECK(lgp_memory_read(mem, sizeof(mem), 4, 0xffffffffu, 2, 0, &v) && v == 0x1514, "a little-endian short");
    CHECK(lgp_memory_read(mem, sizeof(mem), 4, 0xffffffffu, 4, 0, &v) && v == 0x17161514u, "a little-endian word");
    CHECK(lgp_memory_read(mem, sizeof(mem), 4, 0xffffffffu, 4, 1, &v) && v == 0x14151617u, "a big-endian word");
    CHECK(lgp_memory_read(mem, sizeof(mem), 4, 0xffffffffu, 2, 1, &v) && v == 0x1415, "a big-endian short");
    CHECK(lgp_memory_read(mem, sizeof(mem), 0x8c000004u, 0x00ffffffu, 4, 0, &v) && v == 0x17161514u,
          "a mirrored console address and the mask");
    CHECK(lgp_memory_read(mem, sizeof(mem), 0xac000004u, 0x00ffffffu, 1, 0, &v) && v == 0x14, "the other mirror");
    CHECK(lgp_memory_read(mem, sizeof(mem), 60, 0xffffffffu, 4, 0, &v), "the last word fits");
    CHECK(!lgp_memory_read(mem, sizeof(mem), 61, 0xffffffffu, 4, 0, &v), "one byte over does not");
    CHECK(!lgp_memory_read(mem, sizeof(mem), 64, 0xffffffffu, 1, 0, &v), "an offset at the end");
    CHECK(!lgp_memory_read(mem, sizeof(mem), 0xffffffffu, 0xffffffffu, 4, 0, &v), "a huge offset");
    CHECK(!lgp_memory_read(mem, sizeof(mem), 0, 0xffffffffu, 3, 0, &v), "a width of 3");
    CHECK(!lgp_memory_read(NULL, 0, 0, 0xffffffffu, 1, 0, &v), "no memory");
}

static void test_sampling(void)
{
    uint8_t mem[256];
    LgpProbe *p = lgp_probe_new();
    LgpSample s;
    FILE *log = scratch();

    memset(mem, 0, sizeof(mem));
    lgp_probe_set_mask(p, 0x00ffffffu);
    CHECK(lgp_probe_tick(p, 0x8c000010u, 4, 0), "tick");
    CHECK(lgp_probe_watch(p, "screen", 0x8c000014u, 4, 0), "watch screen");
    CHECK(lgp_probe_watch(p, "flag", 0x8c000018u, 1, 0), "watch flag");
    CHECK(!lgp_probe_watch(p, "screen", 0x8c000014u, 4, 0), "a name is used once");
    CHECK(!lgp_probe_watch(p, "bad", 0, 3, 0) && !lgp_probe_watch(p, "", 0, 4, 0), "bad watches");
    CHECK(lgp_probe_expect(p, "screen", 3) && lgp_probe_expect(p, "flag", 1), "expectations");
    CHECK(!lgp_probe_expect(p, "nothing", 1), "an expectation needs a watch");
    CHECK(lgp_probe_stop(p, "screen", 3, 450), "a stop rule");
    CHECK(lgp_probe_has_tick(p), "has a tick");

    /* the guest has not started: tick 0 means skip (the frame==0 guard) */
    s = lgp_probe_sample(p, mem, sizeof(mem), 0, log);
    CHECK(!s.ready && !s.stop, "tick 0 is not ready");
    put32(mem, 0x14, 99);
    s = lgp_probe_sample(p, mem, sizeof(mem), 1, log);
    CHECK(!s.ready, "a value change while not ready is ignored");
    CHECK(!log_has(log, "WATCH"), "nothing was logged while not ready");

    put32(mem, 0x10, 1);
    put32(mem, 0x14, 1);
    s = lgp_probe_sample(p, mem, sizeof(mem), 2, log);
    CHECK(s.ready && s.tick == 1 && !s.stop, "started");
    CHECK(log_has(log, "WATCH screen host_frame=2 tick=1 value=0x1"), "the first value is logged");
    CHECK(log_has(log, "WATCH flag host_frame=2 tick=1 value=0x0"), "every watch is logged once at first");

    put32(mem, 0x10, 500);
    put32(mem, 0x14, 3);
    s = lgp_probe_sample(p, mem, sizeof(mem), 3, log);
    CHECK(s.ready && s.tick == 500 && s.stop, "the stop rule: value 3 and tick 500 >= 450");
    CHECK(!lgp_probe_report(p, NULL), "the flag expectation is not met yet");
    mem[0x18] = 1;
    lgp_probe_sample(p, mem, sizeof(mem), 4, log);
    CHECK(lgp_probe_report(p, NULL), "both met");

    /* a stop rule waits for its tick */
    put32(mem, 0x10, 100);
    s = lgp_probe_sample(p, mem, sizeof(mem), 5, log);
    CHECK(!s.stop, "tick 100 is under 450");

    /* memory that is not there */
    s = lgp_probe_sample(p, NULL, 0, 6, log);
    CHECK(!s.ready, "no memory is not ready");
    CHECK(log_has(log, "GUEST MEMORY unavailable"), "and says so");
    s = lgp_probe_sample(p, mem, 8, 7, log);
    CHECK(!s.ready, "memory too small for the tick");
    lgp_probe_free(p);
    fclose(log);
}

static void test_without_a_tick(void)
{
    uint8_t mem[16] = {0};
    LgpProbe *p = lgp_probe_new();
    LgpSample s;

    lgp_probe_watch(p, "v", 0, 2, 1);
    lgp_probe_expect(p, "v", 0x0102);
    mem[0] = 1;
    mem[1] = 2;
    s = lgp_probe_sample(p, mem, sizeof(mem), 42, NULL);
    CHECK(s.ready && s.tick == 42, "with no tick the tick is the host frame");
    CHECK(lgp_probe_report(p, NULL), "a big-endian short is read");
    CHECK(!lgp_probe_has_tick(p), "no tick");
    s = lgp_probe_sample(p, NULL, 0, 43, NULL);
    CHECK(s.ready, "with no tick missing memory only skips the watches");
    lgp_probe_free(p);

    p = lgp_probe_new();
    CHECK(lgp_probe_report(p, NULL), "no expectations is a pass");
    s = lgp_probe_sample(NULL, mem, sizeof(mem), 5, NULL);
    CHECK(s.ready && s.tick == 5, "no probe at all");
    lgp_probe_free(p);
}

static void test_text_forms(void)
{
    LgpProbe *p = lgp_probe_new();
    uint8_t mem[64] = {0};
    LgpSample s;

    CHECK(lgp_probe_parse_watch(p, "game=0x8c0000e0:4"), "name=ADDR:SIZE");
    CHECK(lgp_probe_parse_watch(p, "short=16:2be"), "a big-endian size");
    CHECK(lgp_probe_parse_watch(p, "byte=32:1") && lgp_probe_parse_watch(p, "plain=0x28"), "decimal and default size");
    CHECK(!lgp_probe_parse_watch(p, "x=1:3") && !lgp_probe_parse_watch(p, "x") && !lgp_probe_parse_watch(p, "=4") &&
              !lgp_probe_parse_watch(p, "x=zz") && !lgp_probe_parse_watch(p, "x=1:") && !lgp_probe_parse_watch(p, "x=-1") &&
              !lgp_probe_parse_watch(p, "x=0x100000000") && !lgp_probe_parse_watch(p, "game=4"),
          "bad watches (a size of 3, no address, a negative or too big address, a name used twice)");
    CHECK(lgp_probe_parse_tick(p, "0x30:4") && lgp_probe_parse_tick(p, "0x30") && !lgp_probe_parse_tick(p, "") &&
              !lgp_probe_parse_tick(p, "0x30:9"),
          "tick forms");
    CHECK(lgp_probe_parse_expect(p, "byte=7") && !lgp_probe_parse_expect(p, "missing=7") && !lgp_probe_parse_expect(p, "byte") &&
              !lgp_probe_parse_expect(p, "byte=x"),
          "expect forms");
    CHECK(lgp_probe_parse_stop(p, "byte=7@100") && lgp_probe_parse_stop(p, "byte=7") && !lgp_probe_parse_stop(p, "byte=7@") &&
              !lgp_probe_parse_stop(p, "byte=7@x") && !lgp_probe_parse_stop(p, "missing=7"),
          "stop forms");

    mem[0x30] = 10;
    mem[32] = 7;
    s = lgp_probe_sample(p, mem, sizeof(mem), 0, NULL);
    CHECK(s.ready && s.tick == 10, "tick read through its text form");
    CHECK(s.stop, "stop byte=7 with no tick bound");
    CHECK(lgp_probe_report(p, NULL), "expect byte=7 met");
    lgp_probe_free(p);
}

int main(void)
{
    test_reads();
    test_sampling();
    test_without_a_tick();
    test_text_forms();
    printf("probe_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
