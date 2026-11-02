/* opensles_stub.c - OpenSL ES no-op.
 *
 * Unity 2019.4 chama slCreateEngine uma vez, depois cria um AudioPlayer
 * por canal, e alimenta PCM via SLAndroidBufferQueueItf. Se retornarmos
 * SL_RESULT_SUCCESS mas nao implementarmos o buffer queue, o engine
 * aloca tudo, chama Enqueue() que vira drop, e nunca faz underflow. O
 * resultado: jogo roda silencioso. Perfeito pra smoke-test.
 *
 * TODO (audio real): rotear SLPlayItf::SetPlayState -> um ring-buffer
 * de 20ms x 2 canais mandando em snd_pcm_writei (ALSA). Isso ~300 LOC. */
#include <android_shim.h>
#include <stdlib.h>
#include <string.h>

/* Todos os "objetos" sao um token opaco com um refcount. */
struct sl_obj { SLuint32 state; int refs; };

static SLresult ok(void) { return SL_RESULT_SUCCESS; }

__attribute__((visibility("default")))
SLresult slCreateEngine(SLobject *e, SLuint32 nOpt, const void *optItf,
                        SLuint32 nItf, const SLinterface *itf, const void *named) {
  (void)nOpt; (void)optItf; (void)nItf; (void)itf; (void)named;
  struct sl_obj *o = calloc(1, sizeof(*o));
  o->refs = 1; o->state = 0;  /* UNREALIZED */
  if (e) *e = o;
  return ok();
}

__attribute__((visibility("default")))
SLresult slDestroyObject(SLobject o) {
  struct sl_obj *x = o;
  if (x && --x->refs <= 0) free(x);
  return ok();
}

__attribute__((visibility("default")))
SLresult slRealizeObject(SLobject o) {
  struct sl_obj *x = o; if (x) x->state = SL_OBJECT_STATE_REALIZED;
  return ok();
}

__attribute__((visibility("default")))
SLresult slGetObjectState(SLobject o, SLuint32 *state) {
  struct sl_obj *x = o; if (x && state) *state = x->state;
  return ok();
}

__attribute__((visibility("default")))
SLresult slGetInterface(SLobject o, const SLinterface iid, SLinterface *iface) {
  /* Retorna NULL em *iface pra forcar Unity a cair em caminho "unsupported".
   * Alguns builds Unity 2019.4 vao aceitar; outros vao chamar SL_RESULT_
   * FEATURE_UNSUPPORTED e logar. Testar. */
  (void)o; (void)iid;
  if (iface) *iface = NULL;
  return SL_RESULT_FEATURE_UNSUPPORTED;
}

__attribute__((visibility("default")))
SLresult slSetInterfaceState(SLobject o, const SLinterface iid, SLuint32 s) {
  (void)o; (void)iid; (void)s; return ok();
}

__attribute__((visibility("default")))
SLresult slRegisterClass(void) { return ok(); }
