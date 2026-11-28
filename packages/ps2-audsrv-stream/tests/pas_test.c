/* pas_test: the stream's bookkeeping against a fake sound ring. No console, no sound. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pas/pas.h"

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

/* ---- the fake ring: a byte log of everything played, a scripted amount of free space, and a volume log ---- */

typedef struct Ring {
    int free_bytes; /* what available() answers */
    int take_limit; /* the most bytes play() takes per call (0 = all it is given) */
    int play_result; /* when not 0, play() returns this instead */
    int16_t played[32768];
    int played_bytes;
    int plays;
    int last_play_bytes;
    int volumes[16];
    int volume_n;
} Ring;

static int ring_available(void *context)
{
    return ((Ring *)context)->free_bytes;
}

static int ring_play(void *context, const void *data, int bytes)
{
    Ring *ring = (Ring *)context;
    int take = bytes;

    ring->plays++;
    ring->last_play_bytes = bytes;
    if (ring->play_result) {
        return ring->play_result;
    }
    if (ring->take_limit && take > ring->take_limit) {
        take = ring->take_limit;
    }
    if (ring->played_bytes + take <= (int)sizeof(ring->played)) {
        memcpy((char *)ring->played + ring->played_bytes, data, (size_t)take);
    }
    ring->played_bytes += take;
    return take;
}

static int ring_set_volume(void *context, int volume)
{
    Ring *ring = (Ring *)context;
    if (ring->volume_n < 16) {
        ring->volumes[ring->volume_n++] = volume;
    }
    return 0;
}

/* ---- the fake renderer: a ramp, so every stereo frame is distinct and a skip or repeat shows ---- */

typedef struct Synth {
    int16_t next;
    int calls;
    uint16_t last_frames;
} Synth;

static void synth_render(void *user, int16_t *pcm, uint16_t frames)
{
    Synth *synth = (Synth *)user;
    uint16_t i;

    synth->calls++;
    synth->last_frames = frames;
    for (i = 0; i < frames; i++) {
        pcm[2 * i] = synth->next;
        pcm[2 * i + 1] = (int16_t)-synth->next;
        synth->next++;
    }
}

static Ring ring;
static Synth synth;
static PasStream stream;

static PasOps ring_ops(void)
{
    PasOps ops;
    ops.context = &ring;
    ops.available = ring_available;
    ops.play = ring_play;
    ops.set_volume = ring_set_volume;
    return ops;
}

static void start(uint16_t chunk)
{
    PasOps ops = ring_ops();
    memset(&ring, 0, sizeof(ring));
    memset(&synth, 0, sizeof(synth));
    CHECK(pas_init(&stream, &ops, synth_render, &synth, chunk, 90) == 1, "init");
}

/* the played stream is the ramp with no gap and no repeat */
static int played_is_ramp(void)
{
    int i;
    for (i = 0; i < ring.played_bytes / PAS_FRAME_BYTES; i++) {
        if (ring.played[2 * i] != (int16_t)i || ring.played[2 * i + 1] != (int16_t)-i) {
            return 0;
        }
    }
    return 1;
}

static void test_init(void)
{
    PasOps ops = ring_ops();
    PasStream other;

    CHECK(pas_init(NULL, &ops, synth_render, &synth, 0, 90) == 0, "no stream");
    CHECK(pas_init(&other, NULL, synth_render, &synth, 0, 90) == 0 && !pas_ready(&other), "no ops");
    CHECK(pas_init(&other, &ops, NULL, &synth, 0, 90) == 0 && !pas_ready(&other), "no renderer");
    ops.available = NULL;
    CHECK(pas_init(&other, &ops, synth_render, &synth, 0, 90) == 0, "no available()");
    ops = ring_ops();
    ops.play = NULL;
    CHECK(pas_init(&other, &ops, synth_render, &synth, 0, 90) == 0, "no play()");
    ops = ring_ops();
    ops.set_volume = NULL;
    CHECK(pas_init(&other, &ops, synth_render, &synth, 0, 90) == 1, "set_volume is optional");
    CHECK(pas_init(&other, &ops, synth_render, &synth, 0, 90) && other.chunk_frames == PAS_MAX_FRAMES, "0 is the most");
    CHECK(pas_init(&other, &ops, synth_render, &synth, 5000, 90) && other.chunk_frames == PAS_MAX_FRAMES, "clamped");
    CHECK(pas_init(&other, &ops, synth_render, &synth, 256, 90) && other.chunk_frames == 256, "a smaller chunk");
    CHECK(!pas_ready(NULL), "NULL is not ready");

    start(0);
    CHECK(ring.volume_n == 0, "init does not touch the volume");
    CHECK(((uintptr_t)stream.pcm & 63u) == 0, "the PCM buffer is 64-byte aligned");
}

static void test_not_ready(void)
{
    PasStream idle;
    memset(&idle, 0, sizeof(idle));
    CHECK(pas_feed(&idle) == 0 && pas_feed(NULL) == 0, "a stream that is not ready feeds nothing");
    pas_drop_pending(&idle);
    pas_mute_for(&idle, 3);
    pas_set_volume(&idle, 10);
    CHECK(idle.volume == 0 && idle.muted_feeds == 0, "and ignores the rest");
}

static void test_fill(void)
{
    int sent;

    /* a big free ring: one chunk rendered and all of it taken */
    start(0);
    ring.free_bytes = 100000;
    sent = pas_feed(&stream);
    CHECK(sent == PAS_MAX_FRAMES * PAS_FRAME_BYTES && synth.calls == 1 && synth.last_frames == PAS_MAX_FRAMES,
          "a whole chunk: sent %d, frames %u", sent, synth.last_frames);
    CHECK(stream.pending_bytes == 0, "nothing pending");

    /* the slack: 4 bytes are left free, then whole frames only */
    start(0);
    ring.free_bytes = 4 + 10 * PAS_FRAME_BYTES + 3;
    sent = pas_feed(&stream);
    CHECK(sent == 10 * PAS_FRAME_BYTES && synth.last_frames == 10, "10 frames fit after the slack: sent %d", sent);

    /* under one frame after the slack: nothing rendered, nothing played */
    start(0);
    ring.free_bytes = 7;
    CHECK(pas_feed(&stream) == 0 && synth.calls == 0 && ring.plays == 0, "7 free bytes: nothing to do");
    ring.free_bytes = 8;
    CHECK(pas_feed(&stream) == PAS_FRAME_BYTES && synth.last_frames == 1, "8 free bytes: one frame");
    ring.free_bytes = -5;
    CHECK(pas_feed(&stream) == 0 && ring.plays == 1, "a negative answer is no space");

    /* a smaller chunk caps each render */
    start(100);
    ring.free_bytes = 100000;
    CHECK(pas_feed(&stream) == 100 * PAS_FRAME_BYTES && synth.last_frames == 100, "chunk of 100");
}

static void test_partial(void)
{
    int i;

    /* the ring takes part of a render: the rest waits, nothing new is rendered until it is gone */
    start(0);
    ring.free_bytes = 100000;
    ring.take_limit = 1000;
    CHECK(pas_feed(&stream) == 1000 && synth.calls == 1, "first part");
    CHECK(stream.pending_bytes == PAS_MAX_FRAMES * PAS_FRAME_BYTES - 1000 && stream.pending_offset == 1000,
          "the rest pending: %u at %u", stream.pending_bytes, stream.pending_offset);
    CHECK(pas_feed(&stream) == 1000 && synth.calls == 1, "second part, no new render");
    CHECK(pas_feed(&stream) == 1000 && synth.calls == 1, "third part");
    CHECK(pas_feed(&stream) == 840 && synth.calls == 1 && stream.pending_bytes == 0, "the last 840 bytes");
    CHECK(pas_feed(&stream) == 1000 && synth.calls == 2, "then a new render");

    /* the free space, not the ring's appetite, limits a send */
    start(0);
    ring.free_bytes = 100000;
    pas_feed(&stream);
    ring.free_bytes = 4 + 2 * PAS_FRAME_BYTES;
    ring.take_limit = 0;
    stream.pending_bytes = 400;
    stream.pending_offset = 0;
    CHECK(pas_feed(&stream) == 2 * PAS_FRAME_BYTES && ring.last_play_bytes == 2 * PAS_FRAME_BYTES, "send fits the space");

    /* a refused play keeps everything pending */
    start(0);
    ring.free_bytes = 100000;
    ring.play_result = -1;
    CHECK(pas_feed(&stream) == 0 && stream.pending_bytes == PAS_MAX_FRAMES * PAS_FRAME_BYTES, "refused: all pending");
    ring.play_result = 0;
    CHECK(pas_feed(&stream) == PAS_MAX_FRAMES * PAS_FRAME_BYTES && synth.calls == 1, "taken later, not rendered twice");

    /* a ring that answers more than it was given is held to what it was given */
    start(0);
    ring.free_bytes = 100000;
    ring.play_result = 999999;
    CHECK(pas_feed(&stream) == PAS_MAX_FRAMES * PAS_FRAME_BYTES && stream.pending_bytes == 0, "clamped to the send");

    /* over many uneven frames the ring sees the ramp exactly once, in order */
    start(300);
    for (i = 0; i < 40; i++) {
        ring.free_bytes = 4 + (i * 37 % 500) * PAS_FRAME_BYTES + (i % 4);
        ring.take_limit = 1 + (i * 113 % 1300);
        pas_feed(&stream);
    }
    CHECK(ring.played_bytes > 0 && played_is_ramp(), "no gap, no repeat over %d bytes", ring.played_bytes);
    CHECK(ring.played_bytes + stream.pending_bytes == synth.next * PAS_FRAME_BYTES,
          "played plus pending is everything rendered");
}

static void test_drop_and_mute(void)
{
    int i;

    start(0);
    ring.free_bytes = 100000;
    ring.take_limit = 1000;
    pas_feed(&stream);
    pas_drop_pending(&stream);
    CHECK(stream.pending_bytes == 0 && stream.pending_offset == 0, "dropped");
    CHECK(pas_feed(&stream) == 1000 && synth.calls == 2, "the next feed renders fresh sound");

    /* a mute of 3 feeds: volume 0 now, back to 90 at the start of the third feed, and the ring kept fed */
    start(0);
    ring.free_bytes = 100000;
    pas_mute_for(&stream, 3);
    CHECK(ring.volume_n == 1 && ring.volumes[0] == 0, "muted now");
    for (i = 0; i < 2; i++) {
        CHECK(pas_feed(&stream) > 0, "fed while muted");
    }
    CHECK(ring.volume_n == 1, "still muted after two feeds");
    pas_feed(&stream);
    CHECK(ring.volume_n == 2 && ring.volumes[1] == 90, "back to 90 on the third feed");
    pas_feed(&stream);
    CHECK(ring.volume_n == 2, "and left alone after");

    /* a volume change during a mute waits for the mute */
    start(0);
    pas_mute_for(&stream, 2);
    pas_set_volume(&stream, 40);
    CHECK(ring.volume_n == 1, "no change while muted");
    pas_feed(&stream);
    pas_feed(&stream);
    CHECK(ring.volume_n == 2 && ring.volumes[1] == 40, "comes back at the new volume");
    pas_set_volume(&stream, 70);
    CHECK(ring.volume_n == 3 && ring.volumes[2] == 70, "applied at once when not muted");

    /* a mute of 0 gives the volume back at once */
    start(0);
    pas_mute_for(&stream, 5);
    pas_mute_for(&stream, 0);
    CHECK(ring.volume_n == 2 && ring.volumes[1] == 90 && stream.muted_feeds == 0, "unmuted");

    /* the volume comes back even on a frame with no ring space */
    start(0);
    ring.free_bytes = 0;
    pas_mute_for(&stream, 1);
    pas_feed(&stream);
    CHECK(ring.volume_n == 2 && ring.volumes[1] == 90, "restored without space");
}

int main(void)
{
    test_init();
    test_not_ready();
    test_fill();
    test_partial();
    test_drop_and_mute();
    printf("pas_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
