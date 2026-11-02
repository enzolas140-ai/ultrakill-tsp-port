/* bridge_jni.c - implementação da mini-JVM.
 *
 * Estratégia:
 *   - jclass/jmethodID/jfieldID/jstring/jobject são todos 'struct _jobjectRef'*
 *     com um tag. Nada de reflection real.
 *   - Cada CallStatic<Type>Method cai num switch em (class, name, sig).
 *     Os casos conhecidos (Unity 2019.4 + Android Build.VERSION.SDK_INT etc)
 *     têm valores hardcoded plausíveis; default retorna zero e loga.
 *   - NewStringUTF aloca strdup. GetStringUTFChars devolve o mesmo ponteiro.
 *     ReleaseStringUTFChars é no-op (nossa string é imutável).
 *   - ExceptionCheck sempre false. Nada lança exceções.
 *   - AttachCurrentThread usa TLS: cada thread tem seu JNIEnv singleton.
 *
 * Isto NÃO é uma JVM. É o suficiente pro Unity 2019.4 passar da fase de
 * boot que hoje SIGSEGV-aria (activity->vm == NULL). */
#include <bridge_jni.h>
#include <android_shim.h>

#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SHIM_UNLIKELY(x) __builtin_expect(!!(x),0)

enum jref_type {
  JT_CLASS = 1, JT_OBJECT, JT_STRING, JT_METHOD, JT_FIELD, JT_ARRAY, JT_WEAK
};

struct _jobjectRef {
  enum jref_type type;
  int            refs;         /* strong refs */
  char          *name;         /* p/ class: "android/os/Build$VERSION"; p/ string: utf8 */
  char          *sig;          /* p/ method/field: "(I)V" etc */
  void          *aux;          /* qualquer */
};

/* pool simples com mutex. Não liberamos nunca até shim_jni_shutdown. */
static pthread_mutex_t pool_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct _jobjectRef **pool = NULL;
static size_t pool_len = 0, pool_cap = 0;

static struct _jobjectRef *ref_alloc(enum jref_type t, const char *name, const char *sig) {
  pthread_mutex_lock(&pool_mtx);
  struct _jobjectRef *r = calloc(1, sizeof(*r));
  r->type = t; r->refs = 1;
  if (name) r->name = strdup(name);
  if (sig)  r->sig  = strdup(sig);
  if (pool_len == pool_cap) {
    pool_cap = pool_cap ? pool_cap*2 : 256;
    pool = realloc(pool, pool_cap * sizeof(*pool));
  }
  pool[pool_len++] = r;
  pthread_mutex_unlock(&pool_mtx);
  return r;
}

static void ref_log_unknown(const char *what, const char *name, const char *sig) {
  fprintf(stderr, "JVM-stub: %s nao mapeado: name=%s sig=%s\n",
          what, name ? name : "-", sig ? sig : "-");
}

/* -------- lookup de métodos/fields "especiais" -------- */
struct known_method {
  const char *cls;   /* "android/os/Build$VERSION" */
  const char *name;  /* "getSDK_INT" */
  const char *sig;   /* "()I" */
  jint        ivalue;
  const char *svalue;
};

static const struct known_method known_ints[] = {
  /* Unity 2019.4 consulta isto via UnityPlayer.getConfiguration(): */
  { "com/unity3d/player/UnityPlayer",    "getConfigurationFromAsset", "(Ljava/lang/String;)[B", 0, NULL },
  /* Version check (Android 8, safe minimum pra Unity 2019.4): */
  { "android/os/Build$VERSION",          "SDK_INT_field_marker",      "I",                        26, NULL },
  /* Surface/VR: Unity chama e tolera 0 */
  { "com/google/androidgamesdk/SwappyDisplayManager", "getFrameInterval", "()J", 0, NULL },
  { "com/unity3d/player/UnityPlayer",    "getImeAttributes",          "()Landroid/view/inputmethod/EditorInfo;", 0, NULL },
  { NULL }
};

static int find_int(const char *cls, const char *name, const char *sig, jint *out) {
  for (int i = 0; known_ints[i].cls; i++)
    if (!strcmp(known_ints[i].cls, cls) &&
        !strcmp(known_ints[i].name, name) &&
        (!sig || !strcmp(known_ints[i].sig, sig))) {
      *out = known_ints[i].ivalue;
      return 1;
    }
  return 0;
}

/* -------- JNI: core -------- */
static jint JNICALL j_GetVersion(JNIEnv *e) { (void)e; return JNI_VERSION_1_6; }

static jclass JNICALL j_FindClass(JNIEnv *e, const char *name) {
  (void)e;
  return (jclass)ref_alloc(JT_CLASS, name ? name : "?", NULL);
}

static jmethodID JNICALL j_GetMethodID(JNIEnv *e, jclass c, const char *name, const char *sig) {
  (void)e;
  const char *cls = c ? ((struct _jobjectRef*)c)->name : "?";
  char buf[512];
  snprintf(buf, sizeof buf, "%s#%s", cls, name ? name : "?");
  return (jmethodID)ref_alloc(JT_METHOD, buf, sig);
}

static jmethodID JNICALL j_GetStaticMethodID(JNIEnv *e, jclass c, const char *name, const char *sig) {
  return j_GetMethodID(e, c, name, sig);
}

static jfieldID JNICALL j_GetFieldID(JNIEnv *e, jclass c, const char *name, const char *sig) {
  (void)e;
  const char *cls = c ? ((struct _jobjectRef*)c)->name : "?";
  char buf[512];
  snprintf(buf, sizeof buf, "%s.%s", cls, name ? name : "?");
  return (jfieldID)ref_alloc(JT_FIELD, buf, sig);
}
static jfieldID JNICALL j_GetStaticFieldID(JNIEnv *e, jclass c, const char *name, const char *sig) {
  return j_GetFieldID(e, c, name, sig);
}

static jobject JNICALL j_NewObject(JNIEnv *e, jclass c, jmethodID m, ...) {
  (void)m;
  return (jobject)ref_alloc(JT_OBJECT, c ? ((struct _jobjectRef*)c)->name : "?", NULL);
}
static jobject JNICALL j_NewObjectV(JNIEnv *e, jclass c, jmethodID m, va_list ap) {
  (void)ap; return j_NewObject(e, c, m);
}
static jobject JNICALL j_NewObjectA(JNIEnv *e, jclass c, jmethodID m, const void *a) {
  (void)a; return j_NewObject(e, c, m);
}

static jclass JNICALL j_GetObjectClass(JNIEnv *e, jobject o) {
  (void)e;
  const char *n = o ? ((struct _jobjectRef*)o)->name : "java/lang/Object";
  return (jclass)ref_alloc(JT_CLASS, n, NULL);
}

static jboolean JNICALL j_IsSameObject(JNIEnv *e, jobject a, jobject b) {
  (void)e; return a == b;
}

static void JNICALL j_DeleteLocalRef(JNIEnv *e, jobject o) { (void)e; (void)o; /* pool */ }
static void JNICALL j_DeleteGlobalRef(JNIEnv *e, jobject o) { (void)e; (void)o; }
static jobject JNICALL j_NewGlobalRef(JNIEnv *e, jobject o) { (void)e; return o; }
static jobject JNICALL j_NewLocalRef(JNIEnv *e, jobject o)  { (void)e; return o; }

static jint JNICALL j_PushLocalFrame(JNIEnv *e, jint n) { (void)e; (void)n; return 0; }
static jobject JNICALL j_PopLocalFrame(JNIEnv *e, jobject r) { (void)e; return r; }
static jint JNICALL j_EnsureLocalCapacity(JNIEnv *e, jint n) { (void)e; (void)n; return 0; }

static jclass JNICALL j_GetSuperclass(JNIEnv *e, jclass c) { (void)e; (void)c; return NULL; }
static jboolean JNICALL j_IsAssignableFrom(JNIEnv *e, jclass s, jclass d) { (void)e; return s == d; }
static jboolean JNICALL j_IsInstanceOf(JNIEnv *e, jobject o, jclass c) { (void)e; (void)o; (void)c; return JNI_TRUE; }

static void JNICALL j_ExceptionDescribe(JNIEnv *e) { (void)e; }
static void JNICALL j_ExceptionClear(JNIEnv *e)    { (void)e; }
static jthrowable JNICALL j_ExceptionOccurred(JNIEnv *e) { (void)e; return NULL; }
static jboolean JNICALL j_ExceptionCheck(JNIEnv *e)      { (void)e; return JNI_FALSE; }
static jint JNICALL j_Throw(JNIEnv *e, jthrowable t)     { (void)e; (void)t; return JNI_OK; }
static jint JNICALL j_ThrowNew(JNIEnv *e, jclass c, const char *msg) {
  (void)e; fprintf(stderr, "JNI throw: %s\n", msg ? msg : "?"); return JNI_OK;
}
static void JNICALL j_FatalError(JNIEnv *e, const char *msg) {
  (void)e; fprintf(stderr, "JNI FatalError: %s\n", msg ? msg : "?"); abort();
}

/* Strings */
static jstring JNICALL j_NewStringUTF(JNIEnv *e, const char *utf) {
  (void)e;
  return (jstring)ref_alloc(JT_STRING, utf ? utf : "", NULL);
}
static const char *JNICALL j_GetStringUTFChars(JNIEnv *e, jstring s, jboolean *copy) {
  (void)e; if (copy) *copy = JNI_TRUE;
  return s ? ((struct _jobjectRef*)s)->name : "";
}
static void JNICALL j_ReleaseStringUTFChars(JNIEnv *e, jstring s, const char *p) {
  (void)e; (void)s; (void)p;
}
static jsize JNICALL j_GetStringLength(JNIEnv *e, jstring s) {
  (void)e; return s ? (jsize)strlen(((struct _jobjectRef*)s)->name) : 0;
}
static jsize JNICALL j_GetStringUTFLength(JNIEnv *e, jstring s) {
  return j_GetStringLength(e, s);
}

/* CallStatic*Method: dispatch via known_ints */
static jint JNICALL j_CallStaticIntMethod(JNIEnv *e, jclass c, jmethodID m, ...) {
  (void)e;
  if (!c || !m) return 0;
  const char *cls  = ((struct _jobjectRef*)c)->name;
  const char *name = ((struct _jobjectRef*)m)->name;   /* "cls#method" */
  const char *sig  = ((struct _jobjectRef*)m)->sig;
  /* strip "cls#" prefix: */
  const char *hash = strchr(name, '#');
  char mname[128];
  if (hash) snprintf(mname, sizeof mname, "%.*s", (int)(hash - name), name);
  else snprintf(mname, sizeof mname, "%s", name);
  jint v = 0;
  if (find_int(cls, mname, sig, &v)) return v;
  ref_log_unknown("CallStaticIntMethod", mname, sig);
  return 0;
}
static jlong JNICALL j_CallStaticLongMethod(JNIEnv *e, jclass c, jmethodID m, ...) { (void)e; (void)c; (void)m; return 0; }
static jboolean JNICALL j_CallStaticBooleanMethod(JNIEnv *e, jclass c, jmethodID m, ...) { (void)e; (void)c; (void)m; return JNI_FALSE; }
static jobject JNICALL j_CallStaticObjectMethod(JNIEnv *e, jclass c, jmethodID m, ...) { (void)e; (void)c; (void)m; return NULL; }
static void JNICALL j_CallStaticVoidMethod(JNIEnv *e, jclass c, jmethodID m, ...) { (void)e; (void)c; (void)m; }

/* Variantes V/A só delegam pro variadic (nós ignoramos args) */
static jint JNICALL j_CallStaticIntMethodV(JNIEnv *e, jclass c, jmethodID m, va_list a) { (void)a; return j_CallStaticIntMethod(e,c,m); }
static jint JNICALL j_CallStaticIntMethodA(JNIEnv *e, jclass c, jmethodID m, void *a)   { (void)a; return j_CallStaticIntMethod(e,c,m); }
static void JNICALL j_CallStaticVoidMethodV(JNIEnv *e, jclass c, jmethodID m, va_list a){ (void)a; j_CallStaticVoidMethod(e,c,m); }
static void JNICALL j_CallStaticVoidMethodA(JNIEnv *e, jclass c, jmethodID m, void *a)  { (void)a; j_CallStaticVoidMethod(e,c,m); }

/* Static fields */
static jint JNICALL j_GetStaticIntField(JNIEnv *e, jclass c, jfieldID f) {
  (void)e;
  if (!c || !f) return 0;
  const char *cls  = ((struct _jobjectRef*)c)->name;
  const char *fname= ((struct _jobjectRef*)f)->name;
  char *dot = strchr(fname, '.');
  char mname[128];
  if (dot) snprintf(mname, sizeof mname, "%s_field_marker", dot+1);
  else snprintf(mname, sizeof mname, "%s", fname);
  const char *sig = ((struct _jobjectRef*)f)->sig;
  jint v = 0;
  if (find_int(cls, mname, sig, &v)) return v;
  /* caso específico: Build$VERSION.SDK_INT */
  if (!strcmp(cls, "android/os/Build$VERSION") && dot && !strcmp(dot+1, "SDK_INT")) return 26;
  ref_log_unknown("GetStaticIntField", fname, sig);
  return 0;
}
static jlong JNICALL j_GetStaticLongField(JNIEnv *e, jclass c, jfieldID f) { (void)e;(void)c;(void)f; return 0; }
static jboolean JNICALL j_GetStaticBooleanField(JNIEnv *e, jclass c, jfieldID f) { (void)e;(void)c;(void)f; return JNI_FALSE; }
static jobject JNICALL j_GetStaticObjectField(JNIEnv *e, jclass c, jfieldID f) { (void)e;(void)c;(void)f; return NULL; }
static void JNICALL j_SetStaticIntField(JNIEnv *e, jclass c, jfieldID f, jint v) { (void)e;(void)c;(void)f;(void)v; }
static void JNICALL j_SetStaticObjectField(JNIEnv *e, jclass c, jfieldID f, jobject v) { (void)e;(void)c;(void)f;(void)v; }

/* Instancia fields (todos 0/NULL - Unity raramente usa) */
#define FIELD_STUB_INT(name)   static jint JNICALL name(JNIEnv *e, jobject o, jfieldID f) { (void)e;(void)o;(void)f; return 0; }
#define FIELD_STUB_OBJ(name)   static jobject JNICALL name(JNIEnv *e, jobject o, jfieldID f) { (void)e;(void)o;(void)f; return NULL; }
#define FIELD_STUB_SET_I(name) static void JNICALL name(JNIEnv *e, jobject o, jfieldID f, jint v) { (void)e;(void)o;(void)f;(void)v; }
FIELD_STUB_INT(j_GetIntField)
FIELD_STUB_INT(j_GetLongField)
FIELD_STUB_INT(j_GetBooleanField)
FIELD_STUB_INT(j_GetByteField)
FIELD_STUB_INT(j_GetCharField)
FIELD_STUB_INT(j_GetShortField)
FIELD_STUB_INT(j_GetFloatField)
FIELD_STUB_INT(j_GetDoubleField)
FIELD_STUB_OBJ(j_GetObjectField)
FIELD_STUB_SET_I(j_SetIntField)
FIELD_STUB_SET_I(j_SetLongField)
FIELD_STUB_SET_I(j_SetBooleanField)
FIELD_STUB_SET_I(j_SetByteField)
FIELD_STUB_SET_I(j_SetCharField)
FIELD_STUB_SET_I(j_SetShortField)
FIELD_STUB_SET_I(j_SetFloatField)
FIELD_STUB_SET_I(j_SetDoubleField)
static void JNICALL j_SetObjectField(JNIEnv *e, jobject o, jfieldID f, jobject v) { (void)e;(void)o;(void)f;(void)v; }

/* Call*Method (virtual): stubs zeros */
static jint JNICALL j_CallIntMethod(JNIEnv *e, jobject o, jmethodID m, ...) { (void)e;(void)o;(void)m; return 0; }
static jint JNICALL j_CallIntMethodV(JNIEnv *e, jobject o, jmethodID m, va_list a){ (void)a; return j_CallIntMethod(e,o,m); }
static jint JNICALL j_CallIntMethodA(JNIEnv *e, jobject o, jmethodID m, void *a)  { (void)a; return j_CallIntMethod(e,o,m); }
static void JNICALL j_CallVoidMethod(JNIEnv *e, jobject o, jmethodID m, ...) { (void)e;(void)o;(void)m; }
static void JNICALL j_CallVoidMethodV(JNIEnv *e, jobject o, jmethodID m, va_list a){ (void)a; j_CallVoidMethod(e,o,m); }
static void JNICALL j_CallVoidMethodA(JNIEnv *e, jobject o, jmethodID m, void *a)  { (void)a; j_CallVoidMethod(e,o,m); }
static jobject JNICALL j_CallObjectMethod(JNIEnv *e, jobject o, jmethodID m, ...) { (void)e;(void)o;(void)m; return NULL; }
static jobject JNICALL j_CallObjectMethodV(JNIEnv *e, jobject o, jmethodID m, va_list a){ (void)a; return j_CallObjectMethod(e,o,m); }
static jobject JNICALL j_CallObjectMethodA(JNIEnv *e, jobject o, jmethodID m, void *a)  { (void)a; return j_CallObjectMethod(e,o,m); }
static jboolean JNICALL j_CallBooleanMethod(JNIEnv *e, jobject o, jmethodID m, ...) { (void)e;(void)o;(void)m; return JNI_FALSE; }
static jboolean JNICALL j_CallBooleanMethodV(JNIEnv *e, jobject o, jmethodID m, va_list a){ (void)a; return j_CallBooleanMethod(e,o,m); }
static jboolean JNICALL j_CallBooleanMethodA(JNIEnv *e, jobject o, jmethodID m, void *a){ (void)a; return j_CallBooleanMethod(e,o,m); }

/* AllocObject + DirectByteBuffer: */
static jobject JNICALL j_AllocObject(JNIEnv *e, jclass c) {
  (void)e; return (jobject)ref_alloc(JT_OBJECT, c ? ((struct _jobjectRef*)c)->name : "?", NULL);
}
static jobject JNICALL j_NewDirectByteBuffer(JNIEnv *e, void *addr, jlong cap) {
  (void)e;
  struct _jobjectRef *r = ref_alloc(JT_OBJECT, "java/nio/DirectByteBuffer", NULL);
  r->aux = addr;
  *(long*)&r->aux = (long)addr | ((long)cap << 1);  /* hack: guarda os dois */
  return r;
}
static void *JNICALL j_GetDirectBufferAddress(JNIEnv *e, jobject b) {
  (void)e;
  if (!b) return NULL;
  struct _jobjectRef *r = (void*)b;
  return (void*)(uintptr_t)((long)r->aux & 0xFFFFFFFFFFFFL);
}
static jlong JNICALL j_GetDirectBufferCapacity(JNIEnv *e, jobject b) {
  (void)e;
  if (!b) return 0;
  return (jlong)(((long)((struct _jobjectRef*)b)->aux) >> 48);
}

/* Arrays minimais */
static jsize JNICALL j_GetArrayLength(JNIEnv *e, jarray a) { (void)e; (void)a; return 0; }
static jobjectArray JNICALL j_NewObjectArray(JNIEnv *e, jsize n, jclass c, jobject init) {
  (void)e; (void)n; (void)c; (void)init;
  return (jobjectArray)ref_alloc(JT_ARRAY, "array", NULL);
}
static jobject JNICALL j_GetObjectArrayElement(JNIEnv *e, jobjectArray a, jsize i) {
  (void)e; (void)a; (void)i; return NULL;
}
static void JNICALL j_SetObjectArrayElement(JNIEnv *e, jobjectArray a, jsize i, jobject v) {
  (void)e; (void)a; (void)i; (void)v;
}
static jbyteArray JNICALL j_NewByteArray(JNIEnv *e, jsize n) {
  (void)e;
  struct _jobjectRef *r = ref_alloc(JT_ARRAY, "byte[]", NULL);
  r->aux = calloc(1, n > 0 ? n : 1);
  *(jsize*)&r->aux = (jsize)((uintptr_t)r->aux);  /* simplificado */
  return (jbyteArray)r;
}
static jbyte *JNICALL j_GetByteArrayElements(JNIEnv *e, jbyteArray a, jboolean *isCopy) {
  (void)e; if (isCopy) *isCopy = JNI_FALSE; return a ? (jbyte*)((struct _jobjectRef*)a)->aux : NULL;
}
static void JNICALL j_ReleaseByteArrayElements(JNIEnv *e, jbyteArray a, jbyte *p, jint mode) {
  (void)e; (void)a; (void)p; (void)mode;
}
static void JNICALL j_GetByteArrayRegion(JNIEnv *e, jbyteArray a, jsize st, jsize ln, jbyte *dst) {
  (void)e;
  jbyte *src = a ? (jbyte*)((struct _jobjectRef*)a)->aux : NULL;
  if (src && dst && ln > 0) memcpy(dst, src+st, ln);
}
static jint JNICALL j_SetByteArrayRegion(JNIEnv *e, jbyteArray a, jsize st, jsize ln, const jbyte *src) {
  (void)e;
  jbyte *dst = a ? (jbyte*)((struct _jobjectRef*)a)->aux : NULL;
  if (dst && src && ln > 0) memcpy(dst+st, src, ln);
  return 0;
}

/* Misc */
static jint JNICALL j_RegisterNatives(JNIEnv *e, jclass c, const void *methods, jint n) {
  (void)e; (void)c; (void)methods; (void)n; return JNI_OK;
}
static jint JNICALL j_UnregisterNatives(JNIEnv *e, jclass c) { (void)e; (void)c; return JNI_OK; }
static jint JNICALL j_MonitorEnter(JNIEnv *e, jobject o) { (void)e; (void)o; return JNI_OK; }
static jint JNICALL j_MonitorExit(JNIEnv *e, jobject o)  { (void)e; (void)o; return JNI_OK; }

/* GetObjectRefType - 0=Invalid, 1=Local, 2=Global, 3=Weak, 4=Indexed */
static jint JNICALL j_GetObjectRefType(JNIEnv *e, jobject o) { (void)e; (void)o; return 1; }

/* -------- vtable estática -------- */
static const struct JNINativeInterface_ g_env_vtable = {
  .reserved0 = NULL, .reserved1 = NULL, .reserved2 = NULL, .reserved3 = NULL,
  .GetVersion = j_GetVersion,
  .FindClass = j_FindClass,
  .FromReflectedMethod = NULL, .FromReflectedField = NULL, .ToReflectedMethod = NULL,
  .GetSuperclass = j_GetSuperclass,
  .IsAssignableFrom = j_IsAssignableFrom,
  .ToReflectedField = NULL,
  .Throw = j_Throw, .ThrowNew = j_ThrowNew,
  .ExceptionOccurred = j_ExceptionOccurred,
  .ExceptionDescribe = j_ExceptionDescribe,
  .ExceptionClear = j_ExceptionClear,
  .FatalError = j_FatalError,
  .PushLocalFrame = j_PushLocalFrame, .PopLocalFrame = j_PopLocalFrame,
  .NewGlobalRef = j_NewGlobalRef, .DeleteGlobalRef = j_DeleteGlobalRef,
  .DeleteLocalRef = j_DeleteLocalRef,
  .IsSameObject = j_IsSameObject,
  .NewLocalRef = j_NewLocalRef,
  .EnsureLocalCapacity = j_EnsureLocalCapacity,
  .AllocObject = j_AllocObject,
  .NewObject = j_NewObject, .NewObjectV = j_NewObjectV, .NewObjectA = j_NewObjectA,
  .GetObjectClass = j_GetObjectClass,
  .IsInstanceOf = j_IsInstanceOf,
  .GetMethodID = j_GetMethodID,
  .CallObjectMethod = j_CallObjectMethod,
  .CallObjectMethodV = j_CallObjectMethodV,
  .CallObjectMethodA = j_CallObjectMethodA,
  .CallBooleanMethod = j_CallBooleanMethod,
  .CallBooleanMethodV = j_CallBooleanMethodV,
  .CallBooleanMethodA = j_CallBooleanMethodA,
  .CallIntMethod = j_CallIntMethod,
  .CallIntMethodV = j_CallIntMethodV,
  .CallIntMethodA = j_CallIntMethodA,
  .CallVoidMethod = j_CallVoidMethod,
  .CallVoidMethodV = j_CallVoidMethodV,
  .CallVoidMethodA = j_CallVoidMethodA,
  /* nonvirtual + char/byte/short/long/float/double: NULL (Unity nao usa) */
  .GetFieldID = j_GetFieldID,
  .GetObjectField = j_GetObjectField,
  .GetIntField = j_GetIntField,
  .SetObjectField = j_SetObjectField,
  .SetIntField = j_SetIntField,
  .GetStaticMethodID = j_GetStaticMethodID,
  .CallStaticIntMethod = j_CallStaticIntMethod,
  .CallStaticIntMethodV = j_CallStaticIntMethodV,
  .CallStaticIntMethodA = j_CallStaticIntMethodA,
  .CallStaticVoidMethod = j_CallStaticVoidMethod,
  .CallStaticVoidMethodV = j_CallStaticVoidMethodV,
  .CallStaticVoidMethodA = j_CallStaticVoidMethodA,
  .CallStaticObjectMethod = j_CallStaticObjectMethod,
  .CallStaticBooleanMethod = j_CallStaticBooleanMethod,
  .CallStaticLongMethod = j_CallStaticLongMethod,
  .GetStaticFieldID = j_GetStaticFieldID,
  .GetStaticIntField = j_GetStaticIntField,
  .GetStaticBooleanField = j_GetStaticBooleanField,
  .GetStaticObjectField = j_GetStaticObjectField,
  .SetStaticIntField = j_SetStaticIntField,
  .NewString = NULL,
  .GetStringLength = j_GetStringLength,
  .NewStringUTF = j_NewStringUTF,
  .GetStringUTFLength = j_GetStringUTFLength,
  .GetStringUTFChars = j_GetStringUTFChars,
  .ReleaseStringUTFChars = j_ReleaseStringUTFChars,
  .GetArrayLength = j_GetArrayLength,
  .NewObjectArray = j_NewObjectArray,
  .GetObjectArrayElement = j_GetObjectArrayElement,
  .SetObjectArrayElement = j_SetObjectArrayElement,
  .NewByteArray = j_NewByteArray,
  .GetByteArrayElements = j_GetByteArrayElements,
  .ReleaseByteArrayElements = j_ReleaseByteArrayElements,
  .GetByteArrayRegion = j_GetByteArrayRegion,
  .SetByteArrayRegion = j_SetByteArrayRegion,
  .RegisterNatives = j_RegisterNatives,
  .UnregisterNatives = j_UnregisterNatives,
  .MonitorEnter = j_MonitorEnter, .MonitorExit = j_MonitorExit,
  .ExceptionCheck = j_ExceptionCheck,
  .NewDirectByteBuffer = j_NewDirectByteBuffer,
  .GetDirectBufferAddress = j_GetDirectBufferAddress,
  .GetDirectBufferCapacity = j_GetDirectBufferCapacity,
  .GetObjectRefType = j_GetObjectRefType,
};

/* -------- JavaVM + TLS env -------- */
static JavaVM g_vm;
static __thread JNIEnv *tls_env = NULL;
static pthread_mutex_t env_mtx = PTHREAD_MUTEX_INITIALIZER;

static jint JNICALL vm_AttachCurrentThread(JavaVM *vm, void **penv, void *args) {
  (void)vm; (void)args;
  if (!tls_env) {
    /* IMPORTANTE: o typedef e `typedef struct JNINativeInterface_ JNIEnv;`,
     * entao `JNIEnv *` ja e um ponteiro para a struct vtable inteira.
     * Antigamente eu fazia calloc + escrevia endereco da vtable em offset 0,
     * mas isso fazia env->FindClass ler offset ~40 (calloc'd zero) = NULL
     * = SIGSEGV ao chamar. Agora apontamos direto p/ a estatica. */
    tls_env = (JNIEnv*)&g_env_vtable;
  }
  if (penv) *penv = tls_env;
  return JNI_OK;
}
static jint JNICALL vm_DetachCurrentThread(JavaVM *vm) { (void)vm; return JNI_OK; }
static jint JNICALL vm_DestroyJavaVM(JavaVM *vm) { (void)vm; _exit(0); }
static jint JNICALL vm_GetEnv(JavaVM *vm, void **penv, jint version) {
  (void)version; return vm_AttachCurrentThread(vm, penv, NULL);
}
static jint JNICALL vm_AttachCurrentThreadAsDaemon(JavaVM *vm, void **penv, void *args) {
  return vm_AttachCurrentThread(vm, penv, args);
}
static const struct JNIInvokeInterface_ g_vm_vtable = {
  .DestroyJavaVM = vm_DestroyJavaVM,
  .AttachCurrentThread = vm_AttachCurrentThread,
  .DetachCurrentThread = vm_DetachCurrentThread,
  .GetEnv = vm_GetEnv,
  .AttachCurrentThreadAsDaemon = vm_AttachCurrentThreadAsDaemon,
};

__attribute__((visibility("default")))
JavaVM *shim_jni_get_vm(void) {
  /* Retorna a propria vtable como JavaVM* (JavaVM = struct JNIInvokeInterface_) */
  return (JavaVM*)&g_vm_vtable;
}

__attribute__((visibility("default")))
JNIEnv *shim_jni_new_env_for_thread(const char *name) {
  (void)name;
  if (!tls_env) {
    void *e;
    vm_AttachCurrentThread(&g_vm, &e, NULL);
  }
  return tls_env;
}

__attribute__((visibility("default")))
void shim_jni_install(void) {
  shim_jni_get_vm();
  /* Força um JNIEnv na thread main (Unity vai AttachCurrentThread nas outras) */
  shim_jni_new_env_for_thread("main");
}
