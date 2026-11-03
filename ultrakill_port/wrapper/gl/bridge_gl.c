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
/* KMS direct scanout (ULTRAKILL_KMS_DIRECT, R36S): dumb buffer em card0 */
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>
#include <linux/fb.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm.h>

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
#define EGL_PBUFFER_BIT     0x0001
#define EGL_WIDTH           0x3057
#define EGL_HEIGHT          0x3056
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
typedef EGLSurface (*PFN_eglCreatePbufferSurface)(EGLDisplay, EGLConfig, const EGLint*);
typedef EGLBoolean (*PFN_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLBoolean (*PFN_eglSwapBuffers)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*PFN_eglBindAPI)(EGLenum);
typedef const char*(*PFN_eglQueryString)(EGLDisplay, int);
typedef EGLint     (*PFN_eglGetError)(void);
typedef void*      (*PFN_eglGetProcAddress)(const char*);
/* GL core via eglGetProcAddress (so o que o present precisa): */
typedef void       (*PFN_glReadPixels)(int,int,int,int,unsigned int,unsigned int,void*);
#define GL_RGBA         0x1908
#define GL_UNSIGNED_BYTE 0x1401

static PFN_eglGetDisplay              p_eglGetDisplay;
static PFN_eglInitialize              p_eglInitialize;
static PFN_eglChooseConfig            p_eglChooseConfig;
static PFN_eglCreateContext           p_eglCreateContext;
static PFN_eglCreateWindowSurface     p_eglCreateWindowSurface;
static PFN_eglCreatePbufferSurface    p_eglCreatePbufferSurface;
static PFN_eglMakeCurrent             p_eglMakeCurrent;
static PFN_eglSwapBuffers             p_eglSwapBuffers;
static PFN_eglBindAPI                 p_eglBindAPI;
static PFN_eglQueryString             p_eglQueryString;
static PFN_eglGetError                p_eglGetError;
static PFN_eglGetProcAddress          p_eglGetProcAddress_real;
static PFN_glReadPixels               p_glReadPixels;

static int load_egl(void) {
  /* IMPORTANTE: nunca use "libEGL.so" sem sufixo de versao aqui. Nosso
   * shim do bridge_egl_shim.c instala-se exatamente como libEGL.so (para
   * que o NEEDED do libunity.so caia nele). Se bridge_gl tambem abrir
   * "libEGL.so" cai no proprio shim e recursa. Abrimos apenas a versao
   * .so.1 que o DDK real instala. */
  static const char *paths[] = {
    /* SO paths absolutos: "libEGL.so.1" (sem caminho) pode resolver para
     * NOSSO shim via RUNPATH/LD_LIBRARY_PATH quando o shim esta tambem
     * carregado (LD_PRELOAD), e entao bridge_gl falaria com ele mesmo. */
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
#define LOAD(fn) do { p_##fn = (PFN_##fn)dlsym(h, #fn); \
                      if (!p_##fn) { fprintf(stderr, "dlsym %s falhou no driver real\n", #fn); return -2; } } while(0)
  LOAD(eglGetDisplay); LOAD(eglInitialize); LOAD(eglChooseConfig);
  LOAD(eglCreateContext); LOAD(eglCreateWindowSurface); LOAD(eglMakeCurrent);
  LOAD(eglSwapBuffers); LOAD(eglBindAPI); LOAD(eglQueryString); LOAD(eglGetError);
  p_eglCreatePbufferSurface = (PFN_eglCreatePbufferSurface)dlsym(h, "eglCreatePbufferSurface");
  if (!p_eglCreatePbufferSurface)
    fprintf(stderr, "bridge_gl: eglCreatePbufferSurface ausente (ok se nao usar ULTRAKILL_NO_SDL_GL)\n");
  p_eglGetProcAddress_real = (PFN_eglGetProcAddress)dlsym(h, "eglGetProcAddress");
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

/* ================= KMS direct scanout (R36S / ULTRAKILL_KMS_DIRECT) =========
 * GBM esta morto nessa firmware (ark sem acesso a renderD128 e os libgbm do
 * rootfs sao o blob disfarçado). Mas card0 funciona - e o que o ES usa.
 * Criamos um dumb buffer fullscreen e escrevemos nele direto (nearest scale
 * do pbuffer RGBA). Um drmModeSetCrtc no primeiro frame assume o display;
 * ao sair, o ES repoe o proprio fb (NO_ATIMMUTAL_FB ajuda). */
typedef struct {
  int fd;
  uint32_t crtc, conn_id, fb, handle, pitch, w, h;
  void *map;
  uint64_t size;
  int ready;
} kms_out;
static kms_out g_kms;

static int kms_open(int src_w, int src_h) {
  kms_out *k = &g_kms;
  const char *dev = getenv("ULTRAKILL_KMS");
  if (!dev || !*dev) dev = "/dev/dri/card0";
  k->fd = open(dev, O_RDWR | O_CLOEXEC);
  if (k->fd < 0) {
    fprintf(stderr, "kms: open %s falhou: %s\n", dev, strerror(errno));
    return -1;
  }
  drmModeRes *res = drmModeGetResources(k->fd);
  if (!res) { fprintf(stderr, "kms: GetResources fail\n"); return -1; }
  drmModeConnector *conn = NULL;
  for (int i = 0; i < res->count_connectors && !conn; i++) {
    drmModeConnector *c = drmModeGetConnector(k->fd, res->connectors[i]);
    if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) conn = c;
    else if (c) drmModeFreeConnector(c);
  }
  if (!conn) { fprintf(stderr, "kms: nenhum conector\n"); return -1; }
  uint32_t crtc = res->crtcs[0];
  drmModeEncoder *enc = drmModeGetEncoder(k->fd, conn->encoder_id);
  if (enc) { if (enc->crtc_id) crtc = enc->crtc_id; drmModeFreeEncoder(enc); }
  int mi = 0;
  for (int i = 0; i < conn->count_modes; i++)
    if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) { mi = i; break; }
  drmModeModeInfo *mode = &conn->modes[mi];
  k->w = mode->hdisplay; k->h = mode->vdisplay; k->conn_id = conn->connector_id;
  struct drm_mode_create_dumb cd;
  memset(&cd, 0, sizeof cd);
  cd.width = k->w; cd.height = k->h; cd.bpp = 32;
  if (ioctl(k->fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd)) {
    fprintf(stderr, "kms: CREATE_DUMB fail: %s\n", strerror(errno)); return -1;
  }
  k->handle = cd.handle; k->pitch = cd.pitch; k->size = cd.size;
  struct drm_mode_map_dumb md;
  memset(&md, 0, sizeof md);
  md.handle = cd.handle;
  if (ioctl(k->fd, DRM_IOCTL_MODE_MAP_DUMB, &md)) {
    fprintf(stderr, "kms: MAP_DUMB fail: %s\n", strerror(errno)); return -1;
  }
  k->map = mmap(0, k->size, PROT_READ | PROT_WRITE, MAP_SHARED, k->fd, md.offset);
  if (k->map == MAP_FAILED) { fprintf(stderr, "kms: mmap fail\n"); return -1; }
  if (drmModeAddFB(k->fd, k->w, k->h, 24, 32, k->pitch, k->handle, &k->fb)) {
    fprintf(stderr, "kms: AddFB fail\n"); return -1;
  }
  if (drmModeSetCrtc(k->fd, crtc, k->fb, 0, 0, &k->conn_id, 1, mode)) {
    fprintf(stderr, "kms: SetCrtc fail\n"); return -1;
  }
  k->crtc = crtc;
  k->ready = 1;
  fprintf(stderr, "kms: scanout %ux%u assumido (source %dx%d)\n",
          k->w, k->h, src_w, src_h);
  return 0;
}

static void kms_flip(const unsigned char *rgba, int sw, int sh) {
  kms_out *k = &g_kms;
  if (!k->ready || !rgba || sw <= 0 || sh <= 0) return;
  unsigned char *base = (unsigned char *)k->map;
  for (uint32_t y = 0; y < k->h; y++) {
    uint32_t sy = (uint32_t)((uint64_t)y * sh / k->h); if (sy >= (uint32_t)sh) sy = sh - 1;
    const unsigned char *srow = rgba + (size_t)sy * sw * 4;
    unsigned char *drow = base + (size_t)y * k->pitch;
    for (uint32_t x = 0; x < k->w; x++) {
      uint32_t sx = (uint32_t)((uint64_t)x * sw / k->w); if (sx >= (uint32_t)sw) sx = sw - 1;
      const unsigned char *p = srow + (size_t)sx * 4;
      /* GL RGBA -> XRGB8888 little-endian (bytes B,G,R,X) */
      drow[x*4+0] = p[2]; drow[x*4+1] = p[1]; drow[x*4+2] = p[0]; drow[x*4+3] = 0xff;
    }
  }
}

/* ================= FALLBACK /dev/fb0 (framebuffer direto) =================
 * Na R36S/ArkOS4Clone o port roda como usuario "ark" (uid 1002): se
 * /dev/dri/card0 nao for escrevivel, o KMS morre e o SDL_CreateRenderer do
 * driver offscreen SEGFAULTA nessa SDL 2.32. Sobram os fb0 - que o ES
 * raramente usa (ele fala DRM direto). Escrita pura no mapeamento: sem
 * GBM, sem renderD128, sem compositor. ULTRAKILL_FB_DEVICE permite apontar
 * fb1 se existir. */
typedef struct {
  int fd; void *map; size_t len;
  uint32_t w, h, pitch, bpp, roff, goff, boff, rlen, glen, blen;
  int ready;
} fb_out;
static fb_out g_fb;

static int fb_open(int src_w, int src_h) {
  fb_out *f = &g_fb;
  const char *dev = getenv("ULTRAKILL_FB_DEVICE");
  f->fd = open((dev && *dev) ? dev : "/dev/fb0", O_RDWR | O_CLOEXEC);
  if (f->fd < 0) f->fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
  if (f->fd < 0) {
    fprintf(stderr, "fb0: open falhou: %s\n", strerror(errno));
    return -1;
  }
  struct fb_var_screeninfo var;
  struct fb_fix_screeninfo finfo;
  if (ioctl(f->fd, FBIOGET_VSCREENINFO, &var) || ioctl(f->fd, FBIOGET_FSCREENINFO, &finfo)) {
    fprintf(stderr, "fb0: ioctl VS/FIS falhou: %s\n", strerror(errno));
    return -1;
  }
  if (var.bits_per_pixel != 32 && var.bits_per_pixel != 16) {
    var.bits_per_pixel = 32; var.grayscale = 0;
    var.red.length = var.green.length = var.blue.length = 8;
    var.red.offset = 16; var.green.offset = 8; var.blue.offset = 0;
    var.transp.length = 0; var.transp.offset = 24;
    if (ioctl(f->fd, FBIOPUT_VSCREENINFO, &var) ||
        ioctl(f->fd, FBIOGET_VSCREENINFO, &var)) {
      fprintf(stderr, "fb0: nao consegui por 32bpp: %s\n", strerror(errno));
      return -1;
    }
  }
  f->w = var.xres; f->h = var.yres; f->pitch = finfo.line_length; f->bpp = var.bits_per_pixel;
  f->roff = var.red.offset;  f->rlen = var.red.length;
  f->goff = var.green.offset; f->glen = var.green.length;
  f->boff = var.blue.offset;  f->blen = var.blue.length;
  size_t need = (size_t)f->pitch * var.yres_virtual;
  f->len = finfo.smem_len < need ? finfo.smem_len : need;
  f->map = mmap(0, f->len, PROT_READ | PROT_WRITE, MAP_SHARED, f->fd, 0);
  if (f->map == MAP_FAILED) { fprintf(stderr, "fb0: mmap falhou: %s\n", strerror(errno)); return -1; }
  f->ready = 1;
  fprintf(stderr, "fb0: framebuffer %ux%u %ubpp pitch=%u (source %dx%d)\n",
          f->w, f->h, f->bpp, f->pitch, src_w, src_h);
  return 0;
}

static void fb_flip(const unsigned char *rgba, int sw, int sh) {
  fb_out *f = &g_fb;
  if (!f->ready || !rgba || sw <= 0 || sh <= 0) return;
  unsigned char *base = (unsigned char *)f->map;
  uint32_t yoff_max = 0;   /* sempre o buffer 0 */
  for (uint32_t y = yoff_max; y < f->h; y++) {
    uint32_t sy = (uint32_t)((uint64_t)(y - yoff_max) * sh / f->h);
    if (sy >= (uint32_t)sh) sy = sh - 1;
    if ((size_t)y * f->pitch + (size_t)f->w * (f->bpp / 8) > f->len) break;
    const unsigned char *srow = rgba + (size_t)sy * sw * 4;
    unsigned char *drow = base + (size_t)y * f->pitch;
    for (uint32_t x = 0; x < f->w; x++) {
      uint32_t sx = (uint32_t)((uint64_t)x * sw / f->w); if (sx >= (uint32_t)sw) sx = sw - 1;
      const unsigned char *p = srow + (size_t)sx * 4;
      if (f->bpp == 32) {
        uint32_t v = ((uint32_t)(p[0] >> (8 - (f->rlen?f->rlen:1))) << f->roff)
                   | ((uint32_t)(p[1] >> (8 - (f->glen?f->glen:1))) << f->goff)
                   | ((uint32_t)(p[2] >> (8 - (f->blen?f->blen:1))) << f->boff);
        ((uint32_t *)drow)[x] = v;
      } else {   /* 565-ish generico */
        uint16_t v = (uint16_t)(((p[0] >> (8 - (f->rlen?f->rlen:1))) << f->roff)
                   | ((p[1] >> (8 - (f->glen?f->glen:1))) << f->goff)
                   | ((p[2] >> (8 - (f->blen?f->blen:1))) << f->boff));
        ((uint16_t *)drow)[x] = v;
      }
    }
  }
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

  /* ULTRAKILL_NO_SDL_GL: firmware com GL quebrado (R36S/ArkOS4Clone: SDL
   * 2.32 + mesa por cima do nosso glvnd crasha no loader GBM/DRM). Nesse
   * modo NAO pedimos GL ao SDL (sem SDL_WINDOW_OPENGL - senao o crash e la
   * dentro); o EGL e criado 100% por nos com Pbuffer + o shim entrega essa
   * surface ao Unity quando ele chama eglCreateWindowSurface. */
  int no_sdl_gl = getenv("ULTRAKILL_NO_SDL_GL") != NULL;

  SDL_Window *win = SDL_CreateWindow(title ? title : "ULTRAKILL",
                                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     w, h,
                                     (no_sdl_gl ? 0 : SDL_WINDOW_OPENGL) | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI);
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
    EGL_SURFACE_TYPE, no_sdl_gl ? EGL_PBUFFER_BIT : EGL_WINDOW_BIT,
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

  if (no_sdl_gl) {
    const EGLint pbuf[] = { EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE };
    fprintf(stderr, "bridge_gl: modo NO_SDL_GL - criando Pbuffer %dx%d\n", w, h);
    out->egl_surface = p_eglCreatePbufferSurface
        ? p_eglCreatePbufferSurface(out->egl_display, cfg, pbuf) : EGL_NO_SURFACE;
  } else {
    out->egl_surface = p_eglCreateWindowSurface(out->egl_display, cfg, out->native_window, NULL);
  }
  if (!out->egl_surface || out->egl_surface == EGL_NO_SURFACE) {
    fprintf(stderr, "eglCreateWindowSurface/Pbuffer falhou (err=0x%x)\n", p_eglGetError());
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

  /* ULTRAKILL_NO_SDL_GL: pipeline de apresentacao pbuffer->CPU->tela.
   * Ordem: (1) KMS dumb buffer em card0, (2) /dev/fb0, (3) SDL software
   * renderer. (3) e ULTIMO e condicional: nessa SDL 2.32 com driver
   * offscreen o SDL_CreateRenderer SEGFAULTA (visto no device), entao so o
   * tentamos com backend de video real. Sem nenhum dos tres o port segue
   * HEADLESS: a engine roda e o log diz ate onde vai - separa crash de
   * apresentacao de crash do Unity. */
  if (no_sdl_gl) {
    if (p_eglGetProcAddress_real)
      p_glReadPixels = (PFN_glReadPixels)p_eglGetProcAddress_real("glReadPixels");
    if (!p_glReadPixels) {
      fprintf(stderr, "bridge_gl: glReadPixels indisponivel; sem readback\n");
    } else {
      if (getenv("ULTRAKILL_KMS_DIRECT") && kms_open(w, h) == 0) {
        out->pixel_buf = malloc((size_t)w * h * 4);
        out->kms_ready = 1;
        fprintf(stderr, "bridge_gl: present = KMS scanout (card0)\n");
      }
      if (!out->kms_ready && !getenv("ULTRAKILL_NO_FB") && fb_open(w, h) == 0) {
        out->pixel_buf = malloc((size_t)w * h * 4);
        out->fb_ready = 1;
        fprintf(stderr, "bridge_gl: present = /dev/fb0\n");
      }
      if (!out->kms_ready && !out->fb_ready) {
        const char *vd = getenv("SDL_VIDEODRIVER");
        int nofb = !vd || !*vd || strcmp(vd, "offscreen") == 0 || strcmp(vd, "dummy") == 0;
        if (nofb || getenv("ULTRAKILL_NO_SDL_RENDERER")) {
          fprintf(stderr, "bridge_gl: SEM present (KMS e fb0 indisponiveis; o\n");
          fprintf(stderr, "           renderer offscreen dessa SDL crasha) - HEADLESS\n");
        } else {
          SDL_Renderer *r = SDL_CreateRenderer(out->sdl, -1, SDL_RENDERER_SOFTWARE);
          if (r) {
            SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888,
                                               SDL_TEXTUREACCESS_STREAMING, w, h);
            if (t) {
              out->sdl_renderer = r; out->sdl_texture = t;
              out->pixel_buf = malloc((size_t)w * h * 4);
              fprintf(stderr, "bridge_gl: present pipeline pronto (leia-se: havera imagem)\n");
            } else {
              SDL_DestroyRenderer(r);
              fprintf(stderr, "bridge_gl: SDL_CreateTexture falhou: %s\n", SDL_GetError());
            }
          } else {
            fprintf(stderr, "bridge_gl: SDL_CreateRenderer falhou: %s\n", SDL_GetError());
          }
        }
      }
      if (out->pixel_buf)
        fprintf(stderr, "bridge_gl: readback %dx%d (%zu bytes)\n", w, h, (size_t)w * h * 4);
      else
        fprintf(stderr, "bridge_gl: AVISO: sem pixel_buf (malloc falhou?)\n");
    }
  }

  fprintf(stderr, "bridge_gl: ok %dx%d, GL ver=%s\n", w, h, gl_version_probe());
  return SHIM_GL_OK;
}

void shim_gl_present(shim_gl_ctx *c) {
  if (!c || !p_glReadPixels || !c->pixel_buf) return;
  if (c->kms_ready) {
    p_glReadPixels(0, 0, c->width, c->height, GL_RGBA, GL_UNSIGNED_BYTE, c->pixel_buf);
    kms_flip((const unsigned char *)c->pixel_buf, c->width, c->height);
    return;
  }
  if (c->fb_ready) {
    p_glReadPixels(0, 0, c->width, c->height, GL_RGBA, GL_UNSIGNED_BYTE, c->pixel_buf);
    fb_flip((const unsigned char *)c->pixel_buf, c->width, c->height);
    return;
  }
  if (!c->sdl_renderer || !c->sdl_texture) return;
  SDL_Renderer  *r = (SDL_Renderer *)c->sdl_renderer;
  SDL_Texture   *t = (SDL_Texture  *)c->sdl_texture;
  p_glReadPixels(0, 0, c->width, c->height, GL_RGBA, GL_UNSIGNED_BYTE, c->pixel_buf);
  int stride = c->width * 4;
  /* GL lê de baixo pra cima; pitch negativo vira a imagem numa passada. */
  SDL_UpdateTexture(t, NULL,
                    (char *)c->pixel_buf + (size_t)stride * (c->height - 1),
                    -stride);
  SDL_RenderClear(r);
  SDL_RenderCopy(r, t, NULL, NULL);
  SDL_RenderPresent(r);
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
