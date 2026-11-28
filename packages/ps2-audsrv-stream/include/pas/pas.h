/* ps2-audsrv-stream: keeps a sound ring fed once a frame from a callback that renders PCM. Part of ps2-audsrv-stream
 * 1.0.0 (MIT).
 *
 * The stream is bookkeeping over a small table of functions (PasOps): how much of the ring is free, hand it bytes, set
 * the volume. Nothing here calls the console, so a host test drives it with a fake ring. The PS2SDK glue in pas/ps2.h
 * starts audsrv and fills the table with audsrv calls.
 *
 * The PCM is signed 16-bit stereo, interleaved left then right, in the machine's byte order: one stereo frame is four
 * bytes.
 */
#ifndef PAS_H
#define PAS_H

#include <stdint.h>

#define PAS_VERSION "1.0.0"

/* 64-byte alignment for the PCM buffer the IOP reads by DMA (C99 has no standard spelling). */
#if defined(_MSC_VER)
#define PAS_ALIGN64_BEFORE __declspec(align(64))
#define PAS_ALIGN64_AFTER
#else
#define PAS_ALIGN64_BEFORE
#define PAS_ALIGN64_AFTER __attribute__((aligned(64)))
#endif

#define PAS_MAX_FRAMES 960 /* the most stereo frames rendered at once: 43.5 ms at 22,050 Hz */
#define PAS_FRAME_BYTES 4 /* one stereo frame of signed 16-bit samples */
#define PAS_RING_SLACK_BYTES 4 /* left free in the ring, so equal read and write positions never mean "full" */

/* Renders `frames` stereo frames (2 * frames int16_t, left first) into `pcm`. `frames` is 1..the stream's chunk. */
typedef void (*PasRender)(void *user, int16_t *pcm, uint16_t frames);

/*
 * The sound ring, as a table of functions. pas/ps2.h fills it with audsrv; a test fills it with a fake. Every function
 * gets `context` first. `available` and `play` are required; `set_volume` may be NULL, and then muting does nothing.
 */
typedef struct PasOps {
    void *context;
    int (*available)(void *context); /* free bytes in the ring (negative counts as none) */
    int (*play)(void *context, const void *data, int bytes); /* bytes the ring took (0 or negative: none) */
    int (*set_volume)(void *context, int volume);
} PasOps;

/*
 * The stream. Keep it in static storage: the ring may read the PCM buffer after pas_feed returns, so the buffer must
 * live as long as the sound runs, and the IOP wants it 64-byte aligned.
 */
typedef PAS_ALIGN64_BEFORE struct PasStream {
    int16_t pcm[2 * PAS_MAX_FRAMES] PAS_ALIGN64_AFTER;
    PasOps ops;
    PasRender render;
    void *user;
    uint16_t chunk_frames; /* the most frames rendered at once, 1..PAS_MAX_FRAMES */
    uint16_t pending_bytes; /* rendered bytes the ring has not taken yet */
    uint16_t pending_offset; /* where they start in pcm, in bytes */
    uint8_t volume; /* the volume to come back to after a mute */
    uint8_t muted_feeds; /* feeds left before the volume comes back; 0 when not muted */
    uint8_t ready;
} PasStream;

/*
 * Sets up a stream over `ops`. `chunk_frames` is the most frames rendered at once (0 = PAS_MAX_FRAMES, at most
 * PAS_MAX_FRAMES). `volume` is what pas_mute_for comes back to; pas_init does not set it on the ring. Returns 1, or 0
 * (and a stream that is not ready) for a NULL stream, ops, render callback or required function.
 */
uint8_t pas_init(PasStream *stream, const PasOps *ops, PasRender render, void *user, uint16_t chunk_frames,
                 uint8_t volume);

/* 1 once pas_init succeeded. Every other call does nothing on a stream that is not ready. */
uint8_t pas_ready(const PasStream *stream);

/*
 * One frame's work. In order:
 *   1. a mute that has run its course gives the volume back;
 *   2. the free space is the ring's free bytes less PAS_RING_SLACK_BYTES, rounded down to whole stereo frames, and
 *      under one frame there is nothing to do;
 *   3. when nothing is pending, render min(free frames, chunk) frames;
 *   4. hand the ring as much of the pending bytes as fits.
 * Returns the bytes the ring took this call. Rendered bytes it did not take wait for the next call, and nothing new is
 * rendered until they are gone, so the sound never skips or repeats.
 */
int pas_feed(PasStream *stream);

/* Forgets rendered bytes the ring has not taken, so the next feed renders fresh sound (a cut to a new piece of music). */
void pas_drop_pending(PasStream *stream);

/*
 * Sets the ring's volume to 0 now and back to the stream's volume at the start of the `feeds`-th pas_feed from now.
 * Meanwhile pas_feed keeps the ring full, so sound already queued drains while silent. 0 gives the volume back at once.
 */
void pas_mute_for(PasStream *stream, uint8_t feeds);

/* Changes the stream's volume, and the ring's unless a mute is running (it then comes back to the new volume). */
void pas_set_volume(PasStream *stream, uint8_t volume);

#endif
