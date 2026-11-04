/* bridge_input.h - API do loop de input. */
#ifndef BRIDGE_INPUT_H
#define BRIDGE_INPUT_H

#include <stdint.h>

typedef struct shim_input_ctx shim_input_ctx;

typedef enum {
  SHIM_INPUT_OK = 0,
  SHIM_INPUT_NO_THREAD,
  SHIM_INPUT_NO_QUEUE,
} shim_input_status;

/* Chamar DEPOIS de shim_gl_init e DEPOIS de dlopen libunity.so
 * (unityHandle pode ser NULL se voce ainda nao fez o dlopen). */
shim_input_status shim_input_start(shim_input_ctx **out,
                                   void *unity_handle,
                                   int screen_w, int screen_h);

void shim_input_stop(shim_input_ctx *ctx);

/* Liga a porta de input REAL deste build: o native registrado pelo
 * UnityPlayer. 'inject' e o ponteiro de nativeInjectEvent (achado via
 * shim_jni_find_native("com/unity3d/player/UnityPlayer","nativeInjectEvent"))
 * e 'env' e o JNIEnv do shim. NULL em inject = fica só na AInputQueue. */
void shim_input_set_unity_inject(shim_input_ctx *ctx, void *inject, void *env);

/* 1 = mouse virtual ligado agora (p/ o bridge desenhar feedback etc). */
int shim_input_mouse_active(shim_input_ctx *ctx);

/* 1 = saida pedida (SDL_QUIT ou SELECT+START 1 s). Checar a cada frame. */
int shim_input_quit_requested(shim_input_ctx *ctx);

#endif
