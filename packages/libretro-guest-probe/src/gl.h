/* The hidden OpenGL context a hardware-render core asks for. Made only when the core asks, so a software core runs on a
 * machine with no display and no GPU. gl_sdl.c makes it with SDL3; gl_none.c refuses. */
#ifndef LGP_GL_H
#define LGP_GL_H

#include <stdio.h>

/* 0 on success (the context is current on this thread), -1 on failure with a line written to `log`. */
int lgp_gl_open(unsigned major, unsigned minor, FILE *log);
void *lgp_gl_proc(const char *name);
void lgp_gl_close(void);

#endif
