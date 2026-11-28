/* Built without SDL: no hardware-render core can have a context. */
#include "gl.h"

int lgp_gl_open(unsigned major, unsigned minor, FILE *log)
{
    (void)major;
    (void)minor;
    fprintf(log, "GL: this build has no SDL, so no OpenGL context can be made\n");
    return -1;
}

void *lgp_gl_proc(const char *name)
{
    (void)name;
    return 0;
}

void lgp_gl_close(void) {}

int lgp_gl_target_open(unsigned width, unsigned height, int depth, int stencil, FILE *log)
{
    (void)width;
    (void)height;
    (void)depth;
    (void)stencil;
    (void)log;
    return -1;
}

uintptr_t lgp_gl_target(void)
{
    return 0;
}

int lgp_gl_read_rgb(unsigned width, unsigned height, uint8_t *rgb)
{
    (void)width;
    (void)height;
    (void)rgb;
    return -1;
}

void lgp_gl_pump(void) {}
