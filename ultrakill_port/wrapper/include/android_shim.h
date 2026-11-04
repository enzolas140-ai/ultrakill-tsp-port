/* android_shim.h - tipos/const do NDK que o libunity.so 2019.4 usa.
 * Recriados a mao a partir de android-ndk r21 (public headers). Nao incluir
 * os headers originais do NDK aqui: eles puxam <jni.h> e dependencias que
 * nao existem em SpruceOS. */
#ifndef ANDROID_SHIM_H
#define ANDROID_SHIM_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* -------- android/log.h -------- */
enum android_LogPriority {
  ANDROID_LOG_UNKNOWN = 0, ANDROID_LOG_DEFAULT, ANDROID_LOG_VERBOSE,
  ANDROID_LOG_DEBUG, ANDROID_LOG_INFO, ANDROID_LOG_WARN,
  ANDROID_LOG_ERROR, ANDROID_LOG_FATAL, ANDROID_LOG_SILENT,
};
int __android_log_print(int prio, const char *tag, const char *fmt, ...);
int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap);
int __android_log_write(int prio, const char *tag, const char *text);
void __android_log_assert(const char *cond, const char *tag, const char *fmt, ...);

/* -------- android/asset_manager.h -------- */
typedef struct AAssetManager AAssetManager;
typedef struct AAsset AAsset;
enum { AASSET_MODE_UNKNOWN=0, AASSET_MODE_RANDOM=1,
       AASSET_MODE_SEQUENTIAL=2, AASSET_MODE_BUFFER=3 };
AAsset *AAssetManager_open(AAssetManager *mgr, const char *filename, int mode);
off_t   AAsset_getLength(AAsset *a);
off_t   AAsset_read(AAsset *a, void *buf, size_t count);
off_t   AAsset_seek(AAsset *a, off_t offset, int whence);
const void *AAsset_getBuffer(AAsset *a);
void    AAsset_close(AAsset *a);
/* nosso extra: construtor (no NDK real isto vem via JNI) */
AAssetManager *shim_AAssetManager_fromPath(const char *basepath);

/* -------- android/looper.h -------- */
typedef struct ALooper ALooper;
enum { ALOOPER_PREPARE_ALLOW_NON_CALLBACKS = 1<<0 };
enum { ALOOPER_EVENT_INPUT = 1<<0, ALOOPER_EVENT_OUTPUT = 1<<1,
       ALOOPER_EVENT_ERROR  = 1<<2, ALOOPER_EVENT_HANGUP = 1<<4,
       ALOOPER_EVENT_INVALID = 1<<8 };
/* retorno de ALooper_pollAll: */
enum { ALOOPER_POLL_WAKE = -1, ALOOPER_POLL_CALLBACK = -2, ALOOPER_POLL_TIMEOUT = -3, ALOOPER_POLL_ERROR = -4 };
typedef int (*ALooper_callbackFunc)(int fd, int events, void *data);
ALooper *ALooper_prepare(int opts);
int      ALooper_acquire(ALooper *l);
int      ALooper_release(ALooper *l);
int      ALooper_addFd(ALooper *l, int fd, int ident, int events,
                       ALooper_callbackFunc cb, void *data);
int      ALooper_removeFd(ALooper *l, int fd);
int      ALooper_pollAll(int timeoutMillis, int *outFd, int *outEvents,
                         void **outData);

/* -------- android/native_activity.h (subset) -------- */
typedef struct AConfiguration AConfiguration;
typedef struct ANativeActivity ANativeActivity;
typedef struct AHardwareBuffer AHardwareBuffer;
enum { ACONFIGURATION_LANGUAGE_REGION_COUNT=3 };
typedef enum { AWINDOW_INVALID=0, AWINDOW_NATIVE=1, AWINDOW_SURFACE=2 } awindow_t;
void AConfiguration_delete(AConfiguration *c);
int32_t AConfiguration_getLanguage(const AConfiguration *c);
int32_t AConfiguration_getCountry(const AConfiguration *c);
int32_t AConfiguration_getKeyboard(const AConfiguration *c);
int32_t AConfiguration_getScreenSize(const AConfiguration *c);
int32_t AConfiguration_getScreenDensity(const AConfiguration *c);

/* AMemory (util p/ Unity quando faz marshaling p/ Java heap) */
typedef struct ANativeWindow ANativeWindow;
ANativeWindow *ANativeWindow_acquire(ANativeWindow *w);
ANativeWindow *ANativeWindow_release(ANativeWindow *w);
int32_t ANativeWindow_getWidth(ANativeWindow *w);
int32_t ANativeWindow_getHeight(ANativeWindow *w);
int32_t ANativeWindow_getFormat(ANativeWindow *w);

/* -------- android/input.h (subset que Unity 2019.4 le) --------
 * Tipos opacos + enums das constantes. AInputEvent e a base;
 * AKeyEvent/AMotionEvent sao aliases - em Android real, tudo
 * downcast via AInputEvent_getType(). */
typedef struct AInputQueue  AInputQueue;
typedef struct AInputEvent  AInputEvent;
typedef AInputEvent          AKeyEvent;
typedef AInputEvent          AMotionEvent;

enum { AINPUT_EVENT_TYPE_KEY = 1, AINPUT_EVENT_TYPE_MOTION = 2 };
enum { AKEY_EVENT_ACTION_DOWN = 0, AKEY_EVENT_ACTION_UP = 1,
       AKEY_EVENT_ACTION_MULTIPLE = 2 };
enum { AMOTION_EVENT_ACTION_DOWN = 0, AMOTION_EVENT_ACTION_UP = 1,
       AMOTION_EVENT_ACTION_MOVE = 2, AMOTION_EVENT_ACTION_CANCEL = 3,
       AMOTION_EVENT_ACTION_OUTSIDE = 4, AMOTION_EVENT_ACTION_POINTER_DOWN = 5,
       AMOTION_EVENT_ACTION_POINTER_UP = 6,
       AMOTION_EVENT_ACTION_MASK = 0xff,
       AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT = 8 };
enum { AINPUT_SOURCE_CLASS_NONE = 0, AINPUT_SOURCE_CLASS_BUTTON = 1,
       AINPUT_SOURCE_CLASS_POINTER = 2, AINPUT_SOURCE_CLASS_JOYSTICK = 0x10,
       AINPUT_SOURCE_KEYBOARD = 0x101, AINPUT_SOURCE_DPAD = 0x201,
       AINPUT_SOURCE_GAMEPAD = 0x401, AINPUT_SOURCE_TOUCHSCREEN = 0x1002,
       AINPUT_SOURCE_TOUCH_NAVIGATION = 0x20000000,
       AINPUT_SOURCE_JOYSTICK = 0x10000010 };
enum { AMOTION_EVENT_TOOL_TYPE_UNKNOWN = 0, AMOTION_EVENT_TOOL_TYPE_FINGER = 1,
       AMOTION_EVENT_TOOL_TYPE_STYLUS = 2, AMOTION_EVENT_TOOL_TYPE_MOUSE = 3 };
enum { AMOTION_EVENT_AXIS_X = 0, AMOTION_EVENT_AXIS_Y = 1,
       AMOTION_EVENT_AXIS_PRESSURE = 2, AMOTION_EVENT_AXIS_SIZE = 3,
       AMOTION_EVENT_AXIS_TOUCH_MAJOR = 4, AMOTION_EVENT_AXIS_TOUCH_MINOR = 5,
       AMOTION_EVENT_AXIS_ORIENTATION = 9,
       AMOTION_EVENT_AXIS_HAT_X = 15, AMOTION_EVENT_AXIS_HAT_Y = 16,
       AMOTION_EVENT_AXIS_LTRIGGER = 17, AMOTION_EVENT_AXIS_RTRIGGER = 18,
       AMOTION_EVENT_AXIS_RX = 19, AMOTION_EVENT_AXIS_RY = 20,
       AMOTION_EVENT_AXIS_Z = 21, AMOTION_EVENT_AXIS_RZ = 22 };
enum { AKEYCODE_UNKNOWN = 0, AKEYCODE_BACK = 4, AKEYCODE_HOME = 3,
       AKEYCODE_DPAD_UP = 19, AKEYCODE_DPAD_DOWN = 20,
       AKEYCODE_DPAD_LEFT = 21, AKEYCODE_DPAD_RIGHT = 22,
       AKEYCODE_DPAD_CENTER = 23, AKEYCODE_BUTTON_A = 96,
       AKEYCODE_BUTTON_B = 97, AKEYCODE_BUTTON_X = 99, AKEYCODE_BUTTON_Y = 100,
       AKEYCODE_BUTTON_L1 = 102, AKEYCODE_BUTTON_R1 = 103,
       AKEYCODE_BUTTON_L2 = 104, AKEYCODE_BUTTON_R2 = 105,
       AKEYCODE_BUTTON_THUMBL = 106, AKEYCODE_BUTTON_THUMBR = 107,
       AKEYCODE_BUTTON_START = 108, AKEYCODE_BUTTON_SELECT = 109,
       AKEYCODE_ESCAPE = 111 };

/* AInputQueue API (exposta por libandroid.so) */
int      AInputQueue_attachLooper(AInputQueue *q, ALooper *l, int ident,
                                  ALooper_callbackFunc cb, void *data);
void     AInputQueue_detachLooper(AInputQueue *q);
int      AInputQueue_hasEvents(AInputQueue *q);
int      AInputQueue_getEvent(AInputQueue *q, AInputEvent **out);
int      AInputQueue_preDispatchEvent(AInputQueue *q, AInputEvent *ev);
void     AInputQueue_finishEvent(AInputQueue *q, AInputEvent *ev, int handled);

/* AInputEvent (generic) */
int32_t  AInputEvent_getType(const AInputEvent *ev);
int32_t  AInputEvent_getDeviceId(const AInputEvent *ev);
int32_t  AInputEvent_getSource(const AInputEvent *ev);

/* AKeyEvent */
int32_t  AKeyEvent_getAction(const AKeyEvent *ev);
int64_t  AKeyEvent_getEventTime(const AKeyEvent *ev);
int64_t  AKeyEvent_getDownTime(const AKeyEvent *ev);
int32_t  AKeyEvent_getKeyCode(const AKeyEvent *ev);
int32_t  AKeyEvent_getScanCode(const AKeyEvent *ev);
int32_t  AKeyEvent_getMetaState(const AKeyEvent *ev);
int32_t  AKeyEvent_getFlags(const AKeyEvent *ev);
int32_t  AKeyEvent_getRepeatCount(const AKeyEvent *ev);

/* AMotionEvent */
size_t   AMotionEvent_getPointerCount(const AMotionEvent *ev);
int32_t  AMotionEvent_getAction(const AMotionEvent *ev);
int64_t  AMotionEvent_getEventTime(const AMotionEvent *ev);
int64_t  AMotionEvent_getDownTime(const AMotionEvent *ev);
float    AMotionEvent_getX(const AMotionEvent *ev, size_t idx);
float    AMotionEvent_getY(const AMotionEvent *ev, size_t idx);
float    AMotionEvent_getRawX(const AMotionEvent *ev, size_t idx);
float    AMotionEvent_getRawY(const AMotionEvent *ev, size_t idx);
float    AMotionEvent_getSize(const AMotionEvent *ev, size_t idx);
float    AMotionEvent_getPressure(const AMotionEvent *ev, size_t idx);
int32_t  AMotionEvent_getPointerId(const AMotionEvent *ev, size_t idx);
float    AMotionEvent_getAxisValue(const AMotionEvent *ev, int32_t axis, size_t idx);
int32_t  AMotionEvent_getToolType(const AMotionEvent *ev, size_t idx);

/* -------- API interna do shim (expandida do NDK publico) --------
 * bridge_input.c usa estes pra materializar eventos vindos do SDL
 * e empilhar na fila que Unity le. */
AInputEvent *shim_input_event_alloc(int32_t type, int32_t source);
void         shim_input_event_free(AInputEvent *ev);
int32_t      shim_input_queue_push(AInputQueue *q, AInputEvent *ev);
AInputQueue *shim_input_queue_global(void);

/* -------- SLES/sLESAll.h: APAGADO em 2026-11-03 (rev. TSP) --------
 * As typedefs SL* desta secao eram um subset INVENTADO (SLobject/SLinterface
 * + slRealizeObject/slGetInterface, que NAO existem no OpenSL ES real) e só
 * eram usadas pelo stub no-op antigo - que era a causa do SIGBUS no
 * nativeResume. O ABI verdadeiro (vtables COM-like de 2 niveis + globais
 * SL_IID_*) esta em include/sles_shim.h, implementado em
 * stubs/opensles_stub.c. Os decls abaixo ficam so como comentario historico
 * e NAO devem ser usados: inclusive, darao erro de assinatura contra
 * sles_shim.h se algum TU incluir os dois. */
typedef uint8_t  SLuint8;   typedef uint16_t SLuint16;
typedef uint32_t SLuint32;  typedef int32_t  SLint32;
typedef uint32_t SLresult;
typedef void    *SLobject;
typedef void    *SLinterface;
#define SL_RESULT_SUCCESS                    ((SLuint32)0x00000000)
#define SL_RESULT_PARAMETER_INVALID          ((SLuint32)0x00000002)
#define SL_RESULT_MEMORY_FAILURE             ((SLuint32)0x00000001)
#define SL_RESULT_FEATURE_UNSUPPORTED        ((SLuint32)0x00000018)
#define SL_OBJECT_STATE_REALIZED             ((SLuint32)0x2)
#define SL_ENGINE_OPTION_NEXT_AVAIL          ((SLuint32)-1)
SLresult slCreateEngine(SLobject *e, SLuint32 nOpt, const void *optItf,
                        SLuint32 nItf, const SLinterface *itf, const void *named);
SLresult slDestroyObject(SLobject o);
SLresult slRealizeObject(SLobject o);
SLresult slGetInterface(SLobject o, const SLinterface iid, SLinterface *iface);
SLresult slSetInterfaceState(SLobject o, const SLinterface iid, SLuint32 s);

/* -------- media/NdkMedia*.h (subset no-op) -------- */
typedef struct AMediaExtractor       AMediaExtractor;
typedef struct AMediaCodec           AMediaCodec;
typedef struct AMediaFormat          AMediaFormat;
typedef struct AMediaCrypto          AMediaCrypto;
typedef struct ABuffer               ABuffer;
typedef int32_t media_status_code;
#define MEDIA_OK 0
#define MEDIA_ERROR_UNKNOWN (-2147483648)
AMediaExtractor *AMediaExtractor_new(void);
void AMediaExtractor_delete(AMediaExtractor *e);

#endif /* ANDROID_SHIM_H */
