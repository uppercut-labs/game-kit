/* A stand-in for the PS2SDK's audsrv.h, for the host test of src/ps2.c only. It declares the names the glue uses, with
 * the signatures of the public PS2SDK header (ps2dev/ps2sdk ee/rpc/audsrv/include/audsrv.h); the test defines the
 * functions as fakes. It is not the SDK and proves nothing about it beyond the glue's own calls. */
#ifndef STUB_AUDSRV_H
#define STUB_AUDSRV_H

typedef struct audsrv_fmt_t {
    int freq;
    int bits;
    int channels;
} audsrv_fmt_t;

extern int audsrv_init(void);
extern int audsrv_set_format(struct audsrv_fmt_t *fmt);
extern int audsrv_set_volume(int volume);
extern int audsrv_play_audio(const char *chunk, int bytes);
extern int audsrv_available(void);

#endif
