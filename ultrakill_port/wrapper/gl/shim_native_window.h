/* shim_native_window.h - struct compartilhada entre bridge_gl.c (que cria)
 * e bridge_egl_shim.c (que identifica ao interceptar eglCreateWindowSurface).
 *
 * IMPORTANTE: o layout é fixo. bridge_egl_shim.c lê o `magic` no offset 0
 * pra decidir se um `EGLNativeWindowType` é nosso shim. Se você inserir
 * campos antes de `magic`, o shim interpreta lixo como magic e o
 * dispatch cai no ramo errado. */
#ifndef SHIM_NATIVE_WINDOW_H
#define SHIM_NATIVE_WINDOW_H

#include <stdint.h>

#define SHIM_ANW_MAGIC 0x414E5700u   /* 'A' 'N' 'W' 0x00 - "ANW\0" */

struct shim_native_window {
  uint32_t magic;                          /* == SHIM_ANW_MAGIC */
  int      refs;
  int      format;                         /* 1 = RGBA_8888 (ANDROID_FORMAT_RGBX_8888) */
  int      w, h;
  void    *native_h;                       /* X11 Window (como uintptr_t) ou wl_surface* */
  void    *egl_surface;                    /* EGLSurface pré-criado pelo bridge_gl */
  void    *egl_config;                     /* EGLConfig usado na criação */
  void    *egl_display;                    /* EGLDisplay correspondente */
  void   (*on_swap)(struct shim_native_window *);
};

/* Helper inline - usado no shim e no bridge_gl. */
static inline int shim_anw_is_shim(void *win) {
  /* Checa magic SEM deref se o ponteiro for visivelmente inválido
   * (ex.: 0x1, que é o que Unity passa em alguns testes internos). */
  if (!win) return 0;
  /* A maioria dos EGLNativeWindowType "reais" (X11 Window ID) é um
   * inteiro pequeno. Só tentamos o cast se o ponteiro for plausível
   * como heap address: */
  uintptr_t p = (uintptr_t)win;
  if (p < 0x1000) return 0;                 /* X11 window IDs vivem aqui */
  return ((struct shim_native_window*)win)->magic == SHIM_ANW_MAGIC;
}

/* Accessor exportado pelo bridge_gl.c via -rdynamic. Retorna NULL se o
 * bridge ainda não chamou shim_gl_init. */
#ifdef __cplusplus
extern "C" {
#endif
struct shim_native_window *shim_get_native_window(void);
#ifdef __cplusplus
}
#endif

#endif /* SHIM_NATIVE_WINDOW_H */
