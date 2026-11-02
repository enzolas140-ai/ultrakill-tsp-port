/* android_nativeactivity_stub.c - ALooper / AMemory / AConfiguration.
 *
 * Unity 2019.4 usa ALooper principalmente na thread principal do
 * NativeActivity pra receber eventos de input do sistema Android.
 * No bridge, rodamos uma SDL2 event loop - entao ALooper_pollAll so
 * precisa "dormir" ate ser wakeado via write num pipe. */
#include <android_shim.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>

struct ALooper {
  int    wake_fd;         /* eventfd */
  int    refs;
  /* fd set: mantemos ate 32 fds registrados; Unity raramente usa >8. */
  struct { int fd; int events; ALooper_callbackFunc cb; void *data; int ident; }
    fds[32];
  int nfds;
};

static __thread ALooper *tls_looper = NULL;

__attribute__((visibility("default")))
ALooper *ALooper_prepare(int opts) {
  (void)opts;
  if (tls_looper) { tls_looper->refs++; return tls_looper; }
  ALooper *l = calloc(1, sizeof(*l));
  l->wake_fd = eventfd(0, EFD_NONBLOCK);
  l->refs = 1;
  tls_looper = l;
  return l;
}

__attribute__((visibility("default")))
ALooper *ALooper_forThread(void) { return tls_looper; }

__attribute__((visibility("default")))
int ALooper_acquire(ALooper *l) { if (l) l->refs++; return 0; }

__attribute__((visibility("default")))
int ALooper_release(ALooper *l) {
  if (!l) return -1;
  if (--l->refs <= 0) {
    close(l->wake_fd);
    if (tls_looper == l) tls_looper = NULL;
    free(l);
  }
  return 0;
}

__attribute__((visibility("default")))
int ALooper_wake(ALooper *l) {
  uint64_t one = 1;
  if (!l) return -1;
  ssize_t r = write(l->wake_fd, &one, sizeof one);
  return r > 0 ? 0 : -1;
}

__attribute__((visibility("default")))
int ALooper_addFd(ALooper *l, int fd, int ident, int events,
                  ALooper_callbackFunc cb, void *data) {
  if (!l) return -1;
  for (int i = 0; i < l->nfds; i++) {
    if (l->fds[i].fd == fd) {  /* replace */
      l->fds[i].events = events; l->fds[i].cb = cb; l->fds[i].data = data;
      l->fds[i].ident = ident;
      return 0;
    }
  }
  if (l->nfds >= 32) return -1;
  l->fds[l->nfds++] = (typeof(l->fds[0])){ fd, events, cb, data, ident };
  return 0;
}

__attribute__((visibility("default")))
int ALooper_removeFd(ALooper *l, int fd) {
  if (!l) return -1;
  for (int i = 0; i < l->nfds; i++) {
    if (l->fds[i].fd == fd) {
      memmove(&l->fds[i], &l->fds[i+1], (l->nfds-i-1)*sizeof(l->fds[0]));
      l->nfds--;
      return 0;
    }
  }
  return -1;
}

__attribute__((visibility("default")))
int ALooper_pollAll(int timeoutMillis, int *outFd, int *outEvents, void **outData) {
  ALooper *l = tls_looper;
  if (!l) return ALOOPER_POLL_CALLBACK;  /* nao estamos na thread main */
  struct pollfd pf[33];
  pf[0] = (struct pollfd){ l->wake_fd, POLLIN, 0 };
  for (int i = 0; i < l->nfds; i++)
    pf[i+1] = (struct pollfd){ l->fds[i].fd, (short)l->fds[i].events, 0 };

  int r = poll(pf, l->nfds+1, timeoutMillis);
  if (r <= 0) return ALOOPER_POLL_TIMEOUT;
  if (pf[0].revents & POLLIN) {
    uint64_t drained; ssize_t got = read(l->wake_fd, &drained, sizeof drained);
    (void)got;
    /* wake-only; volta a loop */
    return ALOOPER_POLL_WAKE;
  }
  for (int i = 0; i < l->nfds; i++) {
    if (pf[i+1].revents) {
      if (outFd) *outFd = l->fds[i].fd;
      if (outEvents) *outEvents = pf[i+1].revents;
      if (outData) *outData = l->fds[i].data;
      if (l->fds[i].cb) {
        l->fds[i].cb(l->fds[i].fd, pf[i+1].revents, l->fds[i].data);
        return ALOOPER_POLL_CALLBACK;
      }
      return l->fds[i].ident;
    }
  }
  return ALOOPER_POLL_TIMEOUT;
}

/* -------- AConfiguration (Unity so le para setar locale) -------- */
struct AConfiguration { int32_t lang; int32_t country; };

__attribute__((visibility("default")))
AConfiguration *AConfiguration_new(void) {
  AConfiguration *c = calloc(1, sizeof(*c));
  c->lang = ('e'<<16) | ('n'<<24);   /* ACONFIGURATION_LANGUAGE_EN */
  c->country = 0;
  return c;
}
__attribute__((visibility("default")))
void AConfiguration_delete(AConfiguration *c) { free(c); }
__attribute__((visibility("default")))
int32_t AConfiguration_getLanguage(const AConfiguration *c) { return c ? c->lang : 0; }
__attribute__((visibility("default")))
int32_t AConfiguration_getCountry(const AConfiguration *c)  { return c ? c->country : 0; }
__attribute__((visibility("default")))
int32_t AConfiguration_getKeyboard(const AConfiguration *c) { (void)c; return 1; /* NOKEYS */ }
__attribute__((visibility("default")))
int32_t AConfiguration_getScreenSize(const AConfiguration *c) { (void)c; return 3; /* LARGE */ }
__attribute__((visibility("default")))
int32_t AConfiguration_getScreenDensity(const AConfiguration *c) { (void)c; return 160; }

/* -------- ANativeWindow: minimal, backed by nothing (SDL cuida disso) ---- */
__attribute__((visibility("default")))
ANativeWindow *ANativeWindow_acquire(ANativeWindow *w) { return w; }
__attribute__((visibility("default")))
ANativeWindow *ANativeWindow_release(ANativeWindow *w) { return w; }
__attribute__((visibility("default")))
int32_t ANativeWindow_getWidth(ANativeWindow *w)  { (void)w; return 1280; }
__attribute__((visibility("default")))
int32_t ANativeWindow_getHeight(ANativeWindow *w) { (void)w; return 720; }
__attribute__((visibility("default")))
int32_t ANativeWindow_getFormat(ANativeWindow *w) { (void)w; return 1; /* RGBA_8888 */ }

/* ANativeWindow_fromSurface: no bridge nao ha Surface real; devolvemos o
 * shim ANativeWindow ja em uso (ou NULL). Unity so usa p/ obter a window a
 * partir do Java Surface - noi ignoramos porque o EGL surface ja esta
 * pre-criado no shim_gl. */
__attribute__((visibility("default")))
ANativeWindow *ANativeWindow_fromSurface(void *env, void *surface) {
  (void)env; (void)surface;
  extern ANativeWindow *ANativeWindow_get_shim(void);
  return ANativeWindow_get_shim();
}
__attribute__((visibility("default")))
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *w, int32_t fw,
                                         int32_t fh, int32_t fmt) {
  (void)w; (void)fw; (void)fh; (void)fmt; return 0;
}

/* -------- __system_property_* (Bionic libc) --------
 * Unity lê ro.build.version.sdk, dalvik.vm.*, etc. Devolvemos vazio/NULL;
 * os callers toleram. struct prop_info e opaca pra nos. */
__attribute__((visibility("default")))
const void *__system_property_find(const char *name) { (void)name; return NULL; }
__attribute__((visibility("default")))
int __system_property_get(const char *name, char *value) {
  (void)name;
  if (value) value[0] = 0;
  return 0;   /* strlen of empty value */
}
__attribute__((visibility("default")))
int __system_property_read(const void *pi, char *name, char *value) {
  (void)pi;
  if (name) name[0] = 0;
  if (value) value[0] = 0;
  return 0;
}
__attribute__((visibility("default")))
const char *__system_property_find_nth(unsigned i) { (void)i; return NULL; }
__attribute__((visibility("default")))
unsigned __system_property_serial(const void *pi) { (void)pi; return 0; }
__attribute__((visibility("default")))
void *__system_property_wait_any(void) { return NULL; }

/* android_set_abort_message: noop */
__attribute__((visibility("default")))
void android_set_abort_message(const char *msg) { (void)msg; }

/* -------- ASensorManager / ASensorEventQueue (acelerometro) --------
 * ULTRAKILL nao usa sensor, mas libunity.so importa os simbolos -> precisa
 * existir pra RTLD_LAZY nao explodir quando o init tocar neles. Devolve
 * NULL/0; Unity trata ausencia de sensor. */
__attribute__((visibility("default")))
void *ASensorManager_getInstance(void) { return NULL; }
__attribute__((visibility("default")))
void *ASensorManager_getInstanceForPackage(const char *p) { (void)p; return NULL; }
__attribute__((visibility("default")))
int ASensorManager_getSensorList(const void *m, const void **list) {
  (void)m; if (list) *list = NULL; return 0;
}
__attribute__((visibility("default")))
const void *ASensorManager_getDefaultSensor(const void *m, int type) {
  (void)m; (void)type; return NULL;
}
__attribute__((visibility("default")))
void *ASensorManager_createEventQueue(const void *m, void *looper,
                                      int ident, void *cb, void *data) {
  (void)m; (void)looper; (void)ident; (void)cb; (void)data; return NULL;
}
__attribute__((visibility("default")))
int ASensorManager_destroyEventQueue(const void *m, void *q) {
  (void)m; (void)q; return 0;
}
__attribute__((visibility("default")))
int ASensorEventQueue_enableSensor(void *q, const void *s) { (void)q; (void)s; return 0; }
__attribute__((visibility("default")))
int ASensorEventQueue_disableSensor(void *q, const void *s) { (void)q; (void)s; return 0; }
__attribute__((visibility("default")))
int ASensorEventQueue_setEventRate(void *q, const void *s, int32_t us) { (void)q; (void)s; (void)us; return 0; }
__attribute__((visibility("default")))
int ASensorEventQueue_hasEvents(void *q) { (void)q; return 0; }
__attribute__((visibility("default")))
ssize_t ASensorEventQueue_getEvents(void *q, void *ev, size_t n) { (void)q; (void)ev; (void)n; return 0; }
__attribute__((visibility("default")))
const char *ASensor_getName(const void *s) { (void)s; return ""; }
__attribute__((visibility("default")))
const char *ASensor_getVendor(const void *s) { (void)s; return ""; }
__attribute__((visibility("default")))
int ASensor_getType(const void *s) { (void)s; return 0; }
__attribute__((visibility("default")))
float ASensor_getResolution(const void *s) { (void)s; return 0.f; }
__attribute__((visibility("default")))
int ASensor_getMinDelay(const void *s) { (void)s; return 0; }
__attribute__((visibility("default")))
int ASensor_getMaxRange(const void *s) { (void)s; return 0; }
__attribute__((visibility("default")))
void ASensor_setAcquisitionRate(const void *s, int r) { (void)s; (void)r; }
__attribute__((visibility("default")))
void ASensor_setMaximumReportLatency(const void *s, int l) { (void)s; (void)l; }
