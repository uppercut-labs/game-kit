/* The stream's bookkeeping. No console calls: builds anywhere. */
#include "pas/pas.h"

#include <string.h>

uint8_t pas_init(PasStream *stream, const PasOps *ops, PasRender render, void *user, uint16_t chunk_frames,
                 uint8_t volume)
{
    if (!stream) {
        return 0;
    }
    stream->ready = 0;
    if (!ops || !render || !ops->available || !ops->play) {
        return 0;
    }
    if (chunk_frames == 0 || chunk_frames > PAS_MAX_FRAMES) {
        chunk_frames = PAS_MAX_FRAMES;
    }
    memset(stream->pcm, 0, sizeof(stream->pcm));
    stream->ops = *ops;
    stream->render = render;
    stream->user = user;
    stream->chunk_frames = chunk_frames;
    stream->pending_bytes = 0;
    stream->pending_offset = 0;
    stream->volume = volume;
    stream->muted_feeds = 0;
    stream->ready = 1;
    return 1;
}

uint8_t pas_ready(const PasStream *stream)
{
    return stream && stream->ready;
}

static void apply_volume(PasStream *stream, uint8_t volume)
{
    if (stream->ops.set_volume) {
        stream->ops.set_volume(stream->ops.context, volume);
    }
}

int pas_feed(PasStream *stream)
{
    int available, send, sent;

    if (!pas_ready(stream)) {
        return 0;
    }
    if (stream->muted_feeds && !--stream->muted_feeds) {
        apply_volume(stream, stream->volume);
    }

    /* Leave PAS_RING_SLACK_BYTES free, then whole stereo frames only; fewer than one frame is nothing to do. */
    available = stream->ops.available(stream->ops.context);
    if (available < PAS_RING_SLACK_BYTES + PAS_FRAME_BYTES) {
        return 0;
    }
    available = (available - PAS_RING_SLACK_BYTES) / PAS_FRAME_BYTES * PAS_FRAME_BYTES;

    if (!stream->pending_bytes) {
        int frames = available / PAS_FRAME_BYTES;
        if (frames > stream->chunk_frames) {
            frames = stream->chunk_frames;
        }
        stream->render(stream->user, stream->pcm, (uint16_t)frames);
        stream->pending_bytes = (uint16_t)(frames * PAS_FRAME_BYTES);
        stream->pending_offset = 0;
    }

    send = stream->pending_bytes < available ? stream->pending_bytes : available;
    sent = stream->ops.play(stream->ops.context, (const char *)stream->pcm + stream->pending_offset, send);
    if (sent <= 0) {
        return 0;
    }
    if (sent > send) {
        sent = send;
    }
    stream->pending_bytes = (uint16_t)(stream->pending_bytes - sent);
    stream->pending_offset = (uint16_t)(stream->pending_offset + sent);
    return sent;
}

void pas_drop_pending(PasStream *stream)
{
    if (!pas_ready(stream)) {
        return;
    }
    stream->pending_bytes = 0;
    stream->pending_offset = 0;
}

void pas_mute_for(PasStream *stream, uint8_t feeds)
{
    if (!pas_ready(stream)) {
        return;
    }
    if (feeds == 0) {
        stream->muted_feeds = 0;
        apply_volume(stream, stream->volume);
        return;
    }
    apply_volume(stream, 0);
    stream->muted_feeds = feeds;
}

void pas_set_volume(PasStream *stream, uint8_t volume)
{
    if (!pas_ready(stream)) {
        return;
    }
    stream->volume = volume;
    if (!stream->muted_feeds) {
        apply_volume(stream, volume);
    }
}
