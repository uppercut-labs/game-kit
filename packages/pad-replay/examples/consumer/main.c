/* A made-up port that records its pads, then plays the recording back through the reader.
 *
 *   my-port-replay <scratch dir>
 */
#include <stdio.h>

#include "padr/padr.h"

/* the port's own bit set: pad-replay never sees these names, only the numbers */
enum { BTN_START = 0x0008, BTN_JUMP = 0x4000 };

int main(int argc, char **argv)
{
    char path[1024];
    PadrTraceOptions topt = {0};
    PadrTrace *trace;
    PadrSample pads[4];
    PadrScript *script;
    PadrOptions ropt = {4, 4096}; /* four slots, at most 4096 events */
    PadrError err;
    unsigned frame, ok;

    snprintf(path, sizeof(path), "%s/session.input", argc > 1 ? argv[1] : ".");
    remove(path); /* a trace is created exclusively, so a rerun clears the last one first */

    /* record: one call per frame with whatever the pads report; only changes reach the file */
    trace = padr_trace_open(path, 4, &topt);
    if (!trace) {
        fprintf(stderr, "cannot create %s\n", path);
        return 1;
    }
    for (frame = 0; frame < 600; ++frame) {
        unsigned slot;
        for (slot = 0; slot < 4; ++slot) {
            pads[slot] = padr_neutral(slot == 0 ? PADR_FRESH : PADR_ABSENT);
        }
        if (frame >= 30 && frame < 33) {
            pads[0].buttons = BTN_START;
        }
        if (frame >= 100 && frame < 140) {
            pads[0].buttons = BTN_JUMP;
            pads[0].lx = 255;
        }
        padr_trace_sample(trace, frame, pads, 4);
    }
    printf("recorded %u lines for 600 frames of four pads\n", padr_trace_records(trace));
    if (!padr_trace_close(trace)) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }

    /* replay: strict, so a bad file is refused whole with the line to look at */
    script = padr_load_file(path, &ropt, &err);
    if (!script) {
        fprintf(stderr, "%s: %s (line %u)\n", path, padr_status_text(err.status), err.line);
        return 1;
    }
    padr_sample(script, 31, pads, 4);
    ok = pads[0].buttons == BTN_START && pads[1].connected == PADR_ABSENT;
    padr_sample(script, 120, pads, 4);
    ok = ok && pads[0].buttons == BTN_JUMP && pads[0].lx == 255;
    padr_sample(script, 599, pads, 4);
    ok = ok && pads[0].buttons == 0 && pads[0].connected == PADR_FRESH;
    printf("replayed %u events, last frame %u: %s\n", padr_event_count(script), padr_last_frame(script),
           ok ? "matches" : "DIFFERS");
    padr_free(script);
    return ok ? 0 : 1;
}
