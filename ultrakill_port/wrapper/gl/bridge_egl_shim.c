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
#define _GNU_SOURCE   /* RTLD_DEFAULT no glibc <= 2.33 (bullseye/ArkOS) */
#include <dlfcn.h>
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void *)0)
#endif
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
  /* Lista de caminhos - só paths ABSOLUTOS. Se colocássemos
   * "libEGL.so.1" (sem caminho) o loader pode achar NOSSO shim
   * (copiado como libEGL.so.1) e recursar em ensure_real. */
  static const char *paths[] = {
    "/usr/lib/libEGL.so.1",
    "/usr/lib/aarch64-linux-gnu/libEGL.so.1",
    "/usr/lib/arm-linux-gnueabihf/libEGL.so.1",
    "/usr/lib/x86_64-linux-gnu/libEGL.so.1",
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
    FWD_AS(ret, name, #name, proto, callargs, fallback)
/* FWD_AS: mesmo FWD, mas procurando um NOME DIFERENTE no driver real.
 * Obrigatorio para os wrappers *_real: o driver Khronos exporta
 * "eglGetProcAddress"/"eglGetPlatformDisplay", NAO "eglGetProcAddress_real"
 * - com o FWD simples o dlsym falhava SEMPRE, o shim devolvia NULL para
 * toda EXT pedida e o Unity chamava ponteiro nulo (SIGSEGV visto no
 * device logo apos nativeResume). */
#define FWD_AS(ret, name, realsym, proto, callargs, fallback)              \
    static ret (*r_##name) proto;                                          \
    __attribute__((visibility("default")))                                 \
    ret name proto {                                                       \
      ensure_real();                                                       \
      if (!r_##name) r_##name = (ret (*) proto) sym(realsym);              \
      if (!r_##name) return (ret)fallback;                                 \
      return r_##name callargs;                                            \
    }

/* EGL 1.0 / 1.1 */
FWD(const char*, eglQueryString,
    (EGLDisplay dpy, int name), (dpy, name), (const char*)0)
/* -------- eglGetProcAddress interceptado --------
 * SDL2 chama eglGetProcAddress("eglQueryDevicesEXT") para descobrir ext
 * functions ANTES de dlsym. Se so encaminharmos ao real, o real nao conhece
 * nossos sintetizados e retorna NULL -> SDL diz "missing". Resolvemos
 * dando match das EXT que nos sintetizamos em bridge_egl_shim.c. */
FWD_AS(void*, eglGetProcAddress_real, "eglGetProcAddress",
       (const char *name), (name), (void*)0)
typedef void (*PFN_eglGetProcAddress_)(const char *);
static unsigned long g_gp_unresolved = 0;
__attribute__((visibility("default")))
void (*eglGetProcAddress(const char *name))(void) {
  extern void (*shim_eglGetProcAddress_dispatch(const char *))(void);
  void (*ours)(void) = shim_eglGetProcAddress_dispatch(name);
  if (ours) return ours;
  void *p = eglGetProcAddress_real(name);
  /* Fallbacks: alguns nomes pedidos (egl*EXT / gl* core) estao EXPORTADOS
   * como simbolo nas libs glvnd/GLESv2 mas o eglGetProcAddress do driver
   * nao os conhece. Devolver NULL aqui = Unity pula no vazio. */
  if (!p && name) p = dlsym(RTLD_DEFAULT, name);
  if (!p && name) {
    static void *gh;   /* libGLESv2 real (dispatcher glvnd ou nosso stack) */
    if (!gh) {
      const char *gv = getenv("ULTRAKILL_REAL_LIBGLESV2");
      gh = dlopen((gv && *gv) ? gv : "libGLESv2.so.2", RTLD_NOW | RTLD_NOLOAD);
      if (!gh) gh = dlopen((gv && *gv) ? gv : "libGLESv2.so.2", RTLD_NOW);
    }
    if (gh) p = dlsym(gh, name);
  }
  if (!p && name) {
    /* loga (com teto) o que nao conseguimos resolver - e exatamente ai que
     * o Unity vai estourar, entao essa lista e o diagnostico do proximo log */
    if (g_gp_unresolved < 40)
      fprintf(stderr, "shim/egl: eglGetProcAddress(\"%s\") -> NULL\n", name);
    g_gp_unresolved++;
  }
  return (void (*)(void))p;
}
FWD(EGLBoolean, eglInitialize,
    (EGLDisplay dpy, EGLint *mj, EGLint *mn), (dpy, mj, mn), EGL_FALSE)
FWD(EGLBoolean, eglTerminate, (EGLDisplay dpy), (dpy), EGL_FALSE)
FWD(EGLint,     eglGetError, (void), (), EGL_SUCCESS)

/* ---- normalizacao SURFACELESS (R36S/ArkOS4Clone) ----
 * SDL kmsdrm passa gbm_device* ao eglGetDisplay; o loader gbm do mesa dessa
 * firmware crasha (strcmp ponteiro lixo). Com ULTRAKILL_EGL_SURFACELESS=1
 * TODOS (SDL, Unity, bridge) recebem o mesmo display surfaceless criado com
 * eglGetDisplay(NULL) - comprovado vivo pelo egltest (ES 3.2 swrast). */
static int shim_surfaceless_forced(void) {
  const char *e = getenv("ULTRAKILL_EGL_SURFACELESS");
  return e && *e && strcmp(e, "0") != 0;
}
static EGLDisplay shim_surfaceless_display(void) {
  static EGLDisplay cached = EGL_NO_DISPLAY;
  if (cached == EGL_NO_DISPLAY) {
    typedef EGLDisplay (*gdfn)(EGLNativeDisplayType);
    gdfn g = (gdfn)sym("eglGetDisplay");
    if (g) cached = g(NULL);
  }
  return cached;
}

__attribute__((visibility("default")))
EGLDisplay eglGetDisplay(EGLNativeDisplayType native) {
  ensure_real();
  static EGLDisplay (*rg)(EGLNativeDisplayType);
  if (!rg) rg = (EGLDisplay (*)(EGLNativeDisplayType))sym("eglGetDisplay");
  if (!rg) return EGL_NO_DISPLAY;
  if (shim_surfaceless_forced()) {
    EGLDisplay d = shim_surfaceless_display();
    if (d) return d;
  }
  return rg(native);
}
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
FWD_AS(EGLDisplay, eglGetPlatformDisplay_real, "eglGetPlatformDisplay",
       (EGLenum plat, void *ndisp, const EGLint *att), (plat, ndisp, att), EGL_NO_DISPLAY)
__attribute__((visibility("default")))
EGLDisplay eglGetPlatformDisplay(EGLenum plat, void *ndisp, const EGLint *att) {
  ensure_real();
  if (shim_surfaceless_forced()) {
    EGLDisplay d = shim_surfaceless_display();
    if (d) return d;
  }
  return eglGetPlatformDisplay_real(plat, ndisp, att);
}
/* eglGetPlatformDisplayEXT definido logo abaixo (sintetiza o device EXT p/ SDL offscreen) */

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

/* =========================================================================
 *  EXT_device_enumeration - SINTETIZADO
 *
 *  SDL2 com video driver "offscreen" exige eglQueryDevicesEXT para obter
 *  um EGLDevice sem display nativo. Muitos DDKs embedded (PowerVR em
 *  handheld) nao expoe esta extensao - so eglGetDisplay(EGL_DEFAULT).
 *  Fornecemos uma sintese: devolvemos UMA "device" sentinel, e no
 *  eglGetPlatformDisplayEXT mapeamos de volta pra EGL_DEFAULT_DISPLAY
 *  quando recebermos o sentinel. Sem isso SDL offscreen retorna
 *  "eglQueryDevicesEXT is missing" e shim_gl_init falha.
 * ========================================================================= */
#define SHIM_EGL_DEVICE_SENTINEL ((void*)(uintptr_t)0xE0)
typedef EGLBoolean (*PFN_qdevs)(EGLint, void**, EGLint*);
typedef EGLDisplay  (*PFN_gpdEXT)(EGLenum, void*, const EGLint*);
typedef EGLBoolean  (*PFN_qdattr)(EGLDisplay, int, EGLint*);

__attribute__((visibility("default")))
EGLBoolean eglQueryDevicesEXT(EGLint max_devices, void **devices, EGLint *num_devices) {
  ensure_real();
  static PFN_qdevs real; if (!real) real = (PFN_qdevs)sym("eglQueryDevicesEXT");
  if (real) {
    EGLBoolean r = real(max_devices, devices, num_devices);
    if (r == EGL_TRUE && (!num_devices || *num_devices > 0)) return EGL_TRUE;
    /* real devolveu TRUE mas 0 devices - cai no nosso fallback */
  }
  fprintf(stderr, "shim/egl: eglQueryDevicesEXT sintetizado (1 device)\n");
  if (num_devices) *num_devices = 1;
  if (devices && max_devices > 0) devices[0] = SHIM_EGL_DEVICE_SENTINEL;
  return EGL_TRUE;
}

__attribute__((visibility("default")))
EGLBoolean eglQueryDeviceStringEXT(void *dev, const char *name) {
  (void)dev; (void)name; return EGL_TRUE;
}

__attribute__((visibility("default")))
EGLDisplay eglGetPlatformDisplayEXT(EGLenum platform, void *native_display,
                                     const EGLint *attrib_list) {
  ensure_real();
  if (shim_surfaceless_forced()) {
    EGLDisplay d = shim_surfaceless_display();
    if (d) return d;
  }
  static PFN_gpdEXT real; if (!real) real = (PFN_gpdEXT)sym("eglGetPlatformDisplayEXT");
  /* traduz o sentinel pra EGL_DEFAULT_DISPLAY (0) - ai o driver real aceita */
  void *nd = (native_display == SHIM_EGL_DEVICE_SENTINEL) ? NULL : native_display;
  if (real) {
    EGLDisplay d = real(platform, nd, attrib_list);
    if (d) return d;
  }
  static PFN_gpdEXT real2;   /* tenta a variante sem EXT */
  if (!real2) real2 = (PFN_gpdEXT)sym("eglGetPlatformDisplay");
  if (real2) return real2(platform, nd, attrib_list);
  /* ultimo recurso: eglGetDisplay(EGL_DEFAULT_DISPLAY) */
  return eglGetDisplay((EGLNativeDisplayType)0);
}

__attribute__((visibility("default")))
EGLBoolean eglQueryDisplayAttribEXT(EGLDisplay dpy, int attribute, EGLint *value) {
  ensure_real();
  static PFN_qdattr real; if (!real) real = (PFN_qdattr)sym("eglQueryDisplayAttribEXT");
  if (real) return real(dpy, attribute, value);
  /* fallback: aceita tudo */
  if (value) *value = 0;
  return EGL_TRUE;
}

/* -------- Pixmap e Pbuffer surfaces -------- */
FWD(EGLSurface, eglCreatePbufferSurface,
    (EGLDisplay dpy, EGLConfig cfg, const EGLint *att), (dpy, cfg, att), EGL_NO_SURFACE)
FWD(EGLSurface, eglCreatePixmapSurface,
    (EGLDisplay dpy, EGLConfig cfg, EGLNativePixmapType pm, const EGLint *att),
    (dpy, cfg, pm, att), EGL_NO_SURFACE)

/* -------- Record: quantas trocas passaram pela shim (diagnóstico) -------- */
static volatile unsigned long g_shim_swaps;

/* Superficies PBUFFER que NOS criamos de resposta ao Unity. Elas tem que
 * ser "imortais": se o Unity chamar eglDestroySurface numa delas antes do
 * ultimo eglMakeCurrent, o proximo draw pega surface pendurada. */
#define SHIM_MAX_TRACKED 16
static void *g_tracked[SHIM_MAX_TRACKED];
static int   g_tracked_n;
static pthread_mutex_t g_tracked_mx = PTHREAD_MUTEX_INITIALIZER;
static void track_surface(void *s) {
  if (!s) return;
  pthread_mutex_lock(&g_tracked_mx);
  if (g_tracked_n < SHIM_MAX_TRACKED) g_tracked[g_tracked_n++] = s;
  pthread_mutex_unlock(&g_tracked_mx);
}
static int is_tracked(void *s) {
  int r = 0;
  if (!s) return 0;
  pthread_mutex_lock(&g_tracked_mx);
  for (int i = 0; i < g_tracked_n; i++) if (g_tracked[i] == s) { r = 1; break; }
  pthread_mutex_unlock(&g_tracked_mx);
  return r;
}

#define SHIM_EGL_WIDTH  0x3057
#define SHIM_EGL_HEIGHT 0x3056

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

  /* >>> NOVO (rota PowerVR / TrimUI Smart Pro): se o bridge_gl conseguiu uma
   * WINDOW surface de verdade (criada pelo SDL sobre GBM/KMS ou compositor),
   * devolvemos ELA - nao um pbuffer. Motivo: quem faz o page flip no painel e
   * essa surface; um pbuffer e offscreen, entao o jogo renderizaria para uma
   * memoria que ninguém apresenta (a tela preta com frames subindo do log
   * antigo). Aceitamos tambem o caso em que o Unity passa NULL/lixo (ele nunca
   * teve uma Surface Java neste port). */
  {
    struct shim_native_window *w2 = current_anw();
    if (w2 && w2->surface_is_window && w2->egl_surface) {
      fprintf(stderr,
              "shim/egl: eglCreateWindowSurface(win=%p) -> devolvendo WINDOW surface real do SDL %p (%dx%d) [swap do Unity = flip no painel]\n",
              (void*)win, w2->egl_surface, w2->w, w2->h);
      return (EGLSurface)w2->egl_surface;
    }
  }

  /* win NAO e nosso ANW: no caminho R36S (JNI_OnLoad -> initJni, sem
   * ANativeActivity_onCreate) o Unity nunca recebeu uma janela de verdade e
   * chama eglCreateWindowSurface(dpy, cfg, NULL/lixo). MESA devolve
   * EGL_NO_SURFACE com esse native window -> o GFX device do Unity fica sem
   * surface e o crash vem no nativeResume. Entao: criamos um PBUFFER real
   * com o CONFIG QUE O PROPRIO UNITY pediu (mesma display, compativel com o
   * contexto dele) e devolvemos como se fosse a window surface. Pbuffer e
   * um render target perfeitamente valido para eglMakeCurrent. */
  {
    struct shim_native_window *w = current_anw();
    int pw = w && w->w > 0 ? w->w : 480;
    int ph = w && w->h > 0 ? w->h : 270;
    const EGLint pattr[] = { SHIM_EGL_WIDTH, pw, SHIM_EGL_HEIGHT, ph, 0x3038 /*NONE*/ };
    EGLSurface ps = eglCreatePbufferSurface(dpy, config, pattr);
    if (ps) {
      track_surface(ps);
      fprintf(stderr,
              "shim/egl: eglCreateWindowSurface(nao-ANW %p) -> PBUFFER real %p %dx%d (config do chamador)\n",
              (void*)win, ps, pw, ph);
      return ps;
    }
    fprintf(stderr, "shim/egl: eglCreateWindowSurface(nao-ANW %p): pbuffer falhou (err=0x%x), cai na pre-criada\n",
            (void*)win, eglGetError());
    if (w && w->egl_surface) return (EGLSurface)w->egl_surface;
  }

  /* ultimo recurso: encaminha pro driver com o handle que veio. */
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
   * atual fica inválido e o Unity crasha no próximo swap. Protege tambem
   * os pbuffers que nos mesmos criamos no lugar das window surfaces. */
  struct shim_native_window *w = current_anw();
  if (surface && ((w && (void*)surface == w->egl_surface) || is_tracked(surface))) {
    fprintf(stderr, "shim/egl: eglDestroySurface(nossa) -> SKIP (protegida)\n");
    return EGL_TRUE;
  }
  return real_ds(dpy, surface);
}

/* ======== ULTRAKILL fix -Q: prova de carregamento ===============
 * O shim nao imprimiu UMA linha em 4 rodadas (nem 'shim/libEGL: swap'), entao
 * ou o libunity.so nao resolve libEGL em nos (pega o driver real direto), ou o
 * stderr nao esta sendo capturado. Este constructor responde a primeira parte no
 * proprio unity_probe.log: se aparecer 'CARREGADO', o shim esta no meio e o swap
 * do Unity simplesmente nunca acontece; se NAO aparecer, o present tem que ser
 * feito no probe e o shim e um beco sem saida - nada mais e chutado as cegas. */
#include <unistd.h>
__attribute__((constructor))
static void shim_ctor_report(void) {
  fprintf(stderr, "shim/libEGL: CARREGADO pid=%d ppid=%d (libEGL.so do port, nao o driver)\n",
          (int)getpid(), (int)getppid());
  fflush(stderr);
}

/* ==== ULTRAKILL fix -Q: present por framebuffer DENTRO do shim ==========
 * Motivo (log de 05/11 07:56): o probe conta `guest_swaps=0` para SEMPRE, porque o
 * libunity.so tem `NEEDED libEGL.so` = este shim; os swaps vao para o driver real e o
 * probe nunca os ve. E a thread do frame loop do probe NAO tem contexto GL
 * (SDL_GL_MakeCurrent rc=-1 'Unable to make EGL context current'), entao o readback
 * la e sempre todo zero. Logo depois do swap real, AQUI, o contexto do Unity esta
 * corrente nesta thread - e o unico sitio onde a imagem pode ser lida.
 * Ja esta comprovado neste cartao que escrever em /dev/fb0 APARECE no painel (o padrao
 * de teste RGB do probe apareceu na tela da ES), entao blit aqui = video.
 * ULTRAKILL_SHIM_FB=0 desliga (sem recompilar). */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

typedef void (*sfb_read_px)(int,int,int,int,unsigned,unsigned,void*);
typedef void (*sfb_get_iv)(unsigned,int*);
typedef void (*sfb_bind_fbo)(unsigned,unsigned);

static void *sfb_glsym(const char *name){
  static void *gh;
  void *p = dlsym(RTLD_DEFAULT, name);
  if (p) return p;
  if (!gh) {
    const char *gv = getenv("ULTRAKILL_REAL_LIBGLESV2");
    const char *cand[4];
    cand[0] = (gv && *gv) ? gv : "libGLESv2.so.2";
    cand[1] = (gv && *gv) ? gv : "libGLESv2.so";
    cand[2] = "libGLESv2.so.2";
    cand[3] = "libGLESv2.so";
    for (int i = 0; i < 4 && !gh; i++) gh = dlopen(cand[i], RTLD_NOW | RTLD_NOLOAD);
    for (int i = 0; i < 4 && !gh; i++) gh = dlopen(cand[i], RTLD_NOW);
  }
  return gh ? dlsym(gh, name) : NULL;
}

#define SFB_READ_FRAMEBUFFER       0x8CA8u
#define SFB_FRAMEBUFFER_BINDING    0x8CA6u
#define SFB_VIEWPORT               0x0BA2u
#define SFB_RGBA                   0x1908u
#define SFB_UNSIGNED_BYTE          0x1401u

static int   sfb_state;              /* 0 nova, 1 pronta, -1 desligada */
static int   sfb_fd = -1;
static unsigned char *sfb_map;
static size_t sfb_len;
static int   sfb_w, sfb_h, sfb_pitch, sfb_bpp = 32;
static int   sfb_ro = 16, sfb_go = 8, sfb_bo = 0, sfb_rl = 8, sfb_gl = 8, sfb_bl = 8;
static unsigned char *sfb_buf;
static size_t sfb_buf_len;
static int   sfb_src_w, sfb_src_h;   /* tamanho do ultimo readback util */
static unsigned long sfb_blits, sfb_empty;
static int   sfb_fbo_id = -1;        /* FBO descoberto (-1 = tenta padrao) */

static int sfb_setup(void){
  const char *dev = getenv("ULTRAKILL_FB_DEVICE");
  struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
  if (!dev || !*dev) dev = "/dev/fb0";
  sfb_fd = open(dev, O_RDWR | O_CLOEXEC);
  if (sfb_fd < 0) { fprintf(stderr, "shim/fb: open %s falhou: %s\n", dev, strerror(errno)); return -1; }
  if (ioctl(sfb_fd, FBIOGET_VSCREENINFO, &v) || ioctl(sfb_fd, FBIOGET_FSCREENINFO, &f)) {
    fprintf(stderr, "shim/fb: ioctl VS/FIS falhou: %s\n", strerror(errno)); return -1;
  }
  v.bits_per_pixel = 32; v.grayscale = 0;
  v.red.length = v.green.length = v.blue.length = 8;
  v.red.offset = 16; v.green.offset = 8; v.blue.offset = 0;
  v.transp.length = 0; v.transp.offset = 24;
  if (ioctl(sfb_fd, FBIOPUT_VSCREENINFO, &v) == 0) ioctl(sfb_fd, FBIOGET_VSCREENINFO, &v);
  sfb_w = (int)v.xres; sfb_h = (int)v.yres; sfb_pitch = (int)f.line_length; sfb_bpp = (int)v.bits_per_pixel;
  sfb_ro = (int)v.red.offset; sfb_go = (int)v.green.offset; sfb_bo = (int)v.blue.offset;
  sfb_rl = v.red.length ? (int)v.red.length : 8;
  sfb_gl = v.green.length ? (int)v.green.length : 8;
  sfb_bl = v.blue.length ? (int)v.blue.length : 8;
  size_t need = (size_t)f.line_length * (v.yres_virtual ? (size_t)v.yres_virtual : (size_t)v.yres);
  sfb_len = (f.smem_len && (size_t)f.smem_len < need) ? (size_t)f.smem_len : need;
  void *m = mmap(0, sfb_len, PROT_READ | PROT_WRITE, MAP_SHARED, sfb_fd, 0);
  if (m == MAP_FAILED) { fprintf(stderr, "shim/fb: mmap %zu falhou: %s\n", sfb_len, strerror(errno)); return -1; }
  sfb_map = (unsigned char *)m;
  sfb_buf_len = (size_t)1280 * 720 * 4;
  sfb_buf = (unsigned char *)malloc(sfb_buf_len);
  if (!sfb_buf) return -1;
  fprintf(stderr, "shim/fb: %dx%d bpp=%d pitch=%d len=%zu (buffer ate 1280x720)\n",
          sfb_w, sfb_h, sfb_bpp, sfb_pitch, sfb_len);
  return 1;
}

static int sfb_has_content(const unsigned char *b, size_t n){
  for (size_t k = 0; k < n; k += 997u) if (b[k]) return 1;
  return 0;
}

static void sfb_blit(const unsigned char *rgba, int sw, int sh){
  for (int y = 0; y < sfb_h; y++) {
    int sy = sh > 0 ? (int)((long long)y * sh / sfb_h) : 0;
    if (sy >= sh) sy = sh - 1;
    if (sy < 0) sy = 0;
    if ((size_t)y * (size_t)sfb_pitch + (size_t)sfb_w * ((size_t)sfb_bpp / 8u) > sfb_len) break;
    const unsigned char *sr = rgba + (size_t)sy * (size_t)sw * 4u;
    unsigned char *dr = sfb_map + (size_t)y * (size_t)sfb_pitch;
    for (int x = 0; x < sfb_w; x++) {
      int sx = sw > 0 ? (int)((long long)x * sw / sfb_w) : 0;
      if (sx >= sw) sx = sw - 1;
      if (sx < 0) sx = 0;
      const unsigned char *p = sr + (size_t)sx * 4u;
      if (sfb_bpp == 32) {
        unsigned r = p[0] >> (8 - sfb_rl), g = p[1] >> (8 - sfb_gl), b = p[2] >> (8 - sfb_bl);
        ((unsigned *)dr)[x] = (r << sfb_ro) | (g << sfb_go) | (b << sfb_bo);
      } else {
        unsigned r = p[0] >> (8 - sfb_rl), g = p[1] >> (8 - sfb_gl), b = p[2] >> (8 - sfb_bl);
        ((unsigned short *)dr)[x] = (unsigned short)((r << sfb_ro) | (g << sfb_go) | (b << sfb_bo));
      }
    }
  }
}

static void sfb_present(void){
  const char *off = getenv("ULTRAKILL_SHIM_FB");
  if (off && (!strcmp(off, "0") || !strcmp(off, "off"))) return;
  if (!sfb_state) sfb_state = sfb_setup();
  if (sfb_state < 0 || !sfb_map || !sfb_buf) return;
  sfb_read_px rd = (sfb_read_px)sfb_glsym("glReadPixels");
  sfb_get_iv  gi = (sfb_get_iv)sfb_glsym("glGetIntegerv");
  sfb_bind_fbo bf = (sfb_bind_fbo)sfb_glsym("glBindFramebuffer");
  if (!rd) { fprintf(stderr, "shim/fb: glReadPixels ausente; desligando\n"); sfb_state = -1; return; }
  int vp[4] = { 0, 0, 0, 0 };
  if (gi) gi(SFB_VIEWPORT, vp);
  int sw = vp[2] > 0 ? vp[2] : (sfb_src_w > 0 ? sfb_src_w : 640);
  int sh = vp[3] > 0 ? vp[3] : (sfb_src_h > 0 ? sfb_src_h : 360);
  if ((size_t)sw * (size_t)sh * 4u > sfb_buf_len) { sw = 640; sh = 360; }
  if (sw <= 0 || sh <= 0) return;
  int saved = 0;
  if (bf && gi) gi(SFB_FRAMEBUFFER_BINDING, &saved);
  size_t n = (size_t)sw * (size_t)sh * 4u;
  int ok = 0;
  /* 1) fonte ja conhecida, ou o framebuffer padrao (pos-swap = o quadro apresentado) */
  if (bf) bf(SFB_READ_FRAMEBUFFER, sfb_fbo_id > 0 ? (unsigned)sfb_fbo_id : 0u);
  rd(0, 0, sw, sh, SFB_RGBA, SFB_UNSIGNED_BYTE, sfb_buf);
  if (sfb_has_content(sfb_buf, n)) ok = 1;
  /* 2) vazia: varre FBOs (o Unity desenha num render target) */
  if (!ok && bf) {
    for (int id = 1; id <= 48 && !ok; id++) {
      bf(SFB_READ_FRAMEBUFFER, (unsigned)id);
      rd(0, 0, sw, sh, SFB_RGBA, SFB_UNSIGNED_BYTE, sfb_buf);
      if (sfb_has_content(sfb_buf, n)) { sfb_fbo_id = id; ok = 1;
        fprintf(stderr, "shim/fb: fonte encontrada no FBO %d (%dx%d)\n", id, sw, sh); }
    }
  }
  if (bf) bf(SFB_READ_FRAMEBUFFER, (unsigned)saved);
  if (!ok) {
    if (++sfb_empty <= 3u || sfb_empty % 300u == 0u)
      fprintf(stderr, "shim/fb: readback vazio (%dx%d) tentativas=%lu\n", sw, sh, sfb_empty);
    return;
  }
  sfb_src_w = sw; sfb_src_h = sh;
  sfb_blit(sfb_buf, sw, sh);
  if (++sfb_blits <= 3ul || sfb_blits % 300ul == 0ul)
    fprintf(stderr, "shim/fb: blit #%lu %dx%d -> painel %dx%d\n", sfb_blits, sw, sh, sfb_w, sfb_h);
}

__attribute__((visibility("default")))
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
  ensure_real();
  static EGLBoolean (*real_sb)(EGLDisplay, EGLSurface);
  if (!real_sb) real_sb = (EGLBoolean(*)(EGLDisplay, EGLSurface))sym("eglSwapBuffers");
  if (!real_sb) return EGL_FALSE;
  g_shim_swaps++;
  if (g_shim_swaps <= 3ul || g_shim_swaps % 300ul == 0ul)
    fprintf(stderr, "shim/libEGL: swap #%lu dpy=%p surface=%p (Unity passa por aqui, nao pelo probe)\n",
            g_shim_swaps, (void*)dpy, (void*)surface);
  EGLBoolean r = real_sb(dpy, surface);
  sfb_present();   /* contexto do Unity esta corrente NESTA thread */
  return r;
}

/* -------- debug hook (opcional) --------
 *  `env ULTRAKILL_EGL_STATS=1` antes de rodar faz o shim, no destroy do
 *  processo, reportar quantos swaps passaram por aqui. */
__attribute__((destructor))
static void shim_report(void) {
  if (getenv("ULTRAKILL_EGL_STATS"))
    fprintf(stderr, "shim/libEGL: swaps=%lu\n", g_shim_swaps);
}

/* -------- dispatcher de EXT sintetizadas --------
 * Retorna ponteiro p/ nossa funcao quando SDL2 pedir eglGetProcAddress
 * por uma das EXT que nos implementamos localmente. NULL se nao for
 * nenhuma delas (ai eglGetProcAddress cai no real). */
typedef void (*generic_fn)(void);
__attribute__((visibility("default")))
generic_fn shim_eglGetProcAddress_dispatch(const char *name) {
  extern EGLBoolean eglQueryDevicesEXT(EGLint, void**, EGLint*);
  extern EGLDisplay eglGetPlatformDisplayEXT(EGLenum, void*, const EGLint*);
  extern EGLBoolean eglQueryDisplayAttribEXT(EGLDisplay, int, EGLint*);
  extern EGLBoolean eglQueryDeviceStringEXT(void*, const char*);
  extern EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, void*, const EGLint*);
  extern EGLBoolean eglDestroySurface(EGLDisplay, EGLSurface);
  extern EGLBoolean eglSwapBuffers(EGLDisplay, EGLSurface);
  extern EGLDisplay eglGetDisplay(void*);
  if (!name) return NULL;
  #define CMP(s) (strcmp(name, s) == 0)
  if (CMP("eglQueryDevicesEXT"))       return (generic_fn)eglQueryDevicesEXT;
  if (CMP("eglGetPlatformDisplayEXT")) return (generic_fn)eglGetPlatformDisplayEXT;
  if (CMP("eglQueryDisplayAttribEXT")) return (generic_fn)eglQueryDisplayAttribEXT;
  if (CMP("eglQueryDeviceStringEXT"))  return (generic_fn)eglQueryDeviceStringEXT;
  if (CMP("eglSwapBuffers"))           return (generic_fn)eglSwapBuffers;
  if (CMP("eglCreateWindowSurface"))   return (generic_fn)eglCreateWindowSurface;
  if (CMP("eglDestroySurface"))         return (generic_fn)eglDestroySurface;
  if (CMP("eglGetDisplay"))             return (generic_fn)eglGetDisplay;
  #undef CMP
  return NULL;
}
