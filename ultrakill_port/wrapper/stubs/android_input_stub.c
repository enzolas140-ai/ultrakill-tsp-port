/* android_input_stub.c - AInputQueue + AInputEvent + accessors, exportados
 * em libandroid.so. É a espinha dorsal do input: Unity chama
 * AInputQueue_getEvent/preDispatchEvent/finishEvent em loop na thread
 * main pra coletar eventos que o bridge_input.c injeta via
 * shim_input_queue_push(). */
#define _GNU_SOURCE
#include <android_shim.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ tipos */

#define SHIM_MAX_POINTERS 8
#define SHIM_QUEUE_CAP    128

struct motion_pointer {
  int32_t id;
  float   x, y;
  float   pressure, size;
  int32_t tool_type;
};

struct axis_floats {
  float hat_x, hat_y;
  float ltrig, rtrig;
  float rx, ry, rz, zx;
};

struct AInputEvent {
  int32_t type;               /* AINPUT_EVENT_TYPE_{KEY,MOTION} */
  int32_t device_id;
  int32_t source;
  int64_t event_time_ns;
  int64_t down_time_ns;
  union {
    struct {                  /* KEY */
      int32_t action, keycode, scancode, meta_state, flags, repeat_count;
    } key;
    struct {                  /* MOTION */
      int32_t action;
      int32_t flags;
      size_t  pointer_count;
      struct motion_pointer ptrs[SHIM_MAX_POINTERS];
      struct axis_floats    axes;
    } motion;
  };
  int handled;                /* p/ finishEvent */
};

/* Fila: buffer circular. `wake_pipe[1]` é escrito em push pra acordar
 * o ALooper_addFd do bridge_main. */
struct AInputQueue {
  AInputEvent     *slots[SHIM_QUEUE_CAP];
  unsigned head, tail;
  pthread_mutex_t  mtx;
  int              wake_pipe[2];   /* [0]=read [1]=write */
  /* looper binding (Uma fila por looper): */
  ALooper *bound_looper;
  int      bound_ident;
  ALooper_callbackFunc bound_cb;
  void    *bound_data;
};

static AInputQueue g_queue = {
  .mtx = PTHREAD_MUTEX_INITIALIZER,   /* funciona em glibc sem init explicito */
  .wake_pipe = { -1, -1 },
};

static int64_t now_ns(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* -------- construtores internos (usados pelo bridge_input.c) -------- */

__attribute__((visibility("default")))
AInputEvent *shim_input_event_alloc(int32_t type, int32_t source) {
  AInputEvent *ev = calloc(1, sizeof(*ev));
  if (!ev) return NULL;
  ev->type = type;
  ev->source = source;
  ev->device_id = 3;                     /* virtual keyboard/dev id */
  ev->event_time_ns = now_ns();
  ev->down_time_ns  = ev->event_time_ns;
  return ev;
}

__attribute__((visibility("default")))
void shim_input_event_free(AInputEvent *ev) { free(ev); }

__attribute__((visibility("default")))
AInputQueue *shim_input_queue_global(void) {
  if (g_queue.wake_pipe[0] < 0) {
    if (pipe2(g_queue.wake_pipe, O_NONBLOCK) != 0) {
      g_queue.wake_pipe[0] = g_queue.wake_pipe[1] = -1;
    }
  }
  return &g_queue;
}

/* Função interna - nao exportada, mas simetrica com getEvent */
static int queue_drain_one_locked(AInputQueue *q, AInputEvent **out) {
  if (q->head == q->tail) return -EAGAIN;
  *out = q->slots[q->tail];
  q->tail = (q->tail + 1) % SHIM_QUEUE_CAP;
  return 0;
}

__attribute__((visibility("default")))
int32_t shim_input_queue_push(AInputQueue *q, AInputEvent *ev) {
  if (!q || !ev) return -1;
  pthread_mutex_lock(&q->mtx);
  unsigned next = (q->head + 1) % SHIM_QUEUE_CAP;
  if (next == q->tail) {
    /* buffer cheio: descarta o mais antigo (head atual) pra nao bloquear
     * a thread SDL. Unity tolera input drop; nao tolera stall. */
    AInputEvent *dropped = q->slots[q->tail];
    q->tail = (q->tail + 1) % SHIM_QUEUE_CAP;
    free(dropped);
    fprintf(stderr, "input_queue: overflow, 1 evento descartado\n");
  }
  q->slots[q->head] = ev;
  q->head = next;
  pthread_mutex_unlock(&q->mtx);

  /* acorda o looper se estiver dormindo no fd: */
  if (q->wake_pipe[1] >= 0) {
    char one = 1; ssize_t r = write(q->wake_pipe[1], &one, 1); (void)r;
  }
  return 0;
}

/* -------- AInputQueue API (NDK publico) -------- */

/* Callback do looper quando o fd da fila acorda: chama cb que o Unity
 * registrou em attachLooper. Nao le a fila aqui - Unity vai chamar
 * getEvent no callback. */
static int looper_wake_cb(int fd, int events, void *data) {
  AInputQueue *q = (AInputQueue*)data;
  char drain[64];
  while (read(q->wake_pipe[0], drain, sizeof drain) > 0) { /* empty */ }
  if (q->bound_cb) {
    return q->bound_cb(fd, ALOOPER_EVENT_INPUT, q->bound_data);
  }
  return 1;   /* continua registrado */
}

__attribute__((visibility("default")))
int AInputQueue_attachLooper(AInputQueue *q, ALooper *l, int ident,
                             ALooper_callbackFunc cb, void *data) {
  if (!q) return -EINVAL;
  shim_input_queue_global();   /* garante wake_pipe inicializado */
  q->bound_looper = l;
  q->bound_ident  = ident;
  q->bound_cb     = cb;
  q->bound_data   = data;
  if (l && q->wake_pipe[0] >= 0) {
    ALooper_addFd(l, q->wake_pipe[0], ident, ALOOPER_EVENT_INPUT,
                  looper_wake_cb, q);
  }
  return 0;
}

__attribute__((visibility("default")))
void AInputQueue_detachLooper(AInputQueue *q) {
  if (!q) return;
  if (q->bound_looper && q->wake_pipe[0] >= 0)
    ALooper_removeFd(q->bound_looper, q->wake_pipe[0]);
  q->bound_looper = NULL;
  q->bound_cb = NULL;
  q->bound_data = NULL;
}

__attribute__((visibility("default")))
int AInputQueue_hasEvents(AInputQueue *q) {
  if (!q) return 0;
  pthread_mutex_lock(&q->mtx);
  int has = (q->head != q->tail);
  pthread_mutex_unlock(&q->mtx);
  return has;
}

__attribute__((visibility("default")))
int AInputQueue_getEvent(AInputQueue *q, AInputEvent **out) {
  if (!q || !out) return -EINVAL;
  pthread_mutex_lock(&q->mtx);
  int r = queue_drain_one_locked(q, out);
  pthread_mutex_unlock(&q->mtx);
  return r;   /* 0 = ok; -EAGAIN = vazio */
}

__attribute__((visibility("default")))
int AInputQueue_preDispatchEvent(AInputQueue *q, AInputEvent *ev) {
  /* Retorna 1 se o evento deve ser ignorado (dispatch suspenso).
   * Nossa fila nao tem filtragem; sempre 0 (deixa Unity processar). */
  (void)q; (void)ev; return 0;
}

__attribute__((visibility("default")))
void AInputQueue_finishEvent(AInputQueue *q, AInputEvent *ev, int handled) {
  (void)q;
  if (!ev) return;
  ev->handled = handled;
  /* Liberamos aqui: no NDK real, finishEvent libera o evento se nao
   * mais referenciado. Unity chama finishEvent após processar. */
  free(ev);
}

/* -------- AInputEvent (generic) -------- */
__attribute__((visibility("default")))
int32_t AInputEvent_getType(const AInputEvent *ev) { return ev ? ev->type : 0; }
__attribute__((visibility("default")))
int32_t AInputEvent_getDeviceId(const AInputEvent *ev) { return ev ? ev->device_id : 0; }
__attribute__((visibility("default")))
int32_t AInputEvent_getSource(const AInputEvent *ev) { return ev ? ev->source : 0; }

/* -------- AKeyEvent accessors -------- */
#define KEY_EV(ev) ((const AInputEvent*)(ev))
__attribute__((visibility("default")))
int32_t AKeyEvent_getAction(const AKeyEvent *ev)     { return KEY_EV(ev) ? ev->key.action : 0; }
__attribute__((visibility("default")))
int64_t AKeyEvent_getEventTime(const AKeyEvent *ev)  { return KEY_EV(ev) ? ev->event_time_ns : 0; }
__attribute__((visibility("default")))
int64_t AKeyEvent_getDownTime(const AKeyEvent *ev)   { return KEY_EV(ev) ? ev->down_time_ns : 0; }
__attribute__((visibility("default")))
int32_t AKeyEvent_getKeyCode(const AKeyEvent *ev)    { return KEY_EV(ev) ? ev->key.keycode : 0; }
__attribute__((visibility("default")))
int32_t AKeyEvent_getScanCode(const AKeyEvent *ev)   { return KEY_EV(ev) ? ev->key.scancode : 0; }
__attribute__((visibility("default")))
int32_t AKeyEvent_getMetaState(const AKeyEvent *ev)  { return KEY_EV(ev) ? ev->key.meta_state : 0; }
__attribute__((visibility("default")))
int32_t AKeyEvent_getFlags(const AKeyEvent *ev)      { return KEY_EV(ev) ? ev->key.flags : 0; }
__attribute__((visibility("default")))
int32_t AKeyEvent_getRepeatCount(const AKeyEvent *ev){ return KEY_EV(ev) ? ev->key.repeat_count : 0; }

/* -------- AMotionEvent accessors -------- */
__attribute__((visibility("default")))
size_t AMotionEvent_getPointerCount(const AMotionEvent *ev) {
  return ev ? ev->motion.pointer_count : 0;
}
__attribute__((visibility("default")))
int32_t AMotionEvent_getAction(const AMotionEvent *ev) { return ev ? ev->motion.action : 0; }
__attribute__((visibility("default")))
int64_t AMotionEvent_getEventTime(const AMotionEvent *ev) { return ev ? ev->event_time_ns : 0; }
__attribute__((visibility("default")))
int64_t AMotionEvent_getDownTime(const AMotionEvent *ev)  { return ev ? ev->down_time_ns : 0; }
__attribute__((visibility("default")))
float AMotionEvent_getX(const AMotionEvent *ev, size_t idx) {
  return (ev && idx < ev->motion.pointer_count) ? ev->motion.ptrs[idx].x : 0.f;
}
__attribute__((visibility("default")))
float AMotionEvent_getY(const AMotionEvent *ev, size_t idx) {
  return (ev && idx < ev->motion.pointer_count) ? ev->motion.ptrs[idx].y : 0.f;
}
__attribute__((visibility("default")))
float AMotionEvent_getRawX(const AMotionEvent *ev, size_t idx) { return AMotionEvent_getX(ev, idx); }
__attribute__((visibility("default")))
float AMotionEvent_getRawY(const AMotionEvent *ev, size_t idx) { return AMotionEvent_getY(ev, idx); }
__attribute__((visibility("default")))
float AMotionEvent_getSize(const AMotionEvent *ev, size_t idx) {
  return (ev && idx < ev->motion.pointer_count) ? ev->motion.ptrs[idx].size : 0.f;
}
__attribute__((visibility("default")))
float AMotionEvent_getPressure(const AMotionEvent *ev, size_t idx) {
  return (ev && idx < ev->motion.pointer_count) ? ev->motion.ptrs[idx].pressure : 0.f;
}
__attribute__((visibility("default")))
int32_t AMotionEvent_getPointerId(const AMotionEvent *ev, size_t idx) {
  return (ev && idx < ev->motion.pointer_count) ? ev->motion.ptrs[idx].id : 0;
}
__attribute__((visibility("default")))
int32_t AMotionEvent_getToolType(const AMotionEvent *ev, size_t idx) {
  return (ev && idx < ev->motion.pointer_count) ? ev->motion.ptrs[idx].tool_type : 0;
}
__attribute__((visibility("default")))
float AMotionEvent_getAxisValue(const AMotionEvent *ev, int32_t axis, size_t idx) {
  if (!ev) return 0.f;
  /* idx 0 é o que tem axes de joystick - Unity usa idx 0 p/ sticks */
  if (idx != 0) return 0.f;
  switch (axis) {
    case AMOTION_EVENT_AXIS_X:       return ev->motion.ptrs[0].x;
    case AMOTION_EVENT_AXIS_Y:       return ev->motion.ptrs[0].y;
    case AMOTION_EVENT_AXIS_HAT_X:   return ev->motion.axes.hat_x;
    case AMOTION_EVENT_AXIS_HAT_Y:   return ev->motion.axes.hat_y;
    case AMOTION_EVENT_AXIS_LTRIGGER:return ev->motion.axes.ltrig;
    case AMOTION_EVENT_AXIS_RTRIGGER:return ev->motion.axes.rtrig;
    case AMOTION_EVENT_AXIS_RX:      return ev->motion.axes.rx;
    case AMOTION_EVENT_AXIS_RY:      return ev->motion.axes.ry;
    case AMOTION_EVENT_AXIS_RZ:      return ev->motion.axes.rz;
    case AMOTION_EVENT_AXIS_Z:       return ev->motion.axes.zx;
    default: return 0.f;
  }
}

/* -------- helpers p/ bridge_input.c construir eventos --------
 * NAO fazem parte do NDK. visibilidade default:
 */
__attribute__((visibility("default")))
void shim_input_key_fill(AInputEvent *ev, int32_t action, int32_t keycode, int32_t scancode) {
  if (!ev) return;
  ev->type = AINPUT_EVENT_TYPE_KEY;
  ev->key.action = action;
  ev->key.keycode = keycode;
  ev->key.scancode = scancode;
}

__attribute__((visibility("default")))
void shim_input_motion_resize(AInputEvent *ev, size_t n) {
  if (!ev) return;
  ev->type = AINPUT_EVENT_TYPE_MOTION;
  if (n > SHIM_MAX_POINTERS) n = SHIM_MAX_POINTERS;
  ev->motion.pointer_count = n;
}

__attribute__((visibility("default")))
void shim_input_motion_set_action(AInputEvent *ev, int32_t action) {
  if (ev) ev->motion.action = action;
}

__attribute__((visibility("default")))
void shim_input_motion_set_pointer(AInputEvent *ev, size_t idx, int32_t id,
                                   float x, float y, float pressure, float size,
                                   int32_t tool_type) {
  if (!ev || idx >= ev->motion.pointer_count) return;
  struct motion_pointer *p = &ev->motion.ptrs[idx];
  p->id = id; p->x = x; p->y = y;
  p->pressure = pressure; p->size = size; p->tool_type = tool_type;
}

__attribute__((visibility("default")))
void shim_input_motion_set_axis(AInputEvent *ev, int32_t axis, float value) {
  if (!ev) return;
  switch (axis) {
    case AMOTION_EVENT_AXIS_HAT_X:    ev->motion.axes.hat_x = value; break;
    case AMOTION_EVENT_AXIS_HAT_Y:    ev->motion.axes.hat_y = value; break;
    case AMOTION_EVENT_AXIS_LTRIGGER: ev->motion.axes.ltrig = value; break;
    case AMOTION_EVENT_AXIS_RTRIGGER: ev->motion.axes.rtrig = value; break;
    case AMOTION_EVENT_AXIS_RX:       ev->motion.axes.rx = value; break;
    case AMOTION_EVENT_AXIS_RY:       ev->motion.axes.ry = value; break;
    case AMOTION_EVENT_AXIS_RZ:       ev->motion.axes.rz = value; break;
    case AMOTION_EVENT_AXIS_Z:        ev->motion.axes.zx = value; break;
    default: break;
  }
}
