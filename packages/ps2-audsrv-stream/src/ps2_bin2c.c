/* pas_ps2_config_bin2c: for a program that embeds audsrv with bin2c under the names in pas/ps2.h. */
#include "pas/ps2.h"

PasPs2Config pas_ps2_config_bin2c(int rate, uint8_t volume, PasRender render, void *user)
{
    PasPs2Config config;
    config.audsrv = audsrv_irx;
    config.size_audsrv = size_audsrv_irx;
    config.rate = rate;
    config.volume = volume;
    config.chunk_frames = 0;
    config.render = render;
    config.user = user;
    return config;
}
