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
