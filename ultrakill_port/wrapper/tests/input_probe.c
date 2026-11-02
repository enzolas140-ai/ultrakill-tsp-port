/* input_probe.c - teste standalone do subsistema de input do shim.
 *
 * Proposito: validar o ANEL SDL -> bridge_input.c -> shim_input_queue_push
 * -> AInputQueue_getEvent -> callback, SEM depender de libunity.so.
 *
 * O que verifica (quatro níveis):
 *
 *   (A) ABI: static_assert no layout de shim_native_window e no tamanho
 *       minimo de AInputEvent. Pega regressao no header compartilhado
 *       antes mesmo de compilar o resto.
 *
 *   (B) Exports: dlopen das libs .so que vao no pacote (libandroid.so,
 *       libEGL.so - nosso shim) + dlsym dos simbolos criticos. Isto
 *       pega o caso de alguem mudar `visibility("default")` para
 *       `hidden` e quebrar o runtime do Unity silenciosamente.
 *
 *   (C) Round-trip sintetico: injeta 3 eventos (KEY, MOTION, AXES) via
 *       shim_input_event_alloc/push e drena pela AInputQueue API
 *       publica. Confere que getAction/getKeyCode/getX/getAxisValue
 *       retornam exatamente o que foi escrito. Se um accessor estiver
 *       lendo de offset errado, isto pega aqui.
 *
 *   (D) Loop real: spawna bridge_input (thread SDL_WaitEventTimeout) e
 *       drena a fila durante `--dur N` segundos. Cada evento do gamepad
 *       aparece logado. Se nao houver gamepad (modo --headless), o
 *       passo D ainda roda mas nao deve produzir eventos reais - o
 *       teste considera isso OK.
 *
 * Exit code: 0 se todos os checks passaram, !=0 na primeira falha.
 *
 * Compilado pelo CMake como target opcional `input_probe`. Rodar:
 *   ULTRAKILL_REAL_LIBEGL=/usr/lib/aarch64-linux-gnu/libEGL.so.1 \
 *   LD_LIBRARY_PATH=../libs/arm64 \
 *   ./build-arm64/input_probe --dur 5
 *   ...no device, com um gamepad fisicamente conectado. */
#define _GNU_SOURCE
#include <android_shim.h>
#include <bridge_input.h>
#include <shim_native_window.h>

#include <SDL.h>

#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ============================================================ (A) ABI */

/* shim_native_window: o bridge_egl_shim le magic no offset 0 SEMPRE.
 * Se isso quebrar, o shim confunde X11 window ID com nosso ponteiro. */
_Static_assert(offsetof(struct shim_native_window, magic) == 0,
               "egl_shim exige magic em offset 0 de shim_native_window");
/* egl_surface vem depois de w,h - nao assertamos offset absoluto porque
 * varia entre 32/64-bit, mas garantimos que egl_surface >= 20 (depois de
 * magic+refs+format+w+h = 5 campos de 4 bytes). */
_Static_assert(offsetof(struct shim_native_window, egl_surface) >= 20,
               "egl_surface deve vir depois de w/h no layout");

/* AInputEvent: usamos o accessor AInputEvent_getType() no offset
 * esperado pelo layout interno - nada externo depende, mas garantimos
 * que nao encolheu. */
_Static_assert(sizeof(struct AInputEvent) >= 64,
               "AInputEvent cresceu demais ou sumiu - checa stub");

/* ============================================================ (B+C+D) */

/* API interna do stub, exportada com visibility default. */
extern AInputEvent *shim_input_event_alloc(int32_t type, int32_t source);
extern int32_t      shim_input_queue_push(AInputQueue *q, AInputEvent *ev);
extern AInputQueue *shim_input_queue_global(void);
extern void         shim_input_key_fill(AInputEvent *ev, int32_t action,
                                        int32_t keycode, int32_t scancode);
extern void         shim_input_motion_resize(AInputEvent *ev, size_t n);
extern void         shim_input_motion_set_action(AInputEvent *ev, int32_t action);
extern void         shim_input_motion_set_pointer(AInputEvent *ev, size_t idx,
                                                  int32_t id, float x, float y,
                                                  float pressure, float size,
                                                  int32_t tool_type);
extern void         shim_input_motion_set_axis(AInputEvent *ev, int32_t axis, float v);

/* -------- util: timestamps -------- */
static int64_t now_ms(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* -------- util: pretty printers -------- */
static const char *etype_name(int32_t t) {
  switch (t) {
    case AINPUT_EVENT_TYPE_KEY:    return "KEY";
    case AINPUT_EVENT_TYPE_MOTION: return "MOTION";
    default: return "?";
  }
}
static const char *kaction_name(int32_t a) {
  switch (a) {
    case AKEY_EVENT_ACTION_DOWN:     return "DOWN";
    case AKEY_EVENT_ACTION_UP:       return "UP";
    case AKEY_EVENT_ACTION_MULTIPLE: return "MULT";
    default: return "?";
  }
}
static const char *maction_name(int32_t a) {
  int32_t base = a & AMOTION_EVENT_ACTION_MASK;
  switch (base) {
    case AMOTION_EVENT_ACTION_DOWN:        return "DOWN";
    case AMOTION_EVENT_ACTION_UP:          return "UP";
    case AMOTION_EVENT_ACTION_MOVE:        return "MOVE";
    case AMOTION_EVENT_ACTION_CANCEL:      return "CANCEL";
    case AMOTION_EVENT_ACTION_OUTSIDE:     return "OUTSIDE";
    case AMOTION_EVENT_ACTION_POINTER_DOWN:return "P_DOWN";
    case AMOTION_EVENT_ACTION_POINTER_UP:  return "P_UP";
    default: return "?";
  }
}
static const char *source_name(int32_t s) {
  switch (s) {
    case AINPUT_SOURCE_KEYBOARD:    return "KEYBOARD";
    case AINPUT_SOURCE_DPAD:        return "DPAD";
    case AINPUT_SOURCE_GAMEPAD:     return "GAMEPAD";
    case AINPUT_SOURCE_TOUCHSCREEN: return "TOUCHSCREEN";
    case AINPUT_SOURCE_JOYSTICK:    return "JOYSTICK";
    default: return "?";
  }
}

static void print_event(AInputEvent *ev) {
  int32_t type = AInputEvent_getType(ev);
  int32_t src  = AInputEvent_getSource(ev);
  int32_t dev  = AInputEvent_getDeviceId(ev);
  if (type == AINPUT_EVENT_TYPE_KEY) {
    int32_t a = AKeyEvent_getAction(ev);
    int32_t k = AKeyEvent_getKeyCode(ev);
    printf("  KEY    src=%-11s dev=%d action=%-5s keycode=%3d  down_t=%" PRId64 "\n",
           source_name(src), dev, kaction_name(a), k,
           (int64_t)AKeyEvent_getDownTime(ev));
  } else if (type == AINPUT_EVENT_TYPE_MOTION) {
    int32_t a = AMotionEvent_getAction(ev);
    size_t n = AMotionEvent_getPointerCount(ev);
    printf("  MOTION src=%-11s dev=%d action=%-7s ptrs=%zu",
           source_name(src), dev, maction_name(a), n);
    for (size_t i = 0; i < n && i < 4; i++)
      printf("  [%zu] x=%.1f y=%.1f id=%d",
             i, AMotionEvent_getX(ev, i), AMotionEvent_getY(ev, i),
             AMotionEvent_getPointerId(ev, i));
    float l  = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_LTRIGGER, 0);
    float r  = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_RTRIGGER, 0);
    float rx = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_RX, 0);
    float ry = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_RY, 0);
    if (l != 0 || r != 0 || rx != 0 || ry != 0)
      printf("  L=%.2f R=%.2f rX=%.2f rY=%.2f", l, r, rx, ry);
    printf("\n");
  } else {
    printf("  ???    type=%d src=%d\n", type, src);
  }
}

/* -------- looper callback (modo "attach ALooper" - opcional) -------- */
static int looper_cb(int fd, int events, void *data) {
  (void)fd; (void)events;
  AInputQueue *q = (AInputQueue*)data;
  AInputEvent *ev = NULL;
  while (AInputQueue_getEvent(q, &ev) == 0) {
    print_event(ev);
    AInputQueue_preDispatchEvent(q, ev);
    AInputQueue_finishEvent(q, ev, /*handled=*/1);
  }
  return 1;
}

/* -------- check (B): dlopen das libs .so do pacote -------- */
struct sym_req { const char *lib; const char *sym; };
static const struct sym_req REQUIRED_EXPORTS[] = {
  /* libandroid.so - API de input que Unity chama */
  { "libandroid.so", "AInputQueue_getEvent" },
  { "libandroid.so", "AInputQueue_attachLooper" },
  { "libandroid.so", "AInputQueue_finishEvent" },
  { "libandroid.so", "AInputEvent_getType" },
  { "libandroid.so", "AInputEvent_getSource" },
  { "libandroid.so", "AKeyEvent_getKeyCode" },
  { "libandroid.so", "AMotionEvent_getAction" },
  { "libandroid.so", "AMotionEvent_getPointerCount" },
  { "libandroid.so", "AMotionEvent_getX" },
  { "libandroid.so", "AMotionEvent_getY" },
  { "libandroid.so", "AMotionEvent_getAxisValue" },
  /* libandroid.so - nossa extensao para bridge_input */
  { "libandroid.so", "shim_input_event_alloc" },
  { "libandroid.so", "shim_input_queue_push" },
  { "libandroid.so", "shim_input_queue_global" },
  { "libandroid.so", "shim_input_motion_set_axis" },
  /* liblog.so - stub simples */
  { "liblog.so", "__android_log_print" },
  { "liblog.so", "__android_log_write" },
  /* libEGL.so - nosso shim; os 3 casos que Unity chama */
  { "libEGL.so",   "eglCreateWindowSurface" },
  { "libEGL.so",   "eglDestroySurface" },
  { "libEGL.so",   "eglSwapBuffers" },
  { "libEGL.so",   "eglMakeCurrent" },
  { "libEGL.so",   "eglGetDisplay" },
  { "libEGL.so",   "eglInitialize" },
  { NULL, NULL }
};

static int check_so_exports(void) {
  int errors = 0;
  /* dedupe por lib */
  static const char *libs[] = { "libandroid.so", "liblog.so", "libEGL.so", NULL };
  void *h[4] = {0};
  for (int i = 0; libs[i]; i++) {
    h[i] = dlopen(libs[i], RTLD_NOW | RTLD_NOLOAD);
    if (!h[i]) h[i] = dlopen(libs[i], RTLD_NOW | RTLD_LOCAL);
    if (!h[i]) {
      fprintf(stderr, "[B] FALHA dlopen(%s): %s\n", libs[i], dlerror());
      errors++;
    }
  }
  for (const struct sym_req *r = REQUIRED_EXPORTS; r->lib; r++) {
    int li = -1;
    for (int i = 0; libs[i]; i++) if (!strcmp(libs[i], r->lib)) { li = i; break; }
    if (li < 0) { fprintf(stderr, "[B] lib desconhecida %s\n", r->lib); errors++; continue; }
    if (!h[li]) continue;
    void *p = dlsym(h[li], r->sym);
    if (!p) {
      fprintf(stderr, "[B] FALHA %s!%s nao exportado\n", r->lib, r->sym);
      errors++;
    }
  }
  for (int i = 0; libs[i]; i++) if (h[i]) dlclose(h[i]);
  printf("[B] exports dos .so: %s (%d falhas)\n",
         errors == 0 ? "OK" : "FAIL", errors);
  return errors;
}

/* -------- check (C): round-trip sintetico -------- */
static int check_synthetic_roundtrip(void) {
  AInputQueue *q = shim_input_queue_global();

  /* C.1 - KEY */
  AInputEvent *k = shim_input_event_alloc(AINPUT_EVENT_TYPE_KEY, AINPUT_SOURCE_GAMEPAD);
  shim_input_key_fill(k, AKEY_EVENT_ACTION_DOWN, AKEYCODE_BUTTON_A, 0x130);
  shim_input_queue_push(q, k);

  /* C.2 - MOTION com 2 pointers */
  AInputEvent *m = shim_input_event_alloc(AINPUT_EVENT_TYPE_MOTION, AINPUT_SOURCE_TOUCHSCREEN);
  shim_input_motion_resize(m, 2);
  shim_input_motion_set_action(m, AMOTION_EVENT_ACTION_POINTER_DOWN);
  shim_input_motion_set_pointer(m, 0, /*id=*/0, 100.f, 200.f, 1.f, 1.f, AMOTION_EVENT_TOOL_TYPE_FINGER);
  shim_input_motion_set_pointer(m, 1, /*id=*/1, 300.f, 400.f, 1.f, 1.f, AMOTION_EVENT_TOOL_TYPE_FINGER);
  shim_input_queue_push(q, m);

  /* C.3 - MOTION com axis de joystick */
  AInputEvent *a = shim_input_event_alloc(AINPUT_EVENT_TYPE_MOTION, AINPUT_SOURCE_JOYSTICK);
  shim_input_motion_resize(a, 1);
  shim_input_motion_set_action(a, AMOTION_EVENT_ACTION_MOVE);
  shim_input_motion_set_pointer(a, 0, 0, 0.f, 0.f, 0.f, 0.f, AMOTION_EVENT_TOOL_TYPE_UNKNOWN);
  shim_input_motion_set_axis(a, AMOTION_EVENT_AXIS_RX, 0.75f);
  shim_input_motion_set_axis(a, AMOTION_EVENT_AXIS_LTRIGGER, 0.5f);
  shim_input_queue_push(q, a);

  int errors = 0;
  AInputEvent *got = NULL;
  int rc;

  /* drain - espera os 3 eventos na ordem */
  rc = AInputQueue_getEvent(q, &got);
  if (rc != 0 || !got || AInputEvent_getType(got) != AINPUT_EVENT_TYPE_KEY ||
      AKeyEvent_getKeyCode(got) != AKEYCODE_BUTTON_A ||
      AKeyEvent_getAction(got) != AKEY_EVENT_ACTION_DOWN ||
      AKeyEvent_getScanCode(got) != 0x130) {
    fprintf(stderr, "[C] FAIL key round-trip (rc=%d)\n", rc); errors++;
  } else printf("[C] round-trip KEY ok: keycode=%d action=%d scancode=%d\n",
                AKeyEvent_getKeyCode(got), AKeyEvent_getAction(got),
                AKeyEvent_getScanCode(got));
  AInputQueue_finishEvent(q, got, 1);

  rc = AInputQueue_getEvent(q, &got);
  if (rc != 0 || !got || AInputEvent_getType(got) != AINPUT_EVENT_TYPE_MOTION ||
      AMotionEvent_getPointerCount(got) != 2 ||
      AMotionEvent_getX(got, 0) != 100.f ||
      AMotionEvent_getY(got, 1) != 400.f) {
    fprintf(stderr, "[C] FAIL motion 2-pointer round-trip (rc=%d)\n", rc); errors++;
  } else printf("[C] round-trip MOTION 2p ok: n=%zu x0=%.0f y1=%.0f\n",
                AMotionEvent_getPointerCount(got),
                AMotionEvent_getX(got, 0), AMotionEvent_getY(got, 1));
  AInputQueue_finishEvent(q, got, 1);

  rc = AInputQueue_getEvent(q, &got);
  if (rc != 0 || !got ||
      AMotionEvent_getAxisValue(got, AMOTION_EVENT_AXIS_RX, 0) != 0.75f ||
      AMotionEvent_getAxisValue(got, AMOTION_EVENT_AXIS_LTRIGGER, 0) != 0.5f) {
    fprintf(stderr, "[C] FAIL axis round-trip (rc=%d)\n", rc); errors++;
  } else printf("[C] round-trip AXIS ok: rX=%.2f Ltrig=%.2f\n",
                AMotionEvent_getAxisValue(got, AMOTION_EVENT_AXIS_RX, 0),
                AMotionEvent_getAxisValue(got, AMOTION_EVENT_AXIS_LTRIGGER, 0));
  AInputQueue_finishEvent(q, got, 1);

  /* fila deve estar vazia */
  rc = AInputQueue_getEvent(q, &got);
  if (rc == 0) {
    fprintf(stderr, "[C] FAIL: fila nao vazia apos drena-3\n");
    errors++;
  } else printf("[C] fila vazia apos drain ok\n");

  printf("[C] round-trip sintetico: %s\n", errors == 0 ? "OK" : "FAIL");
  return errors;
}

/* -------- check (D): loop real com SDL + bridge_input -------- */
static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

static int check_real_loop(int duration_s, int headless) {
  /* (opcional) shim_gl_init - so se NAO headless. */
  static shim_gl_ctx glc;
  if (!headless) {
    shim_gl_status gs = shim_gl_init(&glc, 640, 360, "input_probe");
    if (gs != SHIM_GL_OK) {
      fprintf(stderr, "[D] WARN: shim_gl_init falhou (code %d); caindo em headless\n",
              (int)gs);
      headless = 1;
    }
  }
  if (headless) {
    /* SDL so joystick subsystem, sem window */
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
      fprintf(stderr, "[D] FAIL SDL_Init: %s\n", SDL_GetError());
      return 1;
    }
  }

  static shim_input_ctx *inp = NULL;
  shim_input_status is = shim_input_start(&inp, /*unity_handle=*/NULL,
                                          640, 360);
  if (is != SHIM_INPUT_OK) {
    fprintf(stderr, "[D] FAIL shim_input_start: code %d\n", (int)is);
    if (headless) SDL_Quit();
    return 1;
  }

  signal(SIGINT, on_sigint);
  signal(SIGTERM, on_sigint);

  AInputQueue *q = shim_input_queue_global();
  int64_t t0 = now_ms(), t_end = t0 + (int64_t)duration_s * 1000;
  int drained = 0;
  printf("[D] drena AInputQueue por %ds. Aperte botoes do gamepad (Ctrl+C para sair).\n",
         duration_s);
  while (!g_stop && now_ms() < t_end) {
    AInputEvent *ev = NULL;
    while (AInputQueue_getEvent(q, &ev) == 0) {
      print_event(ev);
      AInputQueue_preDispatchEvent(q, ev);
      AInputQueue_finishEvent(q, ev, 1);
      drained++;
    }
    /* dorme 16ms - nao busy-wait */
    struct timespec ts = { 0, 16_000_000L };
    nanosleep(&ts, NULL);
  }

  shim_input_stop(inp);
  if (!headless) shim_gl_shutdown(&glc);
  else           SDL_Quit();

  printf("[D] total events reais drenados: %d\n", drained);
  /* Se passou por aqui sem crashar, o loop funciona. drained pode ser
   * 0 em CI sem gamepad - nao e falha. */
  printf("[D] loop real: OK\n");
  return 0;
}

/* -------- main -------- */
int main(int argc, char **argv) {
  int duration_s = 5;
  int headless = 0;
  int skip_real = 0;
  const char *libdir = NULL;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--dur") && i+1 < argc)      duration_s = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--headless"))           headless = 1;
    else if (!strcmp(argv[i], "--skip-real-loop"))     skip_real = 1;
    else if (!strcmp(argv[i], "--libdir") && i+1 < argc) libdir = argv[++i];
    else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      printf("uso: input_probe [--dur S] [--headless] [--skip-real-loop] [--libdir DIR]\n");
      printf("  --dur S             segundos de loop real (default 5)\n");
      printf("  --headless          nao abre janela SDL (so joystick)\n");
      printf("  --skip-real-loop    pula o check D (so A+B+C)\n");
      printf("  --libdir DIR        prepend DIR em LD_LIBRARY_PATH\n");
      return 0;
    }
    else { fprintf(stderr, "arg desconhecido: %s\n", argv[i]); return 2; }
  }

  if (libdir) {
    const char *old = getenv("LD_LIBRARY_PATH");
    char buf[4096];
    snprintf(buf, sizeof buf, "%s%s%s", libdir, old ? ":" : "", old ? old : "");
    setenv("LD_LIBRARY_PATH", buf, 1);
    printf("[env] LD_LIBRARY_PATH=%s\n", getenv("LD_LIBRARY_PATH"));
  }

  printf("=== input_probe: teste standalone do subsistema de input ===\n");
  printf("[A] static_asserts de ABI passaram (nao ha o que fazer em runtime)\n");

  int errors = 0;
  errors += check_so_exports();
  errors += check_synthetic_roundtrip();
  if (!skip_real) errors += check_real_loop(duration_s, headless);
  else            printf("[D] SKIP (--skip-real-loop)\n");

  printf("=== RESULT: %s (erros=%d) ===\n",
         errors == 0 ? "PASS" : "FAIL", errors);
  return errors == 0 ? 0 : 1;
}
