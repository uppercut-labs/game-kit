/* A hidden window and an OpenGL core-profile context, with SDL3. */
#include <SDL3/SDL.h>

#include "gl.h"

static SDL_Window *window;
static SDL_GLContext context;
static int video_started;

int lgp_gl_open(unsigned major, unsigned minor, FILE *log)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(log, "SDL: %s\n", SDL_GetError());
        return -1;
    }
    video_started = 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, (int)major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, (int)minor);
    window = SDL_CreateWindow("libretro guest probe", 640, 480, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (window) {
        context = SDL_GL_CreateContext(window);
    }
    if (!window || !context) {
        fprintf(log, "GL: %s\n", SDL_GetError());
        lgp_gl_close();
        return -1;
    }
    return 0;
}

void *lgp_gl_proc(const char *name)
{
    SDL_FunctionPointer p = SDL_GL_GetProcAddress(name);
    void *out;
    /* a function pointer into a data pointer, without a cast the compiler objects to under -Wpedantic */
    {
        union {
            SDL_FunctionPointer f;
            void *d;
        } u;
        u.f = p;
        out = u.d;
    }
    return out;
}

void lgp_gl_close(void)
{
    if (context) {
        SDL_GL_DestroyContext(context);
        context = NULL;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    if (video_started) {
        SDL_Quit();
        video_started = 0;
    }
}
