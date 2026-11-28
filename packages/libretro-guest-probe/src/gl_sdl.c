/* A hidden window, an OpenGL core-profile context and a framebuffer object, with SDL3 (or SDL2 when built with
 * LGP_SDL2). The OpenGL functions are looked up through SDL, so nothing links against an OpenGL library. */
#ifdef LGP_SDL2
#include <SDL.h>
#else
#include <SDL3/SDL.h>
#endif

#include <string.h>

#include "gl.h"

#if defined(_WIN32) && !defined(_WIN64)
#define LGP_APIENTRY __stdcall
#else
#define LGP_APIENTRY
#endif

/* the few OpenGL 3 names this file uses, so it needs no OpenGL header */
typedef unsigned int GLenum_;
typedef unsigned int GLuint_;
typedef int GLint_;
typedef int GLsizei_;
#define GL_TEXTURE_2D_ 0x0DE1u
#define GL_TEXTURE_MIN_FILTER_ 0x2801u
#define GL_TEXTURE_MAG_FILTER_ 0x2800u
#define GL_NEAREST_ 0x2600
#define GL_RGBA8_ 0x8058
#define GL_RGBA_ 0x1908u
#define GL_RGB_ 0x1907u
#define GL_UNSIGNED_BYTE_ 0x1401u
#define GL_PACK_ALIGNMENT_ 0x0D05u
#define GL_FRAMEBUFFER_ 0x8D40u
#define GL_READ_FRAMEBUFFER_ 0x8CA8u
#define GL_READ_FRAMEBUFFER_BINDING_ 0x8CAAu
#define GL_RENDERBUFFER_ 0x8D41u
#define GL_COLOR_ATTACHMENT0_ 0x8CE0u
#define GL_DEPTH_ATTACHMENT_ 0x8D00u
#define GL_DEPTH_STENCIL_ATTACHMENT_ 0x821Au
#define GL_DEPTH24_STENCIL8_ 0x88F0u
#define GL_DEPTH_COMPONENT24_ 0x81A6u
#define GL_FRAMEBUFFER_COMPLETE_ 0x8CD5u

static struct {
    void(LGP_APIENTRY *GenTextures)(GLsizei_, GLuint_ *);
    void(LGP_APIENTRY *DeleteTextures)(GLsizei_, const GLuint_ *);
    void(LGP_APIENTRY *BindTexture)(GLenum_, GLuint_);
    void(LGP_APIENTRY *TexParameteri)(GLenum_, GLenum_, GLint_);
    void(LGP_APIENTRY *TexImage2D)(GLenum_, GLint_, GLint_, GLsizei_, GLsizei_, GLint_, GLenum_, GLenum_, const void *);
    void(LGP_APIENTRY *GenFramebuffers)(GLsizei_, GLuint_ *);
    void(LGP_APIENTRY *DeleteFramebuffers)(GLsizei_, const GLuint_ *);
    void(LGP_APIENTRY *BindFramebuffer)(GLenum_, GLuint_);
    void(LGP_APIENTRY *FramebufferTexture2D)(GLenum_, GLenum_, GLenum_, GLuint_, GLint_);
    GLenum_(LGP_APIENTRY *CheckFramebufferStatus)(GLenum_);
    void(LGP_APIENTRY *GenRenderbuffers)(GLsizei_, GLuint_ *);
    void(LGP_APIENTRY *DeleteRenderbuffers)(GLsizei_, const GLuint_ *);
    void(LGP_APIENTRY *BindRenderbuffer)(GLenum_, GLuint_);
    void(LGP_APIENTRY *RenderbufferStorage)(GLenum_, GLenum_, GLsizei_, GLsizei_);
    void(LGP_APIENTRY *FramebufferRenderbuffer)(GLenum_, GLenum_, GLenum_, GLuint_);
    void(LGP_APIENTRY *GetIntegerv)(GLenum_, GLint_ *);
    void(LGP_APIENTRY *PixelStorei)(GLenum_, GLint_);
    void(LGP_APIENTRY *ReadPixels)(GLint_, GLint_, GLsizei_, GLsizei_, GLenum_, GLenum_, void *);
} gl;

static SDL_Window *window;
static SDL_GLContext context;
static int video_started;
static GLuint_ fbo, color, depth_buffer;
static unsigned pumps;

int lgp_gl_open(unsigned major, unsigned minor, FILE *log)
{
#ifdef LGP_SDL2
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
#else
    if (!SDL_Init(SDL_INIT_VIDEO)) {
#endif
        fprintf(log, "SDL: %s\n", SDL_GetError());
        return -1;
    }
    video_started = 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, (int)major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, (int)minor);
#ifdef LGP_SDL2
    window = SDL_CreateWindow("libretro guest probe", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
#else
    window = SDL_CreateWindow("libretro guest probe", 640, 480, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
#endif
    if (window) {
        context = SDL_GL_CreateContext(window);
    }
    if (!window || !context) {
        fprintf(log, "GL: %s\n", SDL_GetError());
        lgp_gl_close();
        return -1;
    }
    SDL_GL_SetSwapInterval(0);
    return 0;
}

void *lgp_gl_proc(const char *name)
{
    /* a function pointer into a data pointer, without a cast the compiler objects to under -Wpedantic */
#ifdef LGP_SDL2
    return SDL_GL_GetProcAddress(name);
#else
    union {
        SDL_FunctionPointer f;
        void *d;
    } u;
    u.f = SDL_GL_GetProcAddress(name);
    return u.d;
#endif
}

#define GL_LOAD(field, name)                          \
    do {                                              \
        void *p_ = lgp_gl_proc(name);                 \
        if (!p_) {                                    \
            fprintf(log, "GL: no %s\n", name);        \
            return -1;                                \
        }                                             \
        memcpy(&gl.field, &p_, sizeof(p_));           \
    } while (0)

int lgp_gl_target_open(unsigned width, unsigned height, int depth, int stencil, FILE *log)
{
    GLenum_ status;
    if (!context) {
        return -1;
    }
    GL_LOAD(GenTextures, "glGenTextures");
    GL_LOAD(DeleteTextures, "glDeleteTextures");
    GL_LOAD(BindTexture, "glBindTexture");
    GL_LOAD(TexParameteri, "glTexParameteri");
    GL_LOAD(TexImage2D, "glTexImage2D");
    GL_LOAD(GenFramebuffers, "glGenFramebuffers");
    GL_LOAD(DeleteFramebuffers, "glDeleteFramebuffers");
    GL_LOAD(BindFramebuffer, "glBindFramebuffer");
    GL_LOAD(FramebufferTexture2D, "glFramebufferTexture2D");
    GL_LOAD(CheckFramebufferStatus, "glCheckFramebufferStatus");
    GL_LOAD(GenRenderbuffers, "glGenRenderbuffers");
    GL_LOAD(DeleteRenderbuffers, "glDeleteRenderbuffers");
    GL_LOAD(BindRenderbuffer, "glBindRenderbuffer");
    GL_LOAD(RenderbufferStorage, "glRenderbufferStorage");
    GL_LOAD(FramebufferRenderbuffer, "glFramebufferRenderbuffer");
    GL_LOAD(GetIntegerv, "glGetIntegerv");
    GL_LOAD(PixelStorei, "glPixelStorei");
    GL_LOAD(ReadPixels, "glReadPixels");

    gl.GenTextures(1, &color);
    gl.BindTexture(GL_TEXTURE_2D_, color);
    gl.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
    gl.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
    gl.TexImage2D(GL_TEXTURE_2D_, 0, GL_RGBA8_, (GLsizei_)width, (GLsizei_)height, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, NULL);
    gl.BindTexture(GL_TEXTURE_2D_, 0);
    gl.GenFramebuffers(1, &fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER_, fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D_, color, 0);
    if (depth || stencil) {
        gl.GenRenderbuffers(1, &depth_buffer);
        gl.BindRenderbuffer(GL_RENDERBUFFER_, depth_buffer);
        gl.RenderbufferStorage(GL_RENDERBUFFER_, stencil ? GL_DEPTH24_STENCIL8_ : GL_DEPTH_COMPONENT24_, (GLsizei_)width,
                               (GLsizei_)height);
        gl.FramebufferRenderbuffer(GL_FRAMEBUFFER_, stencil ? GL_DEPTH_STENCIL_ATTACHMENT_ : GL_DEPTH_ATTACHMENT_,
                                   GL_RENDERBUFFER_, depth_buffer);
        gl.BindRenderbuffer(GL_RENDERBUFFER_, 0);
    }
    status = gl.CheckFramebufferStatus(GL_FRAMEBUFFER_);
    gl.BindFramebuffer(GL_FRAMEBUFFER_, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE_) {
        fprintf(log, "GL: the framebuffer object is incomplete (0x%x)\n", status);
        return -1;
    }
    return 0;
}

uintptr_t lgp_gl_target(void)
{
    return fbo;
}

int lgp_gl_read_rgb(unsigned width, unsigned height, uint8_t *rgb)
{
    GLint_ old = 0;
    if (!fbo) {
        return -1;
    }
    gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING_, &old);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER_, fbo);
    gl.PixelStorei(GL_PACK_ALIGNMENT_, 1);
    gl.ReadPixels(0, 0, (GLsizei_)width, (GLsizei_)height, GL_RGB_, GL_UNSIGNED_BYTE_, rgb);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER_, (GLuint_)old);
    return 0;
}

void lgp_gl_pump(void)
{
    if (video_started && ++pumps % 120u == 0) {
        SDL_PumpEvents();
    }
}

void lgp_gl_close(void)
{
    if (context && fbo) {
        gl.DeleteFramebuffers(1, &fbo);
        gl.DeleteTextures(1, &color);
        if (depth_buffer) {
            gl.DeleteRenderbuffers(1, &depth_buffer);
        }
    }
    fbo = color = depth_buffer = 0;
    pumps = 0;
    if (context) {
#ifdef LGP_SDL2
        SDL_GL_DeleteContext(context);
#else
        SDL_GL_DestroyContext(context);
#endif
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
