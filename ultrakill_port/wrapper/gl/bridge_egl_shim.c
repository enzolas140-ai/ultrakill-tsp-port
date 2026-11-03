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
