/* A game's deterministic-simulation test using det-core: a seeded replay must come out the same every run. */
#include <stdio.h>
#include "det/det.h"

typedef struct Sim {
    int32_t x;       /* Q16.16 */
    int32_t velocity; /* Q16.16 units per tick */
    det_rng rng;
} Sim;

static void sim_tick(Sim* sim) {
    /* a small random push each tick, a little drag, then move */
    sim->velocity += det_fx_from_ratio(det_rng_range_i32(&sim->rng, -3, 3), 16);
    sim->velocity = det_fx_mul(sim->velocity, det_fx_from_ratio(63, 64));
    sim->x = det_fx_clamp(sim->x + sim->velocity, det_fx_from_int(-100), det_fx_from_int(100));
}

static uint32_t run(uint32_t seed) {
    Sim sim;
    uint32_t hash = det_hash32_begin();
    int i;
    sim.x = 0;
    sim.velocity = 0;
    sim.rng = det_rng_seed(seed);
    for (i = 0; i < 600; i++) {
        sim_tick(&sim);
        hash = det_hash32_i32(hash, sim.x);
    }
    return hash;
}

int main(void) {
    const uint32_t first = run(2026u);
    const uint32_t again = run(2026u);
    const uint32_t other = run(2027u);
    printf("replay hash %08x (again %08x, other seed %08x)\n", (unsigned)first, (unsigned)again, (unsigned)other);
    if (first != again) {
        fprintf(stderr, "the same seed gave two different replays\n");
        return 1;
    }
    if (first == other) {
        fprintf(stderr, "two seeds gave the same replay\n");
        return 1;
    }
    return 0;
}
