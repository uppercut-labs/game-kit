/* A tiny assert for the hud-path-font tests, and the recorder they share: every ink call in order, an FNV-1a digest
 * of that sequence, and the atlas it builds (one byte per texel, the largest alpha). */
#ifndef HUD_PATH_FONT_TEST_CHECK_H
#define HUD_PATH_FONT_TEST_CHECK_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int check_failures;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
            check_failures++; \
        } \
    } while (0)

#define TEST_RESULT(name) \
    (check_failures ? (fprintf(stderr, "%s: %d failure(s)\n", (name), check_failures), 1) \
                    : (printf("%s: passed\n", (name)), 0))

enum { RECORD_LIMIT = 65536 };

typedef struct {
    uint16_t offset;
    uint8_t alpha;
} InkCall;

typedef struct {
    uint32_t count;
    uint8_t overflow;
    uint64_t digest;
    InkCall calls[RECORD_LIMIT];
    uint8_t atlas[256 * 128];
} Recording;

static uint64_t fnv1a(uint64_t hash, const void *bytes, size_t size)
{
    const uint8_t *p = (const uint8_t *)bytes;
    for (size_t i = 0; i < size; ++i) {
        hash ^= p[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

#define FNV_START 0xcbf29ce484222325ULL

static void recording_reset(Recording *r)
{
    memset(r, 0, sizeof *r);
    r->digest = FNV_START;
}

/* The ink callbacks take no user pointer, so each test points this at the recording to fill. */
static Recording *recording_target;

static void record_ink(uint16_t offset, uint8_t alpha)
{
    Recording *r = recording_target;
    uint8_t bytes[3] = {(uint8_t)(offset & 255u), (uint8_t)(offset >> 8), alpha};
    r->digest = fnv1a(r->digest, bytes, sizeof bytes);
    if (r->count < RECORD_LIMIT)
        r->calls[r->count] = (InkCall){offset, alpha};
    else
        r->overflow = 1;
    ++r->count;
    if (offset < sizeof r->atlas && alpha > r->atlas[offset])
        r->atlas[offset] = alpha;
}

#endif
