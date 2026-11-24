/* Built without SDL3: no hardware-render core can have a context. */
#include "gl.h"

int lgp_gl_open(unsigned major, unsigned minor, FILE *log)
{
    (void)major;
    (void)minor;
    fprintf(log, "GL: this build has no SDL3, so no OpenGL context can be made\n");
    return -1;
}

void *lgp_gl_proc(const char *name)
{
    (void)name;
    return 0;
}

void lgp_gl_close(void) {}
