/* A made-up port's input, on port-sdl-input and port-settings: its buttons as data, its settings file, one frame.
 *
 *   my-port <scratch dir>
 *
 * Runs with no window and no controller (SDL's dummy drivers). A real port reads the pads once a frame in its own loop.
 */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "pset/pset.h"
#include "psi/psi.h"

/* the port's pad word: its own bits, here the PlayStation 1 order */
static const uint32_t BITS[PSI_PS_COUNT] = {
    0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
    0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000,
};

int main(int argc, char **argv)
{
    char path[1024];
    PsiSlot slots[PSI_PS_COUNT];
    PsiLayout layout;
    PsiOptions opt;
    PsetOptions sopt;
    PortSettings *settings;
    PsiInput *input;
    PsiPad pads[PSI_PLAYERS];

    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }

    /* the settings file: one text file, kept in the order it was read */
    snprintf(path, sizeof(path), "%s/my-port.cfg", argc > 1 ? argv[1] : ".");
    memset(&sopt, 0, sizeof(sopt));
    sopt.name = "my-port";
    sopt.header = "my-port settings. The port rewrites this file: values are kept, comments are not.";
    settings = pset_open(path, &sopt);

    /* the buttons: the table for a PlayStation pad, then what this port changes */
    memset(&layout, 0, sizeof(layout));
    psi_playstation_slots(slots, BITS);
    slots[PSI_PS_L3].defaults = "none";                    /* this port does not use the stick clicks */
    slots[PSI_PS_R3].defaults = "none";
    layout.slots = slots;
    layout.nslots = PSI_PS_COUNT;
    layout.up = BITS[PSI_PS_UP], layout.down = BITS[PSI_PS_DOWN];
    layout.left = BITS[PSI_PS_LEFT], layout.right = BITS[PSI_PS_RIGHT];
    layout.remap_order = psi_playstation_remap_order;
    layout.nremap = psi_playstation_remap_count;

    memset(&opt, 0, sizeof(opt));
    opt.layout = &layout;
    opt.store.ctx = settings;
    opt.store.get = pset_store_get;     /* the three functions of port-settings fit the store */
    opt.store.set = pset_store_set;
    opt.store.save = pset_store_save;
    opt.name = "my-port";
    input = psi_new(&opt);
    if (!input) {
        return 1;
    }

    /* a frame: the on-screen controller holds Circle; nothing else is connected */
    psi_set_touch(input, 0, BITS[PSI_PS_CIRCLE]);
    psi_pads(input, pads);
    printf("player 1 holds %04x (Circle is %04x)\n", (unsigned)pads[0].buttons, (unsigned)BITS[PSI_PS_CIRCLE]);

    psi_set_analog(input, 0, 1);                           /* saved in the settings as p1.analog */
    pset_save(settings);
    printf("p1.analog = %s\n", pset_get_or(settings, "p1.analog", "?"));

    psi_free(input);
    pset_free(settings);
    SDL_Quit();
    return pads[0].buttons == BITS[PSI_PS_CIRCLE] ? 0 : 1;
}
