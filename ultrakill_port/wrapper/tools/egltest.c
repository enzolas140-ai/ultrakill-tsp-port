/* egltest.c - diagnostico puro de EGL/GLES SEM tocar no SDL.
 * Roda com o mesmo ambiente do bridge no R36S (glvndstack + mesa software).
 * Cada passo imprime OK ou morre nomeando o passo - assim sabemos se o
 * problema e SO o SDL-kmsdrm-esquematic ou o mesa sobre esse kernel. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *EGLDisplay, *EGLContext, *EGLSurface, *EGLConfig;
typedef int EGLBoolean, EGLint;

typedef EGLDisplay (*fn_getd)(void *);
typedef EGLBoolean (*fn_init)(EGLDisplay, EGLint *, EGLint *);
typedef EGLBoolean (*fn_choose)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
typedef EGLSurface (*fn_pbuf)(EGLDisplay, EGLConfig, const EGLint *);
typedef EGLContext (*fn_ctx)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
typedef EGLBoolean (*fn_cur)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef const char *(*fn_qstr)(EGLDisplay, EGLint);
typedef void *(*fn_gpa)(const char *);
typedef const char *(*fn_ggs)(unsigned);

#define EGL_NONE 0x3038
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PBUFFER_BIT 0x0001
#define EGL_WINDOW_BIT 0x0004
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_VENDOR 0x3053
#define EGL_VERSION 0x3054

static int step(const char *name, int ok) {
  fprintf(stderr, "[egltest] %-28s %s\n", name, ok ? "OK" : "FALHOU");
  return ok;
}

int main(void) {
  setvbuf(stderr, NULL, _IONBF, 0);
  const char *real = getenv("ULTRAKILL_REAL_LIBEGL");
  const char *cands[4] = { real, "libEGL.so.1", "/usr/lib/libEGL.so.1", NULL };
  void *he = NULL;
  for (int i = 0; cands[i] && !he; i++)
    he = dlopen(cands[i], RTLD_NOW | RTLD_LOCAL);
  if (!step("dlopen libEGL", he != NULL)) return 1;

  fn_getd   getd  = (fn_getd)dlsym(he, "eglGetDisplay");
  fn_init   init  = (fn_init)dlsym(he, "eglInitialize");
  fn_choose choo  = (fn_choose)dlsym(he, "eglChooseConfig");
  fn_pbuf   pbuf  = (fn_pbuf)dlsym(he, "eglCreatePbufferSurface");
  fn_ctx    mctx  = (fn_ctx)dlsym(he, "eglCreateContext");
  fn_cur    cur   = (fn_cur)dlsym(he, "eglMakeCurrent");
  fn_qstr   qs    = (fn_qstr)dlsym(he, "eglQueryString");
  if (!step("dlsym funcoes EGL", getd && init && choo && pbuf && mctx && cur))
    return 2;

  EGLDisplay dpy = getd(NULL /*EGL_DEFAULT*/);
  if (!step("eglGetDisplay(default)", dpy != NULL && (long)dpy != -1)) {
    fn_gpa gp0 = (fn_gpa)dlsym(he, "eglGetProcAddress");
    (void)gp0;
    return 3;
  }
  EGLint maj = 0, min = 0;
  if (!step("eglInitialize", init(dpy, &maj, &min))) return 4;
  fprintf(stderr, "[egltest] EGL version %d.%d vendor=%s\n", maj, min,
          qs ? qs(dpy, EGL_VENDOR) : "?");

  EGLint cfga[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                    EGL_ALPHA_SIZE, 8, EGL_NONE };
  EGLConfig cfg; EGLint n = 0;
  if (!step("eglChooseConfig(PBUFFER)", choo(dpy, cfga, &cfg, 1, &n) && n > 0))
    return 5;

  EGLint pba[] = { EGL_WIDTH, 480, EGL_HEIGHT, 270, EGL_NONE };
  EGLSurface s = pbuf(dpy, cfg, pba);
  if (!step("eglCreatePbufferSurface", s)) return 6;

  EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
  EGLContext c = mctx(dpy, cfg, NULL, ca);
  if (!step("eglCreateContext(ES2)", c)) return 7;
  if (!step("eglMakeCurrent", cur(dpy, s, s, c))) return 8;

  void *hg = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
  fn_ggs glGetString = hg ? (fn_ggs)dlsym(hg, "glGetString") : NULL;
  if (!glGetString && qs) {
    fn_gpa gp = (fn_gpa)dlsym(he, "eglGetProcAddress");
    if (gp) glGetString = (fn_ggs)gp("glGetString");
  }
  if (glGetString)
    fprintf(stderr, "[egltest] GL_RENDERER/VER = %s\n", glGetString(0x1F02));
  else
    fprintf(stderr, "[egltest] (sem libGLESv2 nem eglGetProcAddress)\n");

  fprintf(stderr, "[egltest] === TUDO OK: mesa EGL funciona SEM SDL ===\n");
  return 0;
}
