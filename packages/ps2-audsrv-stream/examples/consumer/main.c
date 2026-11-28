/* A made-up game's sound check: a minute of 60 Hz frames against a ring that drains at 22,050 Hz, driven through a
 * table that is not a console.
 *
 *   my-ps2-sound
 *
 * On the console the table is pas_ps2_ops() from pas/ps2.h, set up by pas_ps2_start; here the ring is a counter that
 * the "IOP" empties by 22,050 / 60 stereo frames each tick, so the same stream runs in a plain test. It checks that the
 * ring never runs dry once it has filled, and that every rendered frame reaches it exactly once.
 */
#include <stdio.h>

#include "pas/pas.h"

#define RATE 22050
#define RING_BYTES 4096 /* about 46 ms of 22,050 Hz stereo */

static int ring_used; /* bytes queued in the ring */
static long frames_rendered, bytes_played;

static int ring_free(void *c)
{
    (void)c;
    return RING_BYTES - ring_used;
}
static int ring_take(void *c, const void *data, int bytes)
{
    (void)c;
    (void)data;
    ring_used += bytes;
    bytes_played += bytes;
    return bytes;
}

/* the game's mixer: a square wave, left and right in step */
static void mix(void *user, int16_t *pcm, uint16_t frames)
{
    long *phase = (long *)user;
    uint16_t i;
    for (i = 0; i < frames; i++, (*phase)++) {
        int16_t v = (int16_t)((*phase / 50) % 2 ? 6000 : -6000);
        pcm[2 * i] = v;
        pcm[2 * i + 1] = v;
    }
    frames_rendered += frames;
}

static PasStream stream; /* static: the ring may read its buffer after pas_feed returns */

int main(void)
{
    PasOps ops = { 0, ring_free, ring_take, 0 };
    long phase = 0, underruns = 0;
    int tick;

    if (!pas_init(&stream, &ops, mix, &phase, 0, 90)) {
        fprintf(stderr, "init failed\n");
        return 1;
    }
    for (tick = 0; tick < 60 * 60; tick++) {
        int drain = (RATE / 60 + (tick % 2)) * PAS_FRAME_BYTES; /* 367 or 368 frames a tick */
        pas_feed(&stream);
        if (tick > 2 && ring_used < drain) {
            underruns++;
        }
        ring_used = ring_used > drain ? ring_used - drain : 0;
    }
    printf("rendered %ld frames, played %ld bytes, %ld underruns\n", frames_rendered, bytes_played, underruns);
    if (underruns) {
        fprintf(stderr, "the ring ran dry\n");
        return 1;
    }
    if (bytes_played + stream.pending_bytes != frames_rendered * PAS_FRAME_BYTES) {
        fprintf(stderr, "a rendered frame was lost or played twice\n");
        return 1;
    }
    return 0;
}
