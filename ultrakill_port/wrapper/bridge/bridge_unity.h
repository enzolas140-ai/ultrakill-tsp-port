/* bridge_unity.h - o que o bridge precisa saber sobre libunity.so. */
#ifndef BRIDGE_UNITY_H
#define BRIDGE_UNITY_H

#include <stdint.h>

/* ANativeActivity: struct do NDK (native_activity.h). Unity 2019.4 le
 * campos desta struct em UnityMain/ANativeActivity_onCreate. */
struct android_app;    /* opaque */
struct AAssetManager;
struct AConfiguration;
struct ANativeWindow;
struct ALooper;
struct JavaVM_;
struct ARect { int32_t l, t, r, b; };

typedef struct ANativeActivity {
  void                 *callbacks;
  void                 *userData;
  struct JavaVM_       *vm;
  void                 *env;                /* JNIEnv* */
  void                 *clazz;              /* jobject */
  const char           *packageName;
  const char           *activityName;
  const char           *className;
  const char           *internalDataPath;
  const char           *externalDataPath;
  const char           *instanceState;      /* saved state */
  uint8_t               savedStateSize;     /* (simplificado) */
  struct ALooper       *looper;
  int                   contentScale;       /* dp */
  int                   debuggable;
} ANativeActivity;

typedef struct android_app {
  void                *userData;
  void                *activity;
  struct ANativeWindow *window;
  struct AConfiguration *config;
  int8_t               *inputBuffer;        /* AInputQueue* - opaque */
  /* ...mais campos que Unity raramente toca - truncamos */
} android_app;

/* Símbolos que libunity.so exporta em Android builds Unity 2019.4.x: */
typedef void (*UnityMain_fn)(ANativeActivity *activity, void *savedState,
                              size_t savedStateLen);
typedef void (*ANativeActivity_onCreate_fn)(ANativeActivity *activity,
                                            void *savedState,
                                            size_t savedStateLen);

/* Unity as vezes expoe tambem: */
typedef int (*UnityPlayerNativeActivity_init_fn)(int argc, char **argv, char **envp);

#endif /* BRIDGE_UNITY_H */
