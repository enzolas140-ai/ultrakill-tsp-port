/* bridge_gl.c - SDL2 + EGL context, e o ANativeWindow fake que o Unity aceita.
 *
 * Como a GE8300 é dirigida pelo PowerVR DDK (não Mesa), assumimos que
 * existem no sistema:
 *   - /usr/lib/{arch}/libEGL.so.1
 *   - /usr/lib/{arch}/libGLESv2.so.2
 * Estes são carregados via dlopen - não linkamos de forma estática para
 * não conflitar com o stub do shim caso exista um libEGL.so falso.
 *
 * SDL2 é linkado dinamicamente via pkg-config (ver CMakeLists.txt).
 *
 * Backend de display: no TSP/SpruceOS o compositor é X11 (Xweston) ou
 * Wayland, escolhido pelo arranque. SDL2 auto-detecta. Se você ver
 * "Cannot open display", teste `SDL_VIDEODRIVER=kmsgrab` ou `x11`. */
#include <android_shim.h>
#include <bridge_gl.h>
#include <bridge_unity.h>
#include "shim_native_window.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_syswm.h>

/* EGL typedefs (evitamos <EGL/egl.h> p/ não forçar cabeçalho Mesa/DDK). */
typedef void *EGLDisplay;
typedef void *EGLContext;
typedef void *EGLSurface;
typedef void *EGLConfig;
typedef int   EGLint;
typedef unsigned int EGLenum;
typedef unsigned char EGLBoolean;
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_TRUE       ((EGLBoolean)1)
#define EGL_SUCCESS    0x3000
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES_API   0x30A0
#define EGL_OPENGL_ES2_BIT  0x0004
#define EGL_OPENGL_ES3_BIT  0x0040
#define EGL_SURFACE_TYPE    0x3033
#define EGL_WINDOW_BIT      0x0004
#define EGL_RED_SIZE        0x3024
#define EGL_GREEN_SIZE      0x3023
#define EGL_BLUE_SIZE       0x3022
#define EGL_ALPHA_SIZE      0x3021
#define EGL_DEPTH_SIZE      0x3025
#define EGL_STENCIL_SIZE    0x3026
#define EGL_NONE            0x3038
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
typedef void *EGLNativeDisplayType;
typedef void *EGLNativeWindowType;

/* API de dlsym: */
typedef EGLDisplay (*PFN_eglGetDisplay)(EGLNativeDisplayType);
typedef EGLBoolean (*PFN_eglInitialize)(EGLDisplay, EGLint*, EGLint*);
typedef EGLBoolean (*PFN_eglChooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
typedef EGLContext (*PFN_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
typedef EGLSurface (*PFN_eglCreateWindowSurface)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
typedef EGLBoolean (*PFN_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLBoolean (*PFN_eglSwapBuffers)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*PFN_eglBindAPI)(EGLenum);
typedef const char*(*PFN_eglQueryString)(EGLDisplay, int);
typedef EGLint     (*PFN_eglGetError)(void);

static PFN_eglGetDisplay              p_eglGetDisplay;
static PFN_eglInitialize              p_eglInitialize;
static PFN_eglChooseConfig            p_eglChooseConfig;
static PFN_eglCreateContext           p_eglCreateContext;
static PFN_eglCreateWindowSurface     p_eglCreateWindowSurface;
static PFN_eglMakeCurrent             p_eglMakeCurrent;
static PFN_eglSwapBuffers             p_eglSwapBuffers;
static PFN_eglBindAPI                 p_eglBindAPI;
static PFN_eglQueryString             p_eglQueryString;
static PFN_eglGetError                p_eglGetError;

static int load_egl(void) {
  /* IMPORTANTE: nunca use "libEGL.so" sem sufixo de versao aqui. Nosso
   * shim do bridge_egl_shim.c instala-se exatamente como libEGL.so (para
   * que o NEEDED do libunity.so caia nele). Se bridge_gl tambem abrir
   * "libEGL.so" cai no proprio shim e recursa. Abrimos apenas a versao
   * .so.1 que o DDK real instala. */
  static const char *paths[] = {
    "libEGL.so.1",
    "/usr/lib/libEGL.so.1",
    "/usr/lib/aarch64-linux-gnu/libEGL.so.1",
    "/usr/lib/arm-linux-gnueabihf/libEGL.so.1",
    "/usr/lib64/libEGL.so.1",
    "/usr/local/lib/libEGL.so.1",
    "/vendor/lib/egl/libEGLPOWER.so",
    "/usr/lib/egl/libEGLPOWER.so",
    NULL
  };
  const char *override = getenv("ULTRAKILL_REAL_LIBEGL");
  void *h = NULL;
  if (override && *override) h = dlopen(override, RTLD_NOW | RTLD_LOCAL);
  for (int i = 0; paths[i] && !h; i++) h = dlopen(paths[i], RTLD_NOW | RTLD_LOCAL);
  if (!h) {
    fprintf(stderr, "bridge_gl: driver EGL real nao encontrado.\n");
    fprintf(stderr, "  defina ULTRAKILL_REAL_LIBEGL=/caminho/absoluto/libEGL.so.1\n");
    return -1;
  }
#define LOAD(fn) do { p_##fn = (PFN_##fn)dlsym(h, #fn); \\
                      if (!p_##fn) { fprintf(stderr, "dlsym %s falhou no driver real\\n", #fn); return -2; } } while(0)
  LOAD(eglGetDisplay); LOAD(eglInitialize); LOAD(eglChooseConfig);
  LOAD(eglCreateContext); LOAD(eglCreateWindowSurface); LOAD(eglMakeCurrent);
  LOAD(eglSwapBuffers); LOAD(eglBindAPI); LOAD(eglQueryString); LOAD(eglGetError);
#undef LOAD
  return 0;
}

/* struct shim_native_window agora esta em shim_native_window.h -
 * compartilhada com bridge_egl_shim.c. */
static struct shim_native_window g_win = {0};

/* forward decl - definido no fim */
static const char *gl_version_probe(void);

__attribute__((visibility("default")))
ANativeWindow *ANativeWindow_get_shim(void) {
  return (ANativeWindow*)&g_win;
}

/* Exposto para o bridge_egl_shim.c via dlsym(RTLD_DEFAULT). Nao static. */
__attribute__((visibility("default")))
struct shim_native_window *shim_get_native_window(void) {
  if (g_win.magic != SHIM_ANW_MAGIC) return NULL;
  return &g_win;
}

shim_gl_status shim_gl_init(shim_gl_ctx *out, int w, int h, const char *title) {
  memset(out, 0, sizeof(*out));

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return SHIM_GL_NO_SDL;
  }
  /* GLES 3.0 pedido; SDL escolhe 2.0 se driver não tem 3.0. */
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  SDL_Window *win = SDL_CreateWindow(title ? title : "ULTRAKILL",
                                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     w, h,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!win) {
    fprintf(stderr, "SDL_CreateWindow: %s (tente SDL_VIDEODRIVER=x11 / kmsgrab)\n", SDL_GetError());
    return SHIM_GL_NO_WINDOW;
  }
  out->sdl = win;
  out->width = w; out->height = h;

  /* Puxa o handle nativo real (X11 Window ID, wl_surface*, etc). */
  SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version);
  if (!SDL_GetWindowWMInfo(win, &wmi)) {
    fprintf(stderr, "SDL_GetWindowWMInfo: %s\n", SDL_GetError());
  } else {
    switch (wmi.subsystem) {
#ifdef SDL_VIDEO_DRIVER_X11
      case SDL_SYSWM_X11: out->native_window = (void*)(uintptr_t)wmi.info.x11.window; break;
#endif
#ifdef SDL_VIDEO_DRIVER_WAYLAND
      case SDL_SYSWM_WAYLAND: out->native_window = wmi.info.wl.surface; break;
#endif
      default: out->native_window = NULL; break;
    }
  }

  if (load_egl() != 0) return SHIM_GL_NO_EGL;

  out->egl_display = p_eglGetDisplay((EGLNativeDisplayType)out->native_window);
  if (out->egl_display == EGL_NO_DISPLAY) {
    fprintf(stderr, "eglGetDisplay falhou (err=0x%x)\n", p_eglGetError());
    return SHIM_GL_NO_EGL;
  }
  EGLint major=0, minor=0;
  if (!p_eglInitialize(out->egl_display, &major, &minor)) {
    fprintf(stderr, "eglInitialize falhou (err=0x%x)\n", p_eglGetError());
    return SHIM_GL_NO_EGL;
  }
  fprintf(stderr, "EGL %d.%d: %s\n", major, minor,
          p_eglQueryString ? p_eglQueryString(out->egl_display, 0x3053 /*VENDOR*/) : "?");
  p_eglBindAPI(EGL_OPENGL_ES_API);

  const EGLint cfgAttr[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
    EGL_NONE
  };
  EGLConfig cfg; EGLint nCfg = 0;
  if (!p_eglChooseConfig(out->egl_display, cfgAttr, &cfg, 1, &nCfg) || nCfg < 1) {
    /* fallback: sem ES3 */
    EGLint cfgAttr2[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    if (!p_eglChooseConfig(out->egl_display, cfgAttr2, &cfg, 1, &nCfg) || nCfg < 1) {
      fprintf(stderr, "eglChooseConfig falhou (err=0x%x)\n", p_eglGetError());
      return SHIM_GL_CTX_FAIL;
    }
  }
  const EGLint ctxAttr[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
  out->egl_context = p_eglCreateContext(out->egl_display, cfg, EGL_NO_CONTEXT, ctxAttr);
  if (!out->egl_context) {
    const EGLint ctxAttr2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    out->egl_context = p_eglCreateContext(out->egl_display, cfg, EGL_NO_CONTEXT, ctxAttr2);
  }
  if (!out->egl_context) return SHIM_GL_CTX_FAIL;

  out->egl_surface = p_eglCreateWindowSurface(out->egl_display, cfg, out->native_window, NULL);
  if (!out->egl_surface) {
    fprintf(stderr, "eglCreateWindowSurface falhou (err=0x%x)\n", p_eglGetError());
    return SHIM_GL_CTX_FAIL;
  }
  if (!p_eglMakeCurrent(out->egl_display, out->egl_surface, out->egl_surface, out->egl_context)) {
    fprintf(stderr, "eglMakeCurrent falhou (err=0x%x)\n", p_eglGetError());
    return SHIM_GL_CTX_FAIL;
  }

  /* monta fake ANativeWindow (definido em shim_native_window.h): */
  g_win.magic        = SHIM_ANW_MAGIC;
  g_win.refs         = 1;
  g_win.format       = 1;                        /* RGBA_8888 */
  g_win.w = w; g_win.h = h;
  g_win.native_h     = out->native_window;
  g_win.egl_surface  = out->egl_surface;
  g_win.egl_config   = (void*)cfg;
  g_win.egl_display  = out->egl_display;
  g_win.on_swap      = NULL;
  out->fake = (struct ANativeWindow*)&g_win;

  fprintf(stderr, "bridge_gl: ok %dx%d, GL ver=%s\n", w, h, gl_version_probe());
  return SHIM_GL_OK;
}

void shim_gl_swap(shim_gl_ctx *c) {
  if (c && c->egl_display && c->egl_surface && p_eglSwapBuffers)
    p_eglSwapBuffers(c->egl_display, c->egl_surface);
}

void shim_gl_shutdown(shim_gl_ctx *c) {
  if (!c) return;
  if (c->sdl) SDL_DestroyWindow(c->sdl);
  SDL_Quit();
}

/* helper local - evita <GLES/gl.h>: */
static const char *gl_version_probe(void) {
  static const char *(*gs)(unsigned) = NULL;
  if (!gs) {
    void *h = dlopen("libGLESv2.so.2", RTLD_NOW);
    if (!h) h = dlopen("libGLESv2.so", RTLD_NOW);
    if (h) gs = (const char*(*)(unsigned))dlsym(h, "glGetString");
  }
  if (!gs) return "?";
  const char *v = gs(0x1F02 /*GL_VERSION*/);
  return v ? v : "?";
}
