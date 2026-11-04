/* bridge_input.c - thread SDL -> Unity (nativeInjectEvent) + mouse virtual.
 *
 * ===========================================================================
 * POR QUE ESTE ARQUIVO FOI REESCRITO (evidencia, nao chute)
 * ===========================================================================
 * 1) `tools/dumpelf.py gamedata/lib/arm64-v8a/libunity.so --undef` mostra que
 *    o libunity.so deste build NAO importa nenhum AInputQueue_* / AMotionEvent_*.
 *    O log do device (RegisterNatives) mostra a unica porta de input que
 *    EXISTE:  nativeInjectEvent (Landroid/view/InputEvent;)Z.
 *    -> Empilhar eventos na AInputQueue do stub NADA entrega ao jogo. A fila
 *       antiga continua aqui (custo zero, e o caminho dos builds baseados em
 *       NativeActivity), mas o trabalho REAL e feito por nativeInjectEvent com
 *       um jobject falso de android/view/{Key,Motion}Event respondido pela
 *       mini-JVM (ver bridge_jni.c, bloco EVENTOS VIRTUAIS).
 *
 * 2) MOUSE VIRTUAL (pedido do usuario no TrimUI Smart Pro, que nao tem
 *    touch screen nem mouse):
 *      START + R1  -> LIGA/DESLIGA o mouse virtual
 *      B           -> clique (down/up no ponteiro)
 *      analógico   -> move o ponteiro
 *    Enquanto o mouse esta LIGADO o botao B nao e entregue ao jogo (vira
 *    clique) e o analógico configurado (padrao: DIREITO) vira ponteiro; o
 *    analógico ESQUERDO continua sendo movimento, porque senao o jogo fica
 *    inguiavel. Tudo ajustavel por env, sem recompilar:
 *      ULTRAKILL_MOUSE=0|1            (padrao 1)
 *      ULTRAKILL_MOUSE_STICK=right|left|dpad|none   (padrao right)
 *      ULTRAKILL_MOUSE_SPEED=600..3000 pixels/s em curso total (padrao 1100)
 *      ULTRAKILL_MOUSE_DEADZONE=0..32767            (padrao 5000)
 *      ULTRAKILL_MOUSE_MODE=touch|mouse             (padrao touch)
 *      ULTRAKILL_MOUSE_TOGGLE=start+r1|select+r1|l3+r3
 *      ULTRAKILL_MOUSE_CLICK=b|x                    (padrao b)
 *
 *    'touch' = o ponteiro e injetado como dedo (ACTION_DOWN/MOVE/UP,
 *    TOOL_TYPE_FINGER). E o que a interface MOBILE do ULTRAKILL lê, e o Unity
 *    ainda por cima emula Input.mousePosition a partir do toque primario.
 *    'mouse' = injeta como mouse Android real (ACTION_HOVER_MOVE +
 *    BUTTON_DOWN/UP, TOOL_TYPE_MOUSE), para build que usa Input mouse.
 *
 * Ciclo de vida: identico ao anterior - shim_input_start/stop. O bridge_main
 * chama shim_input_set_unity_inject() depois do JNI_OnLoad, quando o native
 * ja esta registrado. */
#include <bridge_input.h>
#include <bridge_jni.h>
#include <android_shim.h>

#include <dlfcn.h>
#include <stdatomic.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>      /* strcasecmp */
#include <time.h>         /* clock_gettime */
#include <unistd.h>

#include <SDL.h>

/* -------- API interna exposta pelo android_input_stub.c -------- */
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

/* signature real do native registrado pelo Unity:
 *   static native boolean nativeInjectEvent(InputEvent ev); */
typedef int32_t (*unity_inject_fn)(JNIEnv *env, jobject thiz, jobject event);

/* -------- constantes Android que faltam no header -------- */
enum {
  AMOTION_EVENT_ACTION_HOVER_MOVE     = 7,
  AMOTION_EVENT_ACTION_BUTTON_DOWN    = 0x0000000b,
  AMOTION_EVENT_ACTION_BUTTON_UP      = 0x0000000c,
  AMOTION_EVENT_ACTION_POINTER_SHIFT  = 8,
  AINPUT_SOURCE_MOUSE                 = 0x00002002,
  AINPUT_SOURCE_STYLUS                = 0x00002004,
  AKEYCODE_BUTTON_SELECT_ALIAS        = AKEYCODE_BUTTON_SELECT
};

#define MOUSE_MAX_PTR SHIM_JNI_MAX_POINTERS

typedef enum { MS_OFF = 0, MS_TOUCH, MS_MOUSE } mouse_kind;

struct shim_input_ctx {
  /* portas de saida */
  unity_inject_fn    inject;          /* nativeInjectEvent (pode ser NULL) */
  JNIEnv            *env;
  jobject            ev_key;          /* envelope permanente KeyEvent */
  jobject            ev_motion;       /* envelope permanente MotionEvent */
  shim_jni_event     kev;
  shim_jni_event     mev;

  pthread_t          thread;
  atomic_int         stop;
  int                screen_w, screen_h;
  SDL_GameController *pad;
  int                running;

  /* ---- combo START+R1: bufferiza os dois botoes p/ nao vazar PAUSE ---- */
  struct { int      down; int32_t code; int64_t t0; int flushed; } pend[2];
  int              combo_armed;       /* 1 = segurou os dois, supime os UPs */

  /* ---- kill switch: SELECT+START segurados 1 s devolve ao ES ---- */
  int              held_start, held_select;
  int64_t          held_both_t0;
  int              quit_sent;

  /* ---- mouse virtual ---- */
  int              mouse_enable;
  int              mouse_on;
  mouse_kind       mouse_mode;
  int              mouse_stick;       /* 0=none 1=left 2=right 3=dpad */
  float            mouse_speed;       /* px/s em deflexao total */
  int              mouse_deadzone;    /* em unidades SDL 0..32767 */
  int32_t          toggle_a, toggle_b;/* keycodes do combo */
  int32_t          click_code;        /* keycode do clique */
  float            mx, my;            /* posicao do ponteiro (res interna) */
  int              clicking;          /* botao de clique pressionado */
  int64_t          down_ms;

  /* joystick: ultimo valor por eixo Android, p/ mandar MotionEvent completo */
  float            jaxis[SHIM_JNI_MAX_AXES];
};

/* -------- util -------- */
static void mouse_move_poll(shim_input_ctx *c);   /* fwd decl */

static int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int env_int(const char *k, int dflt) {
  const char *v = getenv(k);
  if (!v || !*v) return dflt;
  return (int)strtol(v, NULL, 10);
}

static int32_t code_of_name(const char *s) {
  if (!s || !*s) return 0;
  if (!strcasecmp(s, "start"))   return AKEYCODE_BUTTON_START;
  if (!strcasecmp(s, "select"))  return AKEYCODE_BUTTON_SELECT;
  if (!strcasecmp(s, "r1"))      return AKEYCODE_BUTTON_R1;
  if (!strcasecmp(s, "l1"))      return AKEYCODE_BUTTON_L1;
  if (!strcasecmp(s, "r2"))      return AKEYCODE_BUTTON_R2;
  if (!strcasecmp(s, "l2"))      return AKEYCODE_BUTTON_L2;
  if (!strcasecmp(s, "l3"))      return AKEYCODE_BUTTON_THUMBL;
  if (!strcasecmp(s, "r3"))      return AKEYCODE_BUTTON_THUMBR;
  if (!strcasecmp(s, "a"))       return AKEYCODE_BUTTON_A;
  if (!strcasecmp(s, "b"))       return AKEYCODE_BUTTON_B;
  if (!strcasecmp(s, "x"))       return AKEYCODE_BUTTON_X;
  if (!strcasecmp(s, "y"))       return AKEYCODE_BUTTON_Y;
  return 0;
}

/* -------- mapeamento SDL -> Android keycode --------
 * Cobrimos o layout do TSP (SN3026/A133P - mesmo layout do Powkiddy
 * RGB30): D-pad em HAT, ABXY/START/SELECT/L1/R1/L2/R2/L3/R3.
 * ATENCAO ao flip Xbox<->Android: SDL A == botao de baixo == Android BUTTON_B. */
static int32_t map_button(SDL_GameControllerButton b) {
  switch (b) {
    case SDL_CONTROLLER_BUTTON_A:              return AKEYCODE_BUTTON_B;
    case SDL_CONTROLLER_BUTTON_B:              return AKEYCODE_BUTTON_A;
    case SDL_CONTROLLER_BUTTON_X:              return AKEYCODE_BUTTON_Y;
    case SDL_CONTROLLER_BUTTON_Y:              return AKEYCODE_BUTTON_X;
    case SDL_CONTROLLER_BUTTON_BACK:           return AKEYCODE_BUTTON_SELECT;
    case SDL_CONTROLLER_BUTTON_START:          return AKEYCODE_BUTTON_START;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:      return AKEYCODE_BUTTON_THUMBL;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:     return AKEYCODE_BUTTON_THUMBR;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:   return AKEYCODE_BUTTON_L1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:  return AKEYCODE_BUTTON_R1;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:        return AKEYCODE_DPAD_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:      return AKEYCODE_DPAD_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:      return AKEYCODE_DPAD_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:     return AKEYCODE_DPAD_RIGHT;
    case SDL_CONTROLLER_BUTTON_GUIDE:          return AKEYCODE_HOME;
    default:                                   return AKEYCODE_UNKNOWN;
  }
}

/* SDL scancode -> Android keycode p/ teclado fisico (USB-HID / devbox). */
static int32_t map_scancode(SDL_Scancode s) {
  switch (s) {
    case SDL_SCANCODE_ESCAPE:     return AKEYCODE_ESCAPE;
    case SDL_SCANCODE_RETURN:     return 66;   /* AKEYCODE_ENTER */
    case SDL_SCANCODE_BACKSPACE:  return 67;   /* AKEYCODE_DEL */
    case SDL_SCANCODE_SPACE:      return 62;   /* AKEYCODE_SPACE */
    case SDL_SCANCODE_UP:         return AKEYCODE_DPAD_UP;
    case SDL_SCANCODE_DOWN:       return AKEYCODE_DPAD_DOWN;
    case SDL_SCANCODE_LEFT:       return AKEYCODE_DPAD_LEFT;
    case SDL_SCANCODE_RIGHT:      return AKEYCODE_DPAD_RIGHT;
    default: break;
  }
  if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return 29 + (s - SDL_SCANCODE_A);
  if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return 8  + (s - SDL_SCANCODE_1);
  return AKEYCODE_UNKNOWN;
}

/* =====================================================================
 * INJETOR JNI - transforma o estado em objetos Java falsos e chama o
 * nativeInjectEvent do Unity. Sem 'inject' armado (bridge legado, teste
 * headless) caimos só na AInputQueue, como antes.
 * ===================================================================== */

static void ev_common(shim_jni_event *e, int64_t ms) {
  e->event_ms  = ms;
  e->device_id = 0;
  e->flags     = 0;
  e->meta_state = 0;
}

static void inject_key(shim_input_ctx *c, int32_t action, int32_t keycode,
                       int32_t scancode) {
  if (!c->inject || !c->ev_key) return;
  int64_t ms = now_ms();
  shim_jni_event *e = &c->kev;
  memset(e, 0, sizeof *e);
  e->is_key   = 1;
  e->action   = action;
  e->key_code = keycode;
  e->key_scan = scancode;
  e->source   = AINPUT_SOURCE_GAMEPAD;
  if (action == AKEY_EVENT_ACTION_DOWN) { e->down_ms = ms; e->key_repeat = 0; }
  else { e->down_ms = c->kev.down_ms ? c->kev.down_ms : ms; e->key_repeat = 0; }
  ev_common(e, ms);
  c->inject(c->env, NULL, c->ev_key);
}

/* Posiciona um ponteiro no evento de movimento. */
static void mev_set_ptr(shim_jni_event *e, int idx, int32_t id, float x, float y,
                        int32_t tool) {
  if (idx < 0 || idx >= MOUSE_MAX_PTR) return;
  e->ptr[idx].id = id;
  e->ptr[idx].x = x;  e->ptr[idx].y = y;
  e->ptr[idx].pressure = (tool == AMOTION_EVENT_TOOL_TYPE_FINGER) ? 1.f : 0.f;
  e->ptr[idx].size = 1.f;
  e->ptr[idx].tool_type = tool;
  if (idx + 1 > e->n_ptrs) e->n_ptrs = idx + 1;
}

static void inject_motion(shim_input_ctx *c, int32_t action, int32_t id,
                          float x, float y, int32_t source, int32_t tool,
                          int pointer_index_for_action) {
  if (!c->inject || !c->ev_motion) return;
  int64_t ms = now_ms();
  shim_jni_event *e = &c->mev;
  /* NAO memset: e->axis[] e os ponteiros anteriores precisam sobreviver
   * (o Android manda o estado completo em cada MotionEvent). */
  e->is_key = 0;
  e->action = action;
  if (action == AMOTION_EVENT_ACTION_DOWN) e->down_ms = ms;
  if (e->down_ms == 0) e->down_ms = ms;
  e->source = source;
  e->n_ptrs = 0;
  /* para POINTER_DOWN/UP o action carrega o index deslocado <<8 */
  if (action == AMOTION_EVENT_ACTION_POINTER_DOWN ||
      action == AMOTION_EVENT_ACTION_POINTER_UP)
    e->action |= (pointer_index_for_action << AMOTION_EVENT_ACTION_POINTER_SHIFT);
  mev_set_ptr(e, 0, id, x, y, tool);
  ev_common(e, ms);
  c->inject(c->env, NULL, c->ev_motion);
}

/* MotionEvent de joystick: ACTION_MOVE com TODOS os eixos atuais. */
static void inject_joystick(shim_input_ctx *c) {
  if (!c->inject || !c->ev_motion) return;
  int64_t ms = now_ms();
  shim_jni_event *e = &c->mev;
  e->is_key = 0;
  e->action = AMOTION_EVENT_ACTION_MOVE;
  if (e->down_ms == 0) e->down_ms = ms;
  e->source = AINPUT_SOURCE_JOYSTICK;
  e->n_ptrs = 1;
  mev_set_ptr(e, 0, 0, 0.f, 0.f, AMOTION_EVENT_TOOL_TYPE_UNKNOWN);
  for (int i = 0; i < SHIM_JNI_MAX_AXES; i++) e->axis[i] = c->jaxis[i];
  ev_common(e, ms);
  c->inject(c->env, NULL, c->ev_motion);
}

/* =====================================================================
 * MOUSE VIRTUAL
 * ===================================================================== */

static const char *mouse_state_name(shim_input_ctx *c) {
  return c->mouse_on ? (c->mouse_mode == MS_MOUSE ? "MOUSE" : "TOUCH") : "OFF";
}

static void mouse_toggle(shim_input_ctx *c) {
  /* se virou o estado com o botao de clique pressionado, solta o toque ANTES:
   * senao o jogo fica com um finger DOWN eterno (menu arrastando sozinho). */
  if (c->clicking) {
    c->clicking = 0;
    inject_motion(c, AMOTION_EVENT_ACTION_UP, 0, c->mx, c->my,
                  c->mouse_mode == MS_MOUSE ? AINPUT_SOURCE_MOUSE
                                            : AINPUT_SOURCE_TOUCHSCREEN,
                  c->mouse_mode == MS_MOUSE ? AMOTION_EVENT_TOOL_TYPE_MOUSE
                                            : AMOTION_EVENT_TOOL_TYPE_FINGER, 0);
  }
  c->mouse_on = !c->mouse_on;
  c->clicking = 0;
  if (c->mouse_on) { c->mx = c->screen_w * 0.5f; c->my = c->screen_h * 0.5f; }
  fprintf(stderr, "[input] >> MOUSE VIRTUAL %s (ponteiro %.0fx%.0f, "
          "clique=B, liga/desliga=START+R1)\n",
          c->mouse_on ? "LIGADO" : "DESLIGADO", c->mx, c->my);
  (void)mouse_state_name(c);
}

static void mouse_clamp(shim_input_ctx *c) {
  if (c->mx < 0) c->mx = 0;
  if (c->my < 0) c->my = 0;
  if (c->mx > c->screen_w - 1) c->mx = (float)(c->screen_w - 1);
  if (c->my > c->screen_h - 1) c->my = (float)(c->screen_h - 1);
}

/* Move o ponteiro e injeta o movimento (hover, ou drag se clicado). */
static void mouse_move(shim_input_ctx *c, float dx, float dy) {
  if (dx == 0.f && dy == 0.f) return;
  c->mx += dx; c->my += dy;
  mouse_clamp(c);
  int tool = (c->mouse_mode == MS_MOUSE) ? AMOTION_EVENT_TOOL_TYPE_MOUSE
                                        : AMOTION_EVENT_TOOL_TYPE_FINGER;
  int source = (c->mouse_mode == MS_MOUSE) ? AINPUT_SOURCE_MOUSE
                                          : AINPUT_SOURCE_TOUCHSCREEN;
  if (c->mouse_mode == MS_MOUSE) {
    inject_motion(c, AMOTION_EVENT_ACTION_HOVER_MOVE, 0, c->mx, c->my,
                  source, tool, 0);
  } else if (c->clicking) {
    /* em modo touch so existe movimento com o dedo em contato */
    inject_motion(c, AMOTION_EVENT_ACTION_MOVE, 0, c->mx, c->my,
                  source, tool, 0);
  }
}

static void mouse_click_down(shim_input_ctx *c) {
  if (c->clicking) return;
  c->clicking = 1;
  if (c->mouse_mode == MS_MOUSE) {
    inject_motion(c, AMOTION_EVENT_ACTION_DOWN, 0, c->mx, c->my,
                  AINPUT_SOURCE_MOUSE, AMOTION_EVENT_TOOL_TYPE_MOUSE, 0);
    inject_motion(c, AMOTION_EVENT_ACTION_BUTTON_DOWN, 0, c->mx, c->my,
                  AINPUT_SOURCE_MOUSE, AMOTION_EVENT_TOOL_TYPE_MOUSE, 0);
  } else {
    inject_motion(c, AMOTION_EVENT_ACTION_DOWN, 0, c->mx, c->my,
                  AINPUT_SOURCE_TOUCHSCREEN, AMOTION_EVENT_TOOL_TYPE_FINGER, 0);
  }
  fprintf(stderr, "[input] mouse: clique DOWN em %.0f,%.0f\n", c->mx, c->my);
}

static void mouse_click_up(shim_input_ctx *c) {
  if (!c->clicking) return;
  c->clicking = 0;
  if (c->mouse_mode == MS_MOUSE) {
    inject_motion(c, AMOTION_EVENT_ACTION_BUTTON_UP, 0, c->mx, c->my,
                  AINPUT_SOURCE_MOUSE, AMOTION_EVENT_TOOL_TYPE_MOUSE, 0);
    inject_motion(c, AMOTION_EVENT_ACTION_UP, 0, c->mx, c->my,
                  AINPUT_SOURCE_MOUSE, AMOTION_EVENT_TOOL_TYPE_MOUSE, 0);
  } else {
    inject_motion(c, AMOTION_EVENT_ACTION_UP, 0, c->mx, c->my,
                  AINPUT_SOURCE_TOUCHSCREEN, AMOTION_EVENT_TOOL_TYPE_FINGER, 0);
  }
  fprintf(stderr, "[input] mouse: clique UP\n");
}

/* Deflexao de analogo em -1..1 com deadzone. */
static float axis_norm(int raw, int dz) {
  if (raw > 32767) raw = 32767;
  if (raw < -32768) raw = -32768;
  if (raw > -dz && raw < dz) return 0.f;
  float sgn = raw < 0 ? -1.f : 1.f;
  float v = (float)(raw < 0 ? -raw : raw) - (float)dz;
  float span = 32767.f - (float)dz;
  if (span < 1.f) span = 1.f;
  float n = v / span;
  if (n > 1.f) n = 1.f;
  return sgn * n;
}

/* =====================================================================
 * DISPATCH - cada evento SDL gera as duas rotas (JNI + AInputQueue)
 * ===================================================================== */

static void queue_key(shim_input_ctx *c, int32_t action, int32_t keycode,
                      int32_t scancode) {
  AInputEvent *ev = shim_input_event_alloc(AINPUT_EVENT_TYPE_KEY,
                                           AINPUT_SOURCE_GAMEPAD);
  if (!ev) return;
  shim_input_key_fill(ev, action, keycode, scancode);
  shim_input_queue_push(shim_input_queue_global(), ev);
}

static void push_key(shim_input_ctx *c, int32_t action, int32_t keycode,
                     int32_t scancode) {
  if (keycode == AKEYCODE_UNKNOWN) return;
  inject_key(c, action, keycode, scancode);
  queue_key(c, action, keycode, scancode);
}

static void push_motion_axis(shim_input_ctx *c, int32_t axis, float value) {
  if (axis >= 0 && axis < SHIM_JNI_MAX_AXES) c->jaxis[axis] = value;
  inject_joystick(c);
  AInputEvent *ev = shim_input_event_alloc(AINPUT_EVENT_TYPE_MOTION,
                                           AINPUT_SOURCE_JOYSTICK);
  if (!ev) return;
  shim_input_motion_resize(ev, 1);
  shim_input_motion_set_action(ev, AMOTION_EVENT_ACTION_MOVE);
  shim_input_motion_set_pointer(ev, 0, 0, 0.f, 0.f, 0.f, 0.f,
                                AMOTION_EVENT_TOOL_TYPE_UNKNOWN);
  shim_input_motion_set_axis(ev, axis, value);
  shim_input_queue_push(shim_input_queue_global(), ev);
}

static void push_touch(shim_input_ctx *c, int32_t action, int32_t id,
                       float x, float y) {
  int tool = AMOTION_EVENT_TOOL_TYPE_FINGER;
  inject_motion(c, action, id, x, y, AINPUT_SOURCE_TOUCHSCREEN, tool, 0);
  AInputEvent *ev = shim_input_event_alloc(AINPUT_EVENT_TYPE_MOTION,
                                           AINPUT_SOURCE_TOUCHSCREEN);
  if (!ev) return;
  shim_input_motion_resize(ev, 1);
  shim_input_motion_set_action(ev, action);
  shim_input_motion_set_pointer(ev, 0, id, x, y, 1.f, 1.f, tool);
  shim_input_queue_push(shim_input_queue_global(), ev);
}

/* ---- combo START+R1 com buffer de 400 ms ----
 * START e R1 individually sao botoes legitimos do jogo (pause / arma a
 * direita). So quando os dois estao juntos dentro do buffer e que vira
 * toggle - ai NENHUM dos dois e entregue, senao o menu abriria junto. */
#define COMBO_WINDOW_MS 400

static int pend_slot(shim_input_ctx *c, int32_t code) {
  for (int i = 0; i < 2; i++) if (c->pend[i].down && c->pend[i].code == code) return i;
  for (int i = 0; i < 2; i++) if (!c->pend[i].down) return i;
  return -1;
}

static int is_combo_key(shim_input_ctx *c, int32_t code) {
  return code == c->toggle_a || code == c->toggle_b;
}

static void pend_flush(shim_input_ctx *c) {
  for (int i = 0; i < 2; i++) {
    if (c->pend[i].down && c->pend[i].flushed == 0) {
      c->pend[i].flushed = 1;
      push_key(c, AKEY_EVENT_ACTION_DOWN, c->pend[i].code, 0);
    }
  }
}

/* Expira pendencia: se os dois nao foram pressionados a tempo, libera. */
static void pend_tick(shim_input_ctx *c) {
  if (!c->combo_armed) {
    int64_t ms = now_ms();
    for (int i = 0; i < 2; i++)
      if (c->pend[i].down && c->pend[i].flushed == 0 &&
          ms - c->pend[i].t0 >= COMBO_WINDOW_MS)
        pend_flush(c);
  }
  /* kill switch SELECT+START (mesma convencao do gptokeyb do PortMaster) */
  if (c->held_start && c->held_select) {
    if (c->held_both_t0 == 0) c->held_both_t0 = now_ms();
    else if (!c->quit_sent && now_ms() - c->held_both_t0 >= 1000) {
      c->quit_sent = 1;
      fprintf(stderr, "[input] SELECT+START 1 s -> pedindo SAIDA do port\n");
      atomic_store(&c->stop, 1);
    }
  } else c->held_both_t0 = 0;
}

static void on_combo_down(shim_input_ctx *c, int32_t code) {
  int other = (code == c->toggle_a) ? c->toggle_b : c->toggle_a;
  int slot = pend_slot(c, code);
  if (slot >= 0) {
    c->pend[slot].down = 1; c->pend[slot].code = code;
    c->pend[slot].t0 = now_ms(); c->pend[slot].flushed = 0;
  }
  int oslot = -1;
  for (int i = 0; i < 2; i++)
    if (c->pend[i].down && c->pend[i].code == other) oslot = i;
  if (oslot >= 0) {
    /* combo completo: engole os dois (nao envia DOWN de nenhum). Excecao:
     * se um dos dois JA FOI entregue (estourou a janela de 400 ms antes do
     * segundo apertar), tem que mandar o UP dele AGORA - senao o jogo ficaria
     * com o botao pressionado para sempre (pause grudado). */
    c->combo_armed = 1;
    for (int i = 0; i < 2; i++) {
      if (!c->pend[i].down) continue;
      if (c->pend[i].flushed == 1)              /* o jogo viu o DOWN -> devolve o UP */
        push_key(c, AKEY_EVENT_ACTION_UP, c->pend[i].code, 0);
      c->pend[i].flushed = 2;   /* 2 = ja resolvido pelo combo: nao reenvia */
    }
    mouse_toggle(c);
  }
}

static void on_combo_up(shim_input_ctx *c, int32_t code) {
  int slot = -1;
  for (int i = 0; i < 2; i++) if (c->pend[i].down && c->pend[i].code == code) slot = i;
  if (slot < 0) return;
  /* flushed: 0 = DOWN nunca foi entregue (combo engolido, nao manda nada)
   *          1 = DOWN foi entregue (tem que mandar o UP, senao o botao fica
   *              grudado no jogo)
   *          2 = ja resolvido quando o combo fechou (UP enviado la) */
  int st = c->pend[slot].flushed;
  c->pend[slot].down = 0; c->pend[slot].code = 0; c->pend[slot].flushed = 0;
  if (st == 1) push_key(c, AKEY_EVENT_ACTION_UP, code, 0);
  int any = 0;
  for (int i = 0; i < 2; i++) if (c->pend[i].down) any = 1;
  if (!any) c->combo_armed = 0;
}

static void handle_button_down(shim_input_ctx *c, int32_t code) {
  if (!code) return;
  if (code == AKEYCODE_BUTTON_START)  c->held_start  = 1;
  if (code == AKEYCODE_BUTTON_SELECT) c->held_select = 1;
  if (c->mouse_enable && c->mouse_on && code == c->click_code) {
    mouse_click_down(c);
    return;                        /* B vira clique, nao vai ao jogo */
  }
  if (c->mouse_enable && is_combo_key(c, code)) { on_combo_down(c, code); return; }
  push_key(c, AKEY_EVENT_ACTION_DOWN, code, 0);
}

static void handle_button_up(shim_input_ctx *c, int32_t code) {
  if (!code) return;
  if (code == AKEYCODE_BUTTON_START)  c->held_start  = 0;
  if (code == AKEYCODE_BUTTON_SELECT) c->held_select = 0;
  if (c->mouse_enable && c->mouse_on && code == c->click_code) {
    mouse_click_up(c);
    return;
  }
  if (c->mouse_enable && is_combo_key(c, code)) { on_combo_up(c, code); return; }
  push_key(c, AKEY_EVENT_ACTION_UP, code, 0);
}

/* Analogo -> ponteiro (quando o mouse esta ligado) ou -> eixo do jogo. */
static void handle_axis(shim_input_ctx *c, int which, int raw) {
  float v = (float)raw / 32767.f;
  int ax = -1;
  switch (which) {
    case SDL_CONTROLLER_AXIS_LEFTX:        ax = AMOTION_EVENT_AXIS_X;  break;
    case SDL_CONTROLLER_AXIS_LEFTY:        ax = AMOTION_EVENT_AXIS_Y;  break;
    case SDL_CONTROLLER_AXIS_RIGHTX:       ax = AMOTION_EVENT_AXIS_RX; break;
    case SDL_CONTROLLER_AXIS_RIGHTY:       ax = AMOTION_EVENT_AXIS_RY; break;
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT:  ax = AMOTION_EVENT_AXIS_LTRIGGER; break;
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: ax = AMOTION_EVENT_AXIS_RTRIGGER; break;
    default: return;
  }
  /* L2/R2 em SDL vem 0..32767 -> normalizar p/ 0..1 */
  if (ax == AMOTION_EVENT_AXIS_LTRIGGER || ax == AMOTION_EVENT_AXIS_RTRIGGER)
    v = (float)(raw + 32768) / 65535.f;

  int is_cur = 0;
  if (c->mouse_enable && c->mouse_on) {
    if (c->mouse_stick == 2 && (which == SDL_CONTROLLER_AXIS_RIGHTX ||
                                which == SDL_CONTROLLER_AXIS_RIGHTY)) is_cur = 1;
    if (c->mouse_stick == 1 && (which == SDL_CONTROLLER_AXIS_LEFTX ||
                                which == SDL_CONTROLLER_AXIS_LEFTY))  is_cur = 1;
  }
  if (is_cur) {
    float dx = (which == SDL_CONTROLLER_AXIS_RIGHTX ||
                which == SDL_CONTROLLER_AXIS_LEFTX)
             ? axis_norm(raw, c->mouse_deadzone) * c->mouse_speed / 60.f : 0.f;
    float dy = (which == SDL_CONTROLLER_AXIS_RIGHTY ||
                which == SDL_CONTROLLER_AXIS_LEFTY)
             ? axis_norm(raw, c->mouse_deadzone) * c->mouse_speed / 60.f : 0.f;
    mouse_move(c, dx, dy);
    return;                        /* nao vira eixo do jogo */
  }
  push_motion_axis(c, ax, v);
}

static void handle_dpad(shim_input_ctx *c, int hat_x, int hat_y) {
  /* D-pad = 4 teclas no Android; em modo mouse com stick=dpad ele vira
   * ponteiro e nao gera key events. */
  if (c->mouse_enable && c->mouse_on && c->mouse_stick == 3) {
    float step = c->mouse_speed / 30.f;
    mouse_move(c, hat_x * step, hat_y * step);
    return;
  }
  static const int32_t codes[4] = { AKEYCODE_DPAD_LEFT, AKEYCODE_DPAD_RIGHT,
                                    AKEYCODE_DPAD_UP,   AKEYCODE_DPAD_DOWN };
  static int8_t held[4] = {0,0,0,0};
  int cur[4] = { hat_x < 0, hat_x > 0, hat_y < 0, hat_y > 0 };
  for (int i = 0; i < 4; i++) {
    if (cur[i] && !held[i]) { held[i] = 1; handle_button_down(c, codes[i]); }
    else if (!cur[i] && held[i]) { held[i] = 0; handle_button_up(c, codes[i]); }
  }
}

static void handle_event(shim_input_ctx *c, SDL_Event *e) {
  switch (e->type) {
    case SDL_QUIT:
      atomic_store(&c->stop, 1);
      break;

    case SDL_CONTROLLERBUTTONDOWN:
      handle_button_down(c, map_button((SDL_GameControllerButton)e->cbutton.button));
      break;
    case SDL_CONTROLLERBUTTONUP:
      handle_button_up(c, map_button((SDL_GameControllerButton)e->cbutton.button));
      break;

    case SDL_CONTROLLERAXISMOTION:
      handle_axis(c, e->caxis.axis, e->caxis.value);
      break;

    case SDL_JOYHATMOTION: {
      /* alguns devices (sem mapa de game controller) entregam o D-pad como
       * hat: vira as 4 teclas DPAD_*. */
      int hx = (e->jhat.value & SDL_HAT_LEFT)  ? -1 :
               (e->jhat.value & SDL_HAT_RIGHT) ?  1 : 0;
      int hy = (e->jhat.value & SDL_HAT_UP)    ? -1 :
               (e->jhat.value & SDL_HAT_DOWN)  ?  1 : 0;
      handle_dpad(c, hx, hy);
      break;
    }

    case SDL_KEYDOWN:
    case SDL_KEYUP: {
      int32_t kc = map_scancode(e->key.keysym.scancode);
      if (kc == AKEYCODE_UNKNOWN) break;
      push_key(c, e->type == SDL_KEYDOWN ? AKEY_EVENT_ACTION_DOWN
                                         : AKEY_EVENT_ACTION_UP,
               kc, e->key.keysym.scancode);
      break;
    }

    case SDL_FINGERDOWN:
    case SDL_FINGERUP:
    case SDL_FINGERMOTION: {
      int32_t action = (e->type == SDL_FINGERDOWN) ? AMOTION_EVENT_ACTION_DOWN
                     : (e->type == SDL_FINGERUP)   ? AMOTION_EVENT_ACTION_UP
                                                   : AMOTION_EVENT_ACTION_MOVE;
      float x = e->tfinger.x * c->screen_w;
      float y = e->tfinger.y * c->screen_h;
      push_touch(c, action, (int32_t)e->tfinger.fingerId, x, y);
      break;
    }

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEMOTION: {
      /* Mouse fisico (devbox / adaptador USB no TSP). No TrimUI Smart Pro
       * isto e inexistente - e para isso serve o mouse virtual acima. */
      if (e->type == SDL_MOUSEMOTION) {
        c->mx = e->motion.x; c->my = e->motion.y;
        if (e->motion.state & SDL_BUTTON_LMASK)
          push_touch(c, AMOTION_EVENT_ACTION_MOVE, 0, c->mx, c->my);
      } else if (e->button.button == SDL_BUTTON_LEFT) {
        push_touch(c, e->button.state ? AMOTION_EVENT_ACTION_DOWN
                                      : AMOTION_EVENT_ACTION_UP,
                   0, e->button.x, e->button.y);
      }
      break;
    }

    default: break;
  }
}

static void *poll_thread(void *arg) {
  shim_input_ctx *c = (shim_input_ctx*)arg;
  SDL_Event e;
  SDL_JoystickID which = -1;   /* ignorado: usamos so os enums do ctx */
  fprintf(stderr, "bridge_input: thread started (inject=%s env=%s mouse=%s)\n",
          c->inject ? "nativeInjectEvent" : "AInputQueue apenas",
          c->env ? "ok" : "NULL",
          c->mouse_enable ? (c->mouse_mode == MS_MOUSE ? "mouse" : "touch")
                          : "desligado");
  while (!atomic_load(&c->stop)) {
    int r = SDL_WaitEventTimeout(&e, 16);
    if (r) {
      do {
        if (e.type == SDL_CONTROLLERDEVICEADDED && !c->pad) {
          c->pad = SDL_GameControllerOpen(e.cdevice.which);
        }
        handle_event(c, &e);
      } while (!atomic_load(&c->stop) && SDL_PollEvent(&e));
    }
    pend_tick(c);
    /* ponteiro por inercia: se o stick estiver deflectido mas nenhum evento
     * novo chegou, ainda queremos movimento continuo a 60 Hz. */
    if (c->mouse_enable && c->mouse_on && c->mouse_stick != 0)
      mouse_move_poll(c);
  }
  (void)which;
  fprintf(stderr, "bridge_input: thread stopped\n");
  return NULL;
}

/* Le o estado ATUAL dos analogos direto do controller SDL (nao depende de
 * evento), assim o ponteiro anda suave mesmo entre eventos. */
static void mouse_move_poll(shim_input_ctx *c) {
  if (!c->pad) return;
  int sx = 0, sy = 0;
  if (c->mouse_stick == 2) {
    sx = SDL_GameControllerGetAxis(c->pad, SDL_CONTROLLER_AXIS_RIGHTX);
    sy = SDL_GameControllerGetAxis(c->pad, SDL_CONTROLLER_AXIS_RIGHTY);
  } else if (c->mouse_stick == 1) {
    sx = SDL_GameControllerGetAxis(c->pad, SDL_CONTROLLER_AXIS_LEFTX);
    sy = SDL_GameControllerGetAxis(c->pad, SDL_CONTROLLER_AXIS_LEFTY);
  } else return;
  float dx = axis_norm(sx, c->mouse_deadzone) * c->mouse_speed / 60.f;
  float dy = axis_norm(sy, c->mouse_deadzone) * c->mouse_speed / 60.f;
  if (dx || dy) mouse_move(c, dx, dy);
}

/* -------- API publica -------- */

__attribute__((unused))
static void *try_sym(void *h, const char *const names[], int n) {
  if (!h) return NULL;
  for (int i = 0; i < n; i++) {
    void *p = dlsym(h, names[i]);
    if (p) return p;
  }
  return NULL;
}

shim_input_status shim_input_start(shim_input_ctx **out, void *unity_handle,
                                   int screen_w, int screen_h) {
  (void)unity_handle;   /* nao ha UnitySend* neste build: ver cabecalho */
  if (!out) return SHIM_INPUT_NO_QUEUE;
  shim_input_ctx *c = calloc(1, sizeof(*c));
  if (!c) return SHIM_INPUT_NO_THREAD;
  c->screen_w = screen_w > 0 ? screen_w : 640;
  c->screen_h = screen_h > 0 ? screen_h : 480;
  atomic_init(&c->stop, 0);

  /* ---- config do mouse virtual ---- */
  c->mouse_enable  = env_int("ULTRAKILL_MOUSE", 1);
  c->mouse_speed   = (float)env_int("ULTRAKILL_MOUSE_SPEED", 1100);
  c->mouse_deadzone = env_int("ULTRAKILL_MOUSE_DEADZONE", 5000);
  {
    const char *st = getenv("ULTRAKILL_MOUSE_STICK");
    if (!st || !*st) st = "right";
    c->mouse_stick = !strcasecmp(st, "left") ? 1 : !strcasecmp(st, "right") ? 2
                   : !strcasecmp(st, "dpad") ? 3 : 0;
    const char *md = getenv("ULTRAKILL_MOUSE_MODE");
    c->mouse_mode  = (md && !strcasecmp(md, "mouse")) ? MS_MOUSE : MS_TOUCH;
    char buf[64];
    const char *tg = getenv("ULTRAKILL_MOUSE_TOGGLE");
    snprintf(buf, sizeof buf, "%s", tg && *tg ? tg : "start+r1");
    char *plus = strchr(buf, '+');
    if (plus) {
      *plus = 0;
      c->toggle_a = code_of_name(buf);
      c->toggle_b = code_of_name(plus + 1);
    } else { c->toggle_a = AKEYCODE_BUTTON_START; c->toggle_b = AKEYCODE_BUTTON_R1; }
    const char *ck = getenv("ULTRAKILL_MOUSE_CLICK");
    c->click_code = code_of_name(ck && *ck ? ck : "b");
    if (!c->toggle_a || !c->toggle_b) {
      c->toggle_a = AKEYCODE_BUTTON_START; c->toggle_b = AKEYCODE_BUTTON_R1;
    }
  }
  if (c->mouse_speed < 100.f)  c->mouse_speed = 100.f;
  if (c->mouse_speed > 4000.f) c->mouse_speed = 4000.f;
  if (c->mouse_deadzone < 0)   c->mouse_deadzone = 0;
  if (c->mouse_deadzone > 30000) c->mouse_deadzone = 30000;
  c->mx = c->screen_w * 0.5f; c->my = c->screen_h * 0.5f;

  fprintf(stderr, "[input] mouse virtual: enable=%d modo=%s stick=%s "
          "velocidade=%.0f px/s zona morta=%d combo=%d+%d clique=%d\n",
          c->mouse_enable,
          c->mouse_mode == MS_MOUSE ? "mouse" : "touch",
          c->mouse_stick == 1 ? "esquerdo" : c->mouse_stick == 2 ? "direito"
          : c->mouse_stick == 3 ? "d-pad" : "nenhum",
          c->mouse_speed, c->mouse_deadzone,
          (int)c->toggle_a, (int)c->toggle_b, (int)c->click_code);

  /* ---- envelopes de evento (permanentes, mutados a cada injecao) ---- */
  c->ev_key    = shim_jni_make_event(&c->kev, 1);
  c->ev_motion = shim_jni_make_event(&c->mev, 0);

  /* Garante SDL subsystems. NAO inicializamos VIDEO de novo - bridge_gl.c
   * ja fez. */
  SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    if (SDL_IsGameController(i)) {
      c->pad = SDL_GameControllerOpen(i);
      if (c->pad) {
        fprintf(stderr, "bridge_input: gamepad[%d]='%s'\n",
                i, SDL_GameControllerName(c->pad));
        break;
      }
    }
  }
  if (!c->pad)
    fprintf(stderr, "bridge_input: nenhum gamepad SDL ainda - "
                    "aguardando SDL_CONTROLLERDEVICEADDED (ou teclado/mouse)\n");

  if (pthread_create(&c->thread, NULL, poll_thread, c) != 0) {
    free(c);
    return SHIM_INPUT_NO_THREAD;
  }
  c->running = 1;
  *out = c;
  return SHIM_INPUT_OK;
}

/* bridge_main chama depois do JNI_OnLoad, quando o UnityPlayer ja registrou
 * os natives dele. inject = shim_jni_find_native(UP,"nativeInjectEvent"). */
__attribute__((visibility("default")))
void shim_input_set_unity_inject(shim_input_ctx *c, void *inject, void *env) {
  if (!c) return;
  c->inject = (unity_inject_fn)inject;
  c->env    = (JNIEnv*)env;
  fprintf(stderr, "[input] porta de input: %s\n",
          c->inject ? "UnityPlayer.nativeInjectEvent (JNI)" : "(nenhuma - fila AInputQueue)");
}

__attribute__((visibility("default")))
int shim_input_mouse_active(shim_input_ctx *c) {
  return c ? (c->mouse_enable && c->mouse_on) : 0;
}

/* 1 = o usuario pediu saida (SDL_QUIT ou SELECT+START 1 s). O loop de
 * render do bridge_main olha isto a cada frame. */
__attribute__((visibility("default")))
int shim_input_quit_requested(shim_input_ctx *c) {
  return c ? atomic_load(&c->stop) : 0;
}

void shim_input_stop(shim_input_ctx *c) {
  if (!c) return;
  atomic_store(&c->stop, 1);
  if (c->running) pthread_join(c->thread, NULL);
  if (c->pad) SDL_GameControllerClose(c->pad);
  SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
  free(c);
}
