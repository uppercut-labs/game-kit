/* The hidden OpenGL context a hardware-render core asks for, and the framebuffer object it draws into. Made only when the
 * core asks, so a software core runs on a machine with no display and no GPU. gl_sdl.c makes it with SDL3 or SDL2;
 * gl_none.c refuses. */
#ifndef LGP_GL_H
#define LGP_GL_H

#include <stdint.h>
#include <stdio.h>

/* 0 on success (the context is current on this thread), -1 on failure with a line written to `log`. */
int lgp_gl_open(unsigned major, unsigned minor, FILE *log);
void *lgp_gl_proc(const char *name);
void lgp_gl_close(void);

/* A framebuffer object of `width` x `height` with an RGBA8 colour texture, and a depth (and stencil) buffer when asked.
 * 0 on success, -1 with a line written to `log`. lgp_gl_target() is its name, 0 before it is made. */
int lgp_gl_target_open(unsigned width, unsigned height, int depth, int stencil, FILE *log);
uintptr_t lgp_gl_target(void);
/* Read `width` x `height` pixels at the target's origin as RGB24 rows, bottom row first (OpenGL's order). 0 or -1. */
int lgp_gl_read_rgb(unsigned width, unsigned height, uint8_t *rgb);
/* Let the window system process events now and then, so a hidden window is not marked as hung. */
void lgp_gl_pump(void);

#endif
