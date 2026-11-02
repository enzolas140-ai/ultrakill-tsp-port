/* bridge_input.c - thread SDL -> AInputQueue.
 *
 * Ciclo de vida:
 *   shim_input_start:
 *     1. SDL_Init SUBSYSTEM_GAMECTRL + KEYBOARD_MOUSE_HOOK
 *     2. Abre o primeiro gamepad (TSP tem 1 controller embutido)
 *     3. Spawna thread_poll
 *   thread_poll:
 *     - SDL_WaitEventTimeout (sleep real, nao busy-wait - poupa A53)
 *     - Switch SDL_Event -> monta AInputEvent via shim_input_*_fill
 *     - shim_input_queue_push(global)
 *   shim_input_stop:
 *     - flag stops=1, ALooper_wake, join
 *
 * Duas rotas de dispatch:
 *   A. Se libunity.so exporta UnitySendTouchEvent/UnitySendKeyDown/etc.
 *      (varia por versao Unity), chamamos direto. Mais barato e coerente
 *      com o caminho de input real do Unity Player.
 *   B. Fallback: empilha em AInputQueue global. Unity le via
 *      AInputQueue_getEvent se ele usa android_native_app_glue.
 *
 * Fazemos AMBOS: se um dos paths nao funcionar o outro cobre.
 * (Acho que Unity 2019.4 Android Player usa A; android_native_app_glue
 *  nao linka dentro do libunity.so real, entao B pode ficar dormente.
 *  Mesmo assim mantemos B porque o custo e zero.) */
#include <bridge_input.h>
#include <android_shim.h>

#include <dlfcn.h>
#include <stdatomic.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* -------- typedefs dos simbolos Unity opcionais -------- */
typedef void (*fn_touch_begin)(int32_t id, float x, float y, int32_t finger);
typedef void (*fn_touch_move )(int32_t id, float x, float y, int32_t finger);
typedef void (*fn_touch_end  )(int32_t id, float x, float y, int32_t finger);
typedef void (*fn_key_down   )(int32_t keycode);
typedef void (*fn_key_up     )(int32_t keycode);
typedef void (*fn_axis_set   )(int32_t axisIndex, float value);

struct shim_input_ctx {
  void                *unity_handle;
  fn_touch_begin       u_touch_begin;
  fn_touch_move        u_touch_move;
  fn_touch_end         u_touch_end;
  fn_key_down          u_key_down;
  fn_key_up            u_key_up;
  fn_axis_set          u_axis_set;

  pthread_t            thread;
  atomic_int           stop;
  int                  screen_w, screen_h;
  SDL_GameController  *pad;
  int                  running;
};

/* -------- mapeamento SDL -> Android keycode --------
 * Cobrimos o layout do TSP (SN3026/A133P - same layout que Powkiddy
 * RGB30): D-pad em HAT, ABXY/START/SELECT/L1/R1/L2/R2/L3/R3.
 * Outros devices vao pro default AKEYCODE_UNKNOWN e logam; voce
 * adiciona quando ver nos Player.log. */
static int32_t map_button(SDL_GameControllerButton b) {
  switch (b) {
    case SDL_CONTROLLER_BUTTON_A:              return AKEYCODE_BUTTON_B;      /* SDL A == Android B */
    case SDL_CONTROLLER_BUTTON_B:              return AKEYCODE_BUTTON_A;      /* SDL B == Android A */
    case SDL_CONTROLLER_BUTTON_X:              return AKEYCODE_BUTTON_Y;      /* cross-platform swap */
    case SDL_CONTROLLER_BUTTON_Y:              return AKEYCODE_BUTTON_X;
    case SDL_CONTROLLER_BUTTON_BACK:         return AKEYCODE_BUTTON_SELECT;
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

/* SDL scancode -> Android keycode p/ teclado fisico (raro no TSP mas
 * pode aparecer se voce conectar USB-HID). */
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
  if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) {
    return 29 + (s - SDL_SCANCODE_A);  /* AKEYCODE_A = 29 */
  }
  if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) {
    return 8 + (s - SDL_SCANCODE_1);   /* AKEYCODE_1 = 8 */
  }
  return AKEYCODE_UNKNOWN;
}

/* -------- dispatch: tenta Unity symbol, cai pra AInputQueue -------- */

static void push_key(shim_input_ctx *c, int32_t action, int32_t keycode, int32_t scancode) {
  if (action == AKEY_EVENT_ACTION_DOWN && c->u_key_down && keycode != AKEYCODE_UNKNOWN) {
    c->u_key_down(keycode);
    return;
  }
  if (action == AKEY_EVENT_ACTION_UP && c->u_key_up && keycode != AKEYCODE_UNKNOWN) {
    c->u_key_up(keycode);
    return;
  }
  AInputEvent *ev = shim_input_event_alloc(AINPUT_EVENT_TYPE_KEY, AINPUT_SOURCE_GAMEPAD);
  if (!ev) return;
  shim_input_key_fill(ev, action, keycode, scancode);
  shim_input_queue_push(shim_input_queue_global(), ev);
}

static void push_motion_axis(shim_input_ctx *c, int32_t axis, float value) {
  if (c->u_axis_set) { c->u_axis_set(axis, value); return; }
  /* Fallback: motion event com 1 ponteiro + axis custom. Unity le
   * via AMotionEvent_getAxisValue. */
  AInputEvent *ev = shim_input_event_alloc(AINPUT_EVENT_TYPE_MOTION, AINPUT_SOURCE_JOYSTICK);
  if (!ev) return;
  shim_input_motion_resize(ev, 1);
  shim_input_motion_set_action(ev, AMOTION_EVENT_ACTION_MOVE);
  shim_input_motion_set_pointer(ev, 0, 0, 0.f, 0.f, 1.f, 1.f, AMOTION_EVENT_TOOL_TYPE_UNKNOWN);
  shim_input_motion_set_axis(ev, axis, value);
  shim_input_queue_push(shim_input_queue_global(), ev);
}

static void push_touch(shim_input_ctx *c, int32_t action, int32_t id, float x, float y) {
  if (c->u_touch_begin && action == AMOTION_EVENT_ACTION_DOWN)
    { c->u_touch_begin(id, x, y, 0); return; }
  if (c->u_touch_move  && action == AMOTION_EVENT_ACTION_MOVE)
    { c->u_touch_move (id, x, y, 0); return; }
  if (c->u_touch_end   && (action == AMOTION_EVENT_ACTION_UP ||
                            action == AMOTION_EVENT_ACTION_CANCEL))
    { c->u_touch_end  (id, x, y, 0); return; }
  AInputEvent *ev = shim_input_event_alloc(AINPUT_EVENT_TYPE_MOTION, AINPUT_SOURCE_TOUCHSCREEN);
  if (!ev) return;
  shim_input_motion_resize(ev, 1);
  shim_input_motion_set_action(ev, action);
  shim_input_motion_set_pointer(ev, 0, id, x, y, 1.f, 1.f, AMOTION_EVENT_TOOL_TYPE_FINGER);
  shim_input_queue_push(shim_input_queue_global(), ev);
}

/* -------- thread principal -------- */

static void handle_event(shim_input_ctx *c, SDL_Event *e) {
  switch (e->type) {
    case SDL_QUIT:
      atomic_store(&c->stop, 1);
      break;

    case SDL_CONTROLLERBUTTONDOWN:
      push_key(c, AKEY_EVENT_ACTION_DOWN, map_button((SDL_GameControllerButton)e->cbutton.button), 0);
      break;
    case SDL_CONTROLLERBUTTONUP:
      push_key(c, AKEY_EVENT_ACTION_UP,   map_button((SDL_GameControllerButton)e->cbutton.button), 0);
      break;

    case SDL_CONTROLLERAXISMOTION: {
      /* SDL game controller axis enum -> Android axis */
      int32_t ax;
      switch (e->caxis.axis) {
        case SDL_CONTROLLER_AXIS_LEFTX:        ax = AMOTION_EVENT_AXIS_X;  break;
        case SDL_CONTROLLER_AXIS_LEFTY:        ax = AMOTION_EVENT_AXIS_Y;  break;
        case SDL_CONTROLLER_AXIS_RIGHTX:       ax = AMOTION_EVENT_AXIS_RX; break;
        case SDL_CONTROLLER_AXIS_RIGHTY:       ax = AMOTION_EVENT_AXIS_RY; break;
        case SDL_CONTROLLER_AXIS_TRIGGERLEFT:  ax = AMOTION_EVENT_AXIS_LTRIGGER; break;
        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: ax = AMOTION_EVENT_AXIS_RTRIGGER; break;
        default: return;
      }
      float v = e->caxis.value / 32767.0f;
      /* L2/R2 em SDL vao de 0..32767 -> normalizar p/ 0..1 */
      if (ax == AMOTION_EVENT_AXIS_LTRIGGER || ax == AMOTION_EVENT_AXIS_RTRIGGER)
        v = (e->caxis.value + 32768) / 65535.0f;
      push_motion_axis(c, ax, v);
      break;
    }

    case SDL_KEYDOWN: {
      int32_t kc = map_scancode(e->key.keysym.scancode);
      if (kc != AKEYCODE_UNKNOWN) push_key(c, AKEY_EVENT_ACTION_DOWN, kc, e->key.keysym.scancode);
      break;
    }
    case SDL_KEYUP: {
      int32_t kc = map_scancode(e->key.keysym.scancode);
      if (kc != AKEYCODE_UNKNOWN) push_key(c, AKEY_EVENT_ACTION_UP, kc, e->key.keysym.scancode);
      break;
    }

    case SDL_FINGERDOWN:
    case SDL_FINGERUP:
    case SDL_FINGERMOTION: {
      int32_t action = (e->type == SDL_FINGERDOWN) ? AMOTION_EVENT_ACTION_DOWN
                     : (e->type == SDL_FINGERUP)   ? AMOTION_EVENT_ACTION_UP
                                                   : AMOTION_EVENT_ACTION_MOVE;
      /* SDL usa coords normalizadas 0..1 -> escala p/ resolucao interna */
      float x = e->tfinger.x * c->screen_w;
      float y = e->tfinger.y * c->screen_h;
      push_touch(c, action, (int32_t)e->tfinger.fingerId, x, y);
      break;
    }

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEMOTION: {
      /* Sem touch screen no TSP, mas se rodar em devbox voce quer
       * testar com mouse. Mapeia botao esquerdo como finger 0. */
      if (e->type == SDL_MOUSEMOTION) {
        if (!(e->motion.state & SDL_BUTTON_LMASK)) break;  /* so quando pressionado */
        push_touch(c, AMOTION_EVENT_ACTION_MOVE, 0, e->motion.x, e->motion.y);
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
  fprintf(stderr, "bridge_input: thread started (unity exports: "
          "touch=%p key=%p axis=%p)\n",
          (void*)c->u_touch_begin, (void*)c->u_key_down, (void*)c->u_axis_set);
  while (!atomic_load(&c->stop)) {
    /* WaitEventTimeout 16 ms -> ~60Hz wake, 0% CPU em ocioso. */
    int r = SDL_WaitEventTimeout(&e, 16);
    if (!r) continue;
    do {
      handle_event(c, &e);
    } while (!atomic_load(&c->stop) && SDL_PollEvent(&e));
  }
  fprintf(stderr, "bridge_input: thread stopped\n");
  return NULL;
}

/* -------- API publica -------- */

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
  if (!out) return SHIM_INPUT_NO_QUEUE;
  shim_input_ctx *c = calloc(1, sizeof(*c));
  c->unity_handle = unity_handle;
  c->screen_w = screen_w; c->screen_h = screen_h;
  atomic_init(&c->stop, 0);

  /* Descobre exports do Unity. Nomes comuns em Unity 2019.4 Android:
   *   UnitySendTouchEvent(id, x, y, type)
   *   UnitySendKeyDown(keycode)
   *   UnitySendKeyUp(keycode)
   *   UnitySendJoystickAxis(index, value)   <- nome varia p/ nativeSetJoystickAxis
   * Se nao achar (o mais provavel em libunity.so 2019.4 - Unity
   * esconde esses atras de UnityPlayer native methods), caimos em
   * AInputQueue puro. */
  static const char *names_touch_begin[] = {"UnitySendTouchBeginEvent", "UnitySendTouchEvent", NULL};
  static const char *names_touch_move [] = {"UnitySendTouchMoveEvent",  NULL};
  static const char *names_touch_end  [] = {"UnitySendTouchEndEvent",   NULL};
  static const char *names_key_down   [] = {"UnitySendKeyDown",         "UnityCallAndroidKeyDown", NULL};
  static const char *names_key_up     [] = {"UnitySendKeyUp",           "UnityCallAndroidKeyUp",   NULL};
  static const char *names_axis       [] = {"UnitySendJoystickAxis",    "nativeSetJoystickAxis",   NULL};

  c->u_touch_begin = (fn_touch_begin)try_sym(unity_handle, names_touch_begin, 2);
  c->u_touch_move  = (fn_touch_move )try_sym(unity_handle, names_touch_move,  2);
  c->u_touch_end   = (fn_touch_end  )try_sym(unity_handle, names_touch_end,   2);
  c->u_key_down    = (fn_key_down   )try_sym(unity_handle, names_key_down,    2);
  c->u_key_up      = (fn_key_up     )try_sym(unity_handle, names_key_up,      2);
  c->u_axis_set    = (fn_axis_set   )try_sym(unity_handle, names_axis,        2);

  /* Garante SDL subsystems necessários. Nao inicializamos VIDEO
   * de novo - bridge_gl.c já fez. */
  SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
  /* Abre o primeiro game controller (TSP expoe 1). */
  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    if (SDL_IsGameController(i)) {
      c->pad = SDL_GameControllerOpen(i);
      if (c->pad) { fprintf(stderr, "bridge_input: gamepad[%d]='%s'\n",
                             i, SDL_GameControllerName(c->pad)); break; }
    }
  }
  if (!c->pad) fprintf(stderr, "bridge_input: NENHUM gamepad SDL - usando teclado/mouse\n");

  if (pthread_create(&c->thread, NULL, poll_thread, c) != 0) {
    free(c);
    return SHIM_INPUT_NO_THREAD;
  }
  c->running = 1;
  *out = c;
  return SHIM_INPUT_OK;
}

void shim_input_stop(shim_input_ctx *c) {
  if (!c) return;
  atomic_store(&c->stop, 1);
  if (c->running) pthread_join(c->thread, NULL);
  if (c->pad) SDL_GameControllerClose(c->pad);
  SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
  free(c);
}
