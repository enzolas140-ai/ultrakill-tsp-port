/* bridge_egl_shim.c - libEGL.so transparente para o PowerVR DDK real.
 *
 * Este arquivo compila como `libEGL.so` (sem SOVERSION) e é instalado em
 * `libs/{arch}/`. O LD_LIBRARY_PATH do launcher coloca esse diretório na
 * frente, então quando o `libunity.so` do APK roda `NEEDED: libEGL.so` o
 * loader dynamic resolve para cá - NÃO para o driver real.
 *
 * Papel do shim:
 *   1. No primeiro uso, dlopen o driver real (`libEGL.so.1` do sistema) e
 *      cacheia ponteiros para todas as funções EGL.
 *   2. Encaminhar toda chamada EGL para o driver real - comportamento é
 *      idêntico ao de não ter shim nenhum.
 *   3. EXCETO `eglCreateWindowSurface`. Quando o `win` é nosso
 *      `shim_native_window*` (identificado pelo magic ANW\0), NÃO criamos
 *      uma segunda surface: devolvemos a surface que `bridge_gl.c` já
 *      criou antes (via dlopen + RTLD_LOCAL do `libEGL.so.1`, pulando este
 *      shim). Isto mantém a consistência do `eglMakeCurrent` que o bridge
 *      já executou - se chamássemos eglCreateWindowSurface real de novo
 *     íamos um segundo EGLSurface e o contexto atual ficaria órfão.
 *   4. EXCETO `eglDestroySurface`. Se o `surface` é o nosso pré-criado,
 *      pulamos o destroy - senão o Unity derruba a surface na hora do
 *      `OnApplicationPause` e depois não consegue mais swap.
 *
 * Porque não LD_PRELOAD:
 *   LD_LIBRARY_PATH já é suficiente para shadow por NEEDED name. LD_PRELOAD
 *   é mais global do que queremos (afetaria o compositor também). Mantemos
 *   o shim com escopo restrito ao processo do jogo.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "shim_native_window.h"

/* -------- typedefs EGL (mesmo jogo de bridge_gl.c - não usamos <EGL/egl.h>
 *          para não forçar o build host a ter cabecalhos Mesa/DDK) -------- */
typedef void      *EGLDisplay;
typedef void      *EGLContext;
typedef void      *EGLSurface;
typedef void      *EGLConfig;
typedef void      *EGLClientBuffer;
typedef int        EGLboolean_alias;    /* só p/ não repetir literal 'int' */
typedef unsigned int EGLint_alias;
typedef int        EGLint;
typedef unsigned char EGLBoolean;
typedef unsigned int EGLenum;
typedef uint64_t   EGLTime;
typedef void      *EGLNativeDisplayType;
typedef void      *EGLNativeWindowType;
typedef void      *EGLNativePixmapType;
typedef int        khronos_intptr_t;
typedef unsigned int khronos_uintptr_t;
typedef signed long long khronos_ssize_t;
typedef unsigned long long khronos_usize_t;

#define EGL_NO_DISPLAY       ((EGLDisplay)0)
#define EGL_NO_CONTEXT       ((EGLContext)0)
#define EGL_NO_SURFACE       ((EGLSurface)0)
#define EGL_TRUE             ((EGLBoolean)1)
#define EGL_FALSE            ((EGLBoolean)0)
#define EGL_SUCCESS          0x3000
#define EGL_BAD_NATIVE_WINDOW 0x300E
#define EGL_BAD_MATCH        0x3008
#define EGL_NO_DISPLAY_X     EGL_NO_DISPLAY

/* -------- estado do shim -------- */
static void *g_real;
static pthread_once_t g_once = PTHREAD_MUTEX_INITIALIZER;
static int g_loaded_ok;

/* accessor exportado pelo bridge_main (via bridge_gl.c + -rdynamic) */
typedef struct shim_native_window *(*GetANW_fn)(void);
static GetANW_fn g_get_anw;

static void *sym(const char *name) {
  if (!g_real) return NULL;
  void *p = dlsym(g_real, name);
  if (!p) fprintf(stderr, "shim/libEGL: %s ausente no driver real\n", name);
  return p;
}

__attribute__((constructor))
static void shim_preinit(void) {
  /* Não carregamos o real aqui: construtores rodam antes de main, e se
   * o loader ainda está resolvendo símbolos do bridge isso vira
   * deadlock. Deixamos lazy (ensure_real()). */
}

static void ensure_real(void) {
  if (g_loaded_ok) return;
  pthread_mutex_lock((pthread_mutex_t*)&g_once);
  if (g_loaded_ok) { pthread_mutex_unlock((pthread_mutex_t*)&g_once); return; }
  /* Lista de caminhos - NUNCA inclui "libEGL.so" (sem .1) para não
   * recursar em nós mesmos. */
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
  if (override && *override) g_real = dlopen(override, RTLD_NOW | RTLD_LOCAL);
  for (int i = 0; paths[i] && !g_real; i++)
    g_real = dlopen(paths[i], RTLD_NOW | RTLD_LOCAL);
  if (!g_real) {
    fprintf(stderr, "shim/libEGL: NAO ENCONTROU driver real.\n");
    fprintf(stderr, "  defina ULTRAKILL_REAL_LIBEGL=/caminho/absoluto\n");
    fprintf(stderr, "  dlerror=%s\n", dlerror());
  } else {
    fprintf(stderr, "shim/libEGL: driver real carregado.\n");
  }
  /* accessor do g_win: */
  g_get_anw = (GetANW_fn)dlsym(RTLD_DEFAULT, "shim_get_native_window");
  g_loaded_ok = 1;
  pthread_mutex_unlock((pthread_mutex_t*)&g_once);
}

static struct shim_native_window *current_anw(void) {
  return g_get_anw ? g_get_anw() : NULL;
}

/* =========================================================================
 *  FORWARDERS - um por função EGL pública. Cada um faz lazy dlsym na
 *  primeira invocação. Assinaturas seguem EGL 1.0/1.1/1.2/1.3/1.4/1.5.
 * ========================================================================= */

#define FWD(ret, name, proto, callargs, fallback)                          \
    static ret (*r_##name) proto;                                          \
    __attribute__((visibility("default")))                                 \
    ret name proto {                                                       \
      ensure_real();                                                       \
      if (!r_##name) r_##name = (ret (*) proto) sym(#name);                \
      if (!r_##name) return (ret)fallback;                                 \
      return r_##name callargs;                                            \
    }

/* EGL 1.0 / 1.1 */
FWD(const char*, eglQueryString,
    (EGLDisplay dpy, int name), (dpy, name), (const char*)0)
FWD(void*, eglGetProcAddress, (const char *name), (name), (void*)0)
FWD(EGLBoolean, eglInitialize,
    (EGLDisplay dpy, EGLint *mj, EGLint *mn), (dpy, mj, mn), EGL_FALSE)
FWD(EGLBoolean, eglTerminate, (EGLDisplay dpy), (dpy), EGL_FALSE)
FWD(EGLint,     eglGetError, (void), (), EGL_SUCCESS)
FWD(EGLDisplay, eglGetDisplay, (EGLNativeDisplayType nt), (nt), EGL_NO_DISPLAY)
FWD(EGLBoolean, eglBindAPI, (EGLenum api), (api), EGL_FALSE)
FWD(EGLenum,    eglQueryAPI, (void), (), 0)
FWD(EGLBoolean, eglChooseConfig,
    (EGLDisplay dpy, const EGLint *att, EGLConfig *cfgs, EGLint sz, EGLint *n),
    (dpy, att, cfgs, sz, n), EGL_FALSE)
FWD(EGLBoolean, eglGetConfigAttrib,
    (EGLDisplay dpy, EGLConfig cfg, int attr, EGLint *val), (dpy, cfg, attr, val), EGL_FALSE)
FWD(EGLBoolean, eglGetConfigs,
    (EGLDisplay dpy, EGLConfig *cfgs, EGLint sz, EGLint *n), (dpy, cfgs, sz, n), EGL_FALSE)
FWD(EGLContext, eglCreateContext,
    (EGLDisplay dpy, EGLConfig cfg, EGLContext share, const EGLint *att),
    (dpy, cfg, share, att), EGL_NO_CONTEXT)
FWD(EGLBoolean, eglDestroyContext, (EGLContext ctx), (ctx), EGL_FALSE)
FWD(EGLBoolean, eglMakeCurrent,
    (EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx),
    (dpy, draw, read, ctx), EGL_FALSE)
FWD(EGLContext, eglGetCurrentContext, (void), (), EGL_NO_CONTEXT)
FWD(EGLSurface, eglGetCurrentSurface, (int re), (re), EGL_NO_SURFACE)
FWD(EGLDisplay, eglGetCurrentDisplay, (void), (), EGL_NO_DISPLAY)
FWD(EGLBoolean, eglQueryContext,
    (EGLDisplay dpy, EGLContext ctx, int attr, EGLint *val), (dpy, ctx, attr, val), EGL_FALSE)
FWD(EGLBoolean, eglWaitGL, (void), (), EGL_FALSE)
FWD(EGLBoolean, eglWaitNative, (int eng), (eng), EGL_FALSE)
/* eglSwapBuffers tem wrapper especial mais abaixo. */
FWD(EGLBoolean, eglSwapInterval, (EGLDisplay dpy, EGLint i), (dpy, i), EGL_FALSE)
FWD(EGLBoolean, eglCopyBuffers,
    (EGLDisplay dpy, EGLSurface s, EGLNativePixmapType p), (dpy, s, p), EGL_FALSE)

/* EGL 1.1 + 1.2 - query surface */
FWD(EGLBoolean, eglQuerySurface,
    (EGLDisplay dpy, EGLSurface s, int attr, EGLint *val), (dpy, s, attr, val), EGL_FALSE)
FWD(EGLBoolean, eglSurfaceAttrib,
    (EGLDisplay dpy, EGLSurface s, int attr, EGLint val), (dpy, s, attr, val), EGL_FALSE)

/* EGL 1.2 - eglDestroySurface tem wrapper especial mais abaixo. */

FWD(EGLBoolean, eglReleaseThread, (void), (), EGL_FALSE)
FWD(EGLBoolean, eglWaitClient, (void), (), EGL_FALSE)

/* EGL 1.3 - syncs */
FWD(void*, eglCreateSync, (EGLDisplay dpy, int type, const EGLint *att), (dpy, type, att), NULL)
FWD(EGLBoolean, eglDestroySync, (EGLDisplay dpy, void *sync), (dpy, sync), EGL_FALSE)
FWD(int, eglClientWaitSync, (EGLDisplay dpy, void *sync, int flags, EGLTime t), (dpy, sync, flags, t), 0x3082 /*SIGNALED*/)
FWD(EGLBoolean, eglGetSyncAttrib, (EGLDisplay dpy, void *sync, int attr, EGLint *val), (dpy, sync, attr, val), EGL_FALSE)

/* EGL 1.4 */
FWD(EGLDisplay, eglGetPlatformDisplay,
    (EGLenum plat, void *ndisp, const EGLint *att), (plat, ndisp, att), EGL_NO_DISPLAY)
FWD(EGLDisplay, eglGetPlatformDisplayEXT,
    (EGLenum plat, void *ndisp, const EGLint *att), (plat, ndisp, att), EGL_NO_DISPLAY)

/* EGL 1.5 */
FWD(EGLContext, eglCreateContextAttribsARB,
    (EGLDisplay dpy, EGLConfig cfg, EGLContext share, const EGLint *att),
    (dpy, cfg, share, att), EGL_NO_CONTEXT)

/* KHR image (Unity usa para shared textures com camera; provavelmente
 * nulo aqui mas declaramos p/ linker nao reclamar) */
FWD(void*, eglCreateImageKHR,
    (EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buf, const EGLint *att),
    (dpy, ctx, target, buf, att), NULL)
FWD(EGLBoolean, eglDestroyImageKHR, (EGLDisplay dpy, void *img), (dpy, img), EGL_FALSE)

/* -------- Pixmap e Pbuffer surfaces -------- */
FWD(EGLSurface, eglCreatePbufferSurface,
    (EGLDisplay dpy, EGLConfig cfg, const EGLint *att), (dpy, cfg, att), EGL_NO_SURFACE)
FWD(EGLSurface, eglCreatePixmapSurface,
    (EGLDisplay dpy, EGLConfig cfg, EGLNativePixmapType pm, const EGLint *att),
    (dpy, cfg, pm, att), EGL_NO_SURFACE)

/* -------- Record: quantas trocas passaram pela shim (diagnóstico) -------- */
static volatile unsigned long g_shim_swaps;

/* =========================================================================
 *  CASOS ESPECIAIS - eglCreateWindowSurface / eglDestroySurface
 * ========================================================================= */

typedef EGLSurface (*PFN_real_CWS)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
typedef EGLBoolean (*PFN_real_DS)(EGLDisplay, EGLSurface);

__attribute__((visibility("default")))
EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativeWindowType win,
                                  const EGLint *attrib_list) {
  ensure_real();
  static PFN_real_CWS real_cws;
  if (!real_cws) real_cws = (PFN_real_CWS)sym("eglCreateWindowSurface");
  if (!real_cws) return EGL_NO_SURFACE;

  /* Detecta nosso shim_native_window. Se for, devolve a surface que o
   * bridge_gl.c já criou (bypassando o real EGL). Assim o contexto que
   * o bridge já fez eglMakeCurrent continua válido. */
  if (shim_anw_is_shim((void*)win)) {
    struct shim_native_window *w = (struct shim_native_window*)win;
    fprintf(stderr,
            "shim/egl: eglCreateWindowSurface(ANW*) -> devolvendo surface pre-criada %p (%dx%d)\n",
            w->egl_surface, w->w, w->h);
    return (EGLSurface)w->egl_surface;
  }

  /* Caso contrário: Unity (ou outro chamador) passou um handle nativo real
   * - encaminha direto pro driver. */
  fprintf(stderr, "shim/egl: eglCreateWindowSurface(nativo %p) -> encaminha p/ real\n",
          (void*)win);
  return real_cws(dpy, config, win, attrib_list);
}

__attribute__((visibility("default")))
EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
  ensure_real();
  static PFN_real_DS real_ds;
  if (!real_ds) real_ds = (PFN_real_DS)sym("eglDestroySurface");
  if (!real_ds) return EGL_FALSE;

  /* Protege a surface pré-criada pelo bridge_gl - sem ela o contexto
   * atual fica inválido e o Unity crasha no próximo swap. */
  struct shim_native_window *w = current_anw();
  if (w && surface && (void*)surface == w->egl_surface) {
    fprintf(stderr, "shim/egl: eglDestroySurface(precrida) -> SKIP (protegida)\n");
    return EGL_TRUE;
  }
  return real_ds(dpy, surface);
}

__attribute__((visibility("default")))
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
  ensure_real();
  static EGLBoolean (*real_sb)(EGLDisplay, EGLSurface);
  if (!real_sb) real_sb = (EGLBoolean(*)(EGLDisplay, EGLSurface))sym("eglSwapBuffers");
  if (!real_sb) return EGL_FALSE;
  g_shim_swaps++;
  return real_sb(dpy, surface);
}

/* -------- debug hook (opcional) --------
 *  `env ULTRAKILL_EGL_STATS=1` antes de rodar faz o shim, no destroy do
 *  processo, reportar quantos swaps passaram por aqui. */
__attribute__((destructor))
static void shim_report(void) {
  if (getenv("ULTRAKILL_EGL_STATS"))
    fprintf(stderr, "shim/libEGL: swaps=%lu\n", g_shim_swaps);
}
