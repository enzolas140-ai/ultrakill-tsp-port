/* bridge_gl.h - cria SDL_Window + EGL context e devolve um ANativeWindow
 * que Unity aceita. */
#ifndef BRIDGE_GL_H
#define BRIDGE_GL_H

#include <stdint.h>

/* opaque para o bridge_main */
struct ANativeWindow;
struct SDL_Window;
struct __eglDisplay;
struct __eglSurface;

typedef enum {
  SHIM_GL_OK = 0,
  SHIM_GL_NO_SDL,       /* SDL2 nao linkou */
  SHIM_GL_NO_EGL,       /* dlopen libEGL falhou */
  SHIM_GL_NO_WINDOW,    /* SDL_CreateWindow falhou (sem X11/Wayland/fbdev) */
  SHIM_GL_CTX_FAIL,     /* eglMakeCurrent falhou */
} shim_gl_status;

typedef struct shim_gl_ctx {
  struct SDL_Window *sdl;                /* janela real */
  void              *egl_display;        /* EGLDisplay */
  void              *egl_context;        /* EGLContext */
  void              *egl_surface;        /* EGLSurface (pre-criado) */
  /* Handle nativo do backend (X11 WindowID / wl_surface* / ...) que
   * eglCreateWindowSurface realmente aceita no Linux: */
  void              *native_window;
  int                width, height;
  /* Nosso ANativeWindow fake (mesmo tipo usado em activity->window): */
  struct ANativeWindow *fake;
  /* Modo ULTRAKILL_NO_SDL_GL (R36S): copia o Pbuffer GL pra tela via
   * SDL software renderer (KMS dumb buffer), sem SDL/GLX/EGL window. */
  void              *sdl_renderer;   /* SDL_Renderer*  */
  void              *sdl_texture;    /* SDL_Texture*   */
  void              *pixel_buf;      /* RGBA w*h*4     */
} shim_gl_ctx;

/* Cria SDL window + EGL context com client API GLES3 (fallback GLES2).
 * Retorna SHIM_GL_OK / zero se OK, negativo se erro. */
shim_gl_status shim_gl_init(shim_gl_ctx *out, int w, int h, const char *title);

/* glReadPixels do pbuffer -> SDL_UpdateTexture -> RenderPresent.
 * No-op se modo normal (TSP) ou se renderer nao criado. */
void shim_gl_present(shim_gl_ctx *c);

/* Marca destroy (chamar no shutdown). */
void shim_gl_shutdown(shim_gl_ctx *c);

/* Swaps: o loop Unity nao faz (o Unity chama eglSwapBuffers internamente,
 * ver TODO em bridge_egl_shim.c). Este helper so existe para caso voce
 * queira rodar um smoke test sem Unity. */
void shim_gl_swap(shim_gl_ctx *c);

#endif
