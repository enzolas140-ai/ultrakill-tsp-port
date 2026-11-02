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

#endif
