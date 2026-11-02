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

/* ======== mini-runtime Java p/ cadenas de CallObjectMethod ========
 * initJni e o boot do Unity Android chamam getSplashSurface/getAssets/
 * getClassLoader/new File(...).getAbsolutePath() etc via JNI. Stub que
 * devolve NULL faz o C++ derefar NULL (crash pc=0x0 visto no device).
 * Este dispatcher devolve objetos plausiveis encadeaveis em toda parte. */
static const char *mbase(const char *disp) {
  const char *h = disp ? strchr(disp, '#') : NULL;
  return h ? h + 1 : (disp ? disp : "?");
}
static char *xstrdup_(const char *s) { char *r = strdup(s ? s : ""); return r; }
__attribute__((unused))
static char *shim_sys_path(const char *sub) {
  const char *root = getenv("ULTRAKILL_ASSET_ROOT") ? getenv("ULTRAKILL_ASSET_ROOT") : "/tmp";
  char *b = malloc(strlen(root) + 64);
  sprintf(b, "%s", root);
  char *g = strstr(b, "/gamedata");
  if (g) *g = 0;
  char *out = malloc(strlen(b) + strlen(sub) + 8);
  sprintf(out, "%s/%s", b, sub);
  free(b);
  return out;
}
static jobject new_obj(const char *cls) {
  return (jobject)ref_alloc(JT_OBJECT, cls, NULL);
}
__attribute__((unused))
static jobject clone_as(const jobject o, const char *cls) {
  struct _jobjectRef *r = (struct _jobjectRef*)o;
  if (r) { free(r->name); r->name = xstrdup_(cls); }
  return o;
}

static jobject dispatch_obj(JNIEnv *env, jobject obj, jmethodID m, void *args) {
  (void)args;
  if (!m) return NULL;
  struct _jobjectRef *mr = (struct _jobjectRef*)m;
  const char *mn = mbase(mr->name);
  struct _jobjectRef *or_ = (struct _jobjectRef*)obj;
  const char *cls = (or_ && or_->name) ? or_->name : "java/lang/Object";
  fprintf(stderr, "JNI obj-call: %s#%s\n", cls, mn);

  if (!strcmp(mn, "getClass"))
    return (jobject)ref_alloc(JT_CLASS, cls, NULL);
  if (!strcmp(mn, "getName") && strncmp(cls, "java/lang/", 10) == 0) {
    const char *n = strrchr(cls, '/'); n = n ? n + 1 : cls;
    return (jobject)ref_alloc(JT_STRING, n, NULL);
  }
  if (!strcmp(mn, "getClassLoader"))  return new_obj("java/lang/ClassLoader");
  if (!strcmp(mn, "loadClass"))       return (jobject)ref_alloc(JT_CLASS, "java/lang/Object", NULL);
  if (!strcmp(mn, "getAssets"))       return new_obj("android/content/res/AssetManager");
  if (!strcmp(mn, "getResourceStream")) return new_obj("java/io/InputStream");
  if (!strcmp(mn, "createNativeWindow") || !strcmp(mn, "getSurface"))
    return new_obj("android/view/Surface");
  if (!strcmp(mn, "getApplicationInfo")) {
    jobject ai = new_obj("android/content/pm/ApplicationInfo");
    return ai;
  }
  if (!strcmp(mn, "currentThread"))   return new_obj("java/lang/Thread");
  if (!strcmp(mn, "getStackTrace")) {
    struct _jobjectRef *r = ref_alloc(JT_ARRAY, "StackTraceElement[]", NULL);
    r->aux = calloc(1, sizeof(void*));
    ((void**)r->aux)[0] = new_obj("java/lang/StackTraceElement");
    *(jsize*)&r->refs = 1;   /* refs vira length p/ arrays (nunca liberamos) */
    return (jobject)r;
  }
  if (!strcmp(mn, "getFileDescriptor")) return new_obj("java/io/FileDescriptor");
  if (!strcmp(mn, "absolutePath") || !strcmp(mn, "getPath")) {
    struct _jobjectRef *fr = (struct _jobjectRef*)obj;
    const char *p = (fr && fr->aux) ? (const char*)fr->aux
                 : (fr && fr->name ? fr->name : "/data/data/arkboxgames.ultrakill/files");
    return (jobject)ref_alloc(JT_STRING, p, NULL);
  }
  if (!strcmp(mn, "getAbsolutePath") || !strcmp(mn, "getCanonicalPath")) {
    struct _jobjectRef *fr = (struct _jobjectRef*)obj;
    const char *p = (fr && fr->aux) ? (const char*)fr->aux : "/tmp";
    char *rp = xstrdup_(p);
    if (fr) { free(fr->aux); fr->aux = rp; }
    return (jobject)ref_alloc(JT_STRING, rp, NULL);
  }
  /* File(String) constructor via NewObject cai aqui tb (cls=java/io/File) */
  if (!strcmp(cls, "java/lang/String")) {
    struct _jobjectRef *sr = (struct _jobjectRef*)obj;
    const char *s = sr && sr->name ? sr->name : "";
    if (!strcmp(mn, "length"))   return (jobject)(intptr_t)strlen(s);
    if (!strcmp(mn, "isEmpty"))  return (jobject)(intptr_t)(strlen(s) == 0);
    if (!strcmp(mn, "toString")) return obj;
    if (!strcmp(mn, "toLowerCase") || !strcmp(mn, "toUpperCase") ||
        !strcmp(mn, "trim"))     return obj;
  }
  /* default: objeto generico encadeavel (classe = metodo chamado) */
  return new_obj(mn);
}

static jobject JNICALL j_CallObjectMethod(JNIEnv *e, jobject o, jmethodID m, ...) {
  return dispatch_obj(e, o, m, NULL);
}
static jobject JNICALL j_CallObjectMethodV(JNIEnv *e, jobject o, jmethodID m, va_list a) { (void)a; return j_CallObjectMethod(e, o, m); }
static jobject JNICALL j_CallObjectMethodA(JNIEnv *e, jobject o, jmethodID m, void *args) { return dispatch_obj(e, o, m, args); }
/* NewObject com args: File(path) etc - marca o path em aux */
static jobject JNICALL j_NewObject_impl(JNIEnv *e, jclass c, jmethodID m, const char *sarg) {
  (void)e; (void)m;
  const char *cn = c ? ((struct _jobjectRef*)c)->name : "?";
  struct _jobjectRef *r = ref_alloc(JT_OBJECT, cn, NULL);
  if (sarg && !strncmp(cn, "java/io/File", 12)) r->aux = xstrdup_(sarg);
  return (jobject)r;
}
static jobject JNICALL j_NewObject(JNIEnv *e, jclass c, jmethodID m, ...) {
  /* variadic: primeiro arg pointer-sized = provavel string p/ File */
  va_list ap; va_start(ap, m);
  const char *s = va_arg(ap, const char *);
  va_end(ap);
  return j_NewObject_impl(e, c, m, s);
}
static jobject JNICALL j_NewObjectV(JNIEnv *e, jclass c, jmethodID m, va_list ap) {
  const char *s = va_arg(ap, const char *);
  return j_NewObject_impl(e, c, m, s);
}
static jobject JNICALL j_NewObjectA(JNIEnv *e, jclass c, jmethodID m, const void *a) {
  return j_NewObject_impl(e, c, m, (const char*)a);
}

static void JNICALL j_CallVoidMethod(JNIEnv *e, jobject o, jmethodID m, ...) { (void)e;(void)o;(void)m; }
static void JNICALL j_CallVoidMethodV(JNIEnv *e, jobject o, jmethodID m, va_list a){ (void)a; j_CallVoidMethod(e,o,m); }
static void JNICALL j_CallVoidMethodA(JNIEnv *e, jobject o, jmethodID m, void *a)  { (void)a; j_CallVoidMethod(e,o,m); }
static jboolean JNICALL j_CallBooleanMethod(JNIEnv *e, jobject o, jmethodID m, ...) {
  (void)e;(void)o;
  const char *mn = m ? mbase(((struct _jobjectRef*)m)->name) : "";
  fprintf(stderr, "JNI bool-call: %s\n", mn);
  if (o && !strcmp(mn, "hasNext")) return JNI_FALSE;
  return JNI_FALSE;
}
static jboolean JNICALL j_CallBooleanMethodV(JNIEnv *e, jobject o, jmethodID m, va_list a){ (void)a; return j_CallBooleanMethod(e,o,m); }
static jboolean JNICALL j_CallBooleanMethodA(JNIEnv *e, jobject o, jmethodID m, void *a){ (void)a; return j_CallBooleanMethod(e,o,m); }
static jint JNICALL j_CallIntMethod(JNIEnv *e, jobject o, jmethodID m, ...) {
  (void)e;
  if (!m) return 0;
  const char *mn = mbase(((struct _jobjectRef*)m)->name);
  const char *cls = (o && ((struct _jobjectRef*)o)->name) ? ((struct _jobjectRef*)o)->name : "?";
  fprintf(stderr, "JNI int-call: %s#%s\n", cls, mn);
  if (o && !strcmp(mn, "length"))
    return (jint)strlen(((struct _jobjectRef*)o)->name ? ((struct _jobjectRef*)o)->name : "");
  return 0;
}
static jint JNICALL j_CallIntMethodV(JNIEnv *e, jobject o, jmethodID m, va_list a){ (void)a; return j_CallIntMethod(e,o,m); }
static jint JNICALL j_CallIntMethodA(JNIEnv *e, jobject o, jmethodID m, void *a)  { (void)a; return j_CallIntMethod(e,o,m); }
static jlong JNICALL j_CallLongMethod(JNIEnv *e, jobject o, jmethodID m, ...) { (void)e;(void)o;(void)m; return 0; }
static jlong JNICALL j_CallLongMethodV(JNIEnv *e, jobject o, jmethodID m, va_list a){ (void)a; return 0; }
static jlong JNICALL j_CallLongMethodA(JNIEnv *e, jobject o, jmethodID m, void *a)  { (void)a; return 0; }

static jclass JNICALL j_GetObjectClass(JNIEnv *e, jobject o) {
  (void)e;
  const char *n = o ? ((struct _jobjectRef*)o)->name : "java/lang/Object";
  return (jclass)ref_alloc(JT_CLASS, n ? n : "java/lang/Object", NULL);
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
static jobject JNICALL j_CallStaticObjectMethod(JNIEnv *e, jclass c, jmethodID m, ...) {
  (void)e; (void)m;
  const char *cn = c ? ((struct _jobjectRef*)c)->name : "?";
  fprintf(stderr, "JNI static-obj-call: %s#%s\n", cn,
          m ? mbase(((struct _jobjectRef*)m)->name) : "?");
  return new_obj(cn);   /* NUNCA NULL - chamadores deref o resultado */
}
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

/* Call*Method (virtual): agora vao todos pelo dispatcher acima */

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
/* Arrays minimais - versionais funcionais. Layout: refs (int32) = length,
 * aux = buffer do heap. Init zera o max de tipos p/ avoids NaN via bytes. */
static jsize JNICALL j_GetArrayLength(JNIEnv *e, jarray a) {
  (void)e; return a ? (jsize)((struct _jobjectRef*)a)->refs : 0;
}
static jobjectArray JNICALL j_NewObjectArray(JNIEnv *e, jsize n, jclass c, jobject init) {
  (void)e; (void)c; (void)init;
  struct _jobjectRef *r = ref_alloc(JT_ARRAY, "Object[]", NULL);
  r->refs = n > 0 ? n : 0;
  r->aux = calloc((size_t)(n > 0 ? n : 1), sizeof(void*));
  if (init) for (jsize i = 0; i < n; i++) ((void**)r->aux)[i] = init;
  return (jobjectArray)r;
}
static jobject JNICALL j_GetObjectArrayElement(JNIEnv *e, jobjectArray a, jsize i) {
  (void)e;
  if (!a) return NULL;
  struct _jobjectRef *r = (struct _jobjectRef*)a;
  if (i < 0 || i >= r->refs || !r->aux) return new_obj("java/lang/Object");
  void *el = ((void**)r->aux)[i];
  return el ? el : new_obj("java/lang/Object");
}
static void JNICALL j_SetObjectArrayElement(JNIEnv *e, jobjectArray a, jsize i, jobject v) {
  (void)e;
  if (!a || i < 0 || i >= ((struct _jobjectRef*)a)->refs) return;
  ((void**)((struct _jobjectRef*)a)->aux)[i] = v;
}
#define NEW_PRIM_ARRAY(fname, jtype, elem)                                     \
  static jtype##Array JNICALL fname(JNIEnv *e, jsize n) {                       \
    (void)e;                                                                   \
    struct _jobjectRef *r = ref_alloc(JT_ARRAY, #jtype "[]", NULL);            \
    r->refs = n > 0 ? n : 0;                                                   \
    r->aux = calloc((size_t)(n > 0 ? n : 1), elem);                            \
    return (jtype##Array)r;                                                    \
  }
NEW_PRIM_ARRAY(j_NewBooleanArray, jboolean, 1)
NEW_PRIM_ARRAY(j_NewByteArray,   jbyte,   1)
NEW_PRIM_ARRAY(j_NewCharArray,   jchar,   2)
NEW_PRIM_ARRAY(j_NewShortArray,  jshort,  2)
NEW_PRIM_ARRAY(j_NewIntArray,    jint,    4)
NEW_PRIM_ARRAY(j_NewLongArray,   jlong,   8)
NEW_PRIM_ARRAY(j_NewFloatArray,  jfloat,  4)
NEW_PRIM_ARRAY(j_NewDoubleArray, jdouble, 8)

#define GET_ARRAY_ELEMENTS(fn, jtype)                                          \
  static jtype *JNICALL fn(JNIEnv *e, jtype##Array a, jboolean *isCopy) {       \
    (void)e; if (isCopy) *isCopy = JNI_FALSE;                                  \
    return a ? (jtype*)((struct _jobjectRef*)a)->aux : NULL;                   \
  }
GET_ARRAY_ELEMENTS(j_GetBooleanArrayElements, jboolean)
GET_ARRAY_ELEMENTS(j_GetByteArrayElements,    jbyte)
GET_ARRAY_ELEMENTS(j_GetCharArrayElements,    jchar)
GET_ARRAY_ELEMENTS(j_GetShortArrayElements,   jshort)
GET_ARRAY_ELEMENTS(j_GetIntArrayElements,     jint)
GET_ARRAY_ELEMENTS(j_GetLongArrayElements,    jlong)
GET_ARRAY_ELEMENTS(j_GetFloatArrayElements,   jfloat)
GET_ARRAY_ELEMENTS(j_GetDoubleArrayElements,  jdouble)

#define RELEASE_ARRAY(fn, jtype)                                               \
  static void JNICALL fn(JNIEnv *e, jtype##Array a, jtype *p, jint mode) {     \
    (void)e; (void)a; (void)p; (void)mode;                                    \
  }
RELEASE_ARRAY(j_ReleaseBooleanArrayElements, jboolean)
RELEASE_ARRAY(j_ReleaseByteArrayElements,    jbyte)
RELEASE_ARRAY(j_ReleaseCharArrayElements,    jchar)
RELEASE_ARRAY(j_ReleaseShortArrayElements,   jshort)
RELEASE_ARRAY(j_ReleaseIntArrayElements,     jint)
RELEASE_ARRAY(j_ReleaseLongArrayElements,    jlong)
RELEASE_ARRAY(j_ReleaseFloatArrayElements,   jfloat)
RELEASE_ARRAY(j_ReleaseDoubleArrayElements,  jdouble)

#define GET_REGION(fn, jtype)                                                  \
  static void JNICALL fn(JNIEnv *e, jtype##Array a, jsize st, jsize ln, jtype *dst) { \
    (void)e; jtype *src = a ? (jtype*)((struct _jobjectRef*)a)->aux : NULL;    \
    if (src && dst && ln > 0) memcpy(dst, src + st, (size_t)ln * sizeof(jtype)); \
  }
GET_REGION(j_GetBooleanArrayRegion, jboolean)
GET_REGION(j_GetByteArrayRegion,    jbyte)
GET_REGION(j_GetCharArrayRegion,    jchar)
GET_REGION(j_GetShortArrayRegion,   jshort)
GET_REGION(j_GetIntArrayRegion,     jint)
GET_REGION(j_GetLongArrayRegion,    jlong)
GET_REGION(j_GetFloatArrayRegion,   jfloat)
GET_REGION(j_GetDoubleArrayRegion,  jdouble)

#define SET_REGION(fn, jtype)                                                  \
  static jint JNICALL fn(JNIEnv *e, jtype##Array a, jsize st, jsize ln, const jtype *src) { \
    (void)e; jtype *dst = a ? (jtype*)((struct _jobjectRef*)a)->aux : NULL;    \
    if (dst && src && ln > 0) memcpy(dst + st, src, (size_t)ln * sizeof(jtype)); \
    return 0;                                                                  \
  }
SET_REGION(j_SetBooleanArrayRegion, jboolean)
SET_REGION(j_SetByteArrayRegion,    jbyte)
SET_REGION(j_SetCharArrayRegion,    jchar)
SET_REGION(j_SetShortArrayRegion,   jshort)
SET_REGION(j_SetIntArrayRegion,     jint)
SET_REGION(j_SetLongArrayRegion,    jlong)
SET_REGION(j_SetFloatArrayRegion,   jfloat)
SET_REGION(j_SetDoubleArrayRegion,  jdouble)

static void *JNICALL j_GetPrimitiveArrayCritical(JNIEnv *e, jarray a, jboolean *ic) {
  (void)e; if (ic) *ic = JNI_FALSE;
  return a ? ((struct _jobjectRef*)a)->aux : NULL;
}
static void JNICALL j_ReleasePrimitiveArrayCritical(JNIEnv *e, jarray a, void *p, jint m) {
  (void)e; (void)a; (void)p; (void)m;
}

/* Static OBJECT fields: Unity le Build.VERSION.SDK_INT (int, ok antes) e
 * tambem objetos (contexto, classloader). Sempre devolvemos ref plausivel. */
static jobject JNICALL j_GetStaticObjectField(JNIEnv *e, jclass c, jfieldID f) {
  (void)e;
  const char *cn = c ? ((struct _jobjectRef*)c)->name : "?";
  const char *fn_ = f ? ((struct _jobjectRef*)f)->name : "?";
  fprintf(stderr, "JNI GetStaticObjectField: %s.%s\n", cn, fn_);
  return new_obj(cn);
}

/* Misc */
/* JNINativeMethod conforme jni.h: { char*name; char*signature; void*fnPtr }. */
typedef struct { const char *name; const char *signature; void *fnPtr; } shim_JNINativeMethod;

#define SHIM_MAX_NATIVES 1024
static struct {
  char cls[160];
  char name[160];
  char sig[160];
  void *fn;
} g_natives[SHIM_MAX_NATIVES];
static int g_natives_n = 0;

static jint JNICALL j_RegisterNatives(JNIEnv *e, jclass c, const void *methods_, jint n) {
  (void)e;
  const shim_JNINativeMethod *m = (const shim_JNINativeMethod*)methods_;
  const char *clsname = c ? ((struct _jobjectRef*)c)->name : "?";
  fprintf(stderr, "JNI RegisterNatives: class=%s n=%d\n", clsname, (int)n);
  for (int i = 0; i < n && m; i++) {
    fprintf(stderr, "   native[%d] %s %s -> %p\n", i,
            m[i].name ? m[i].name : "?",
            m[i].signature ? m[i].signature : "?",
            m[i].fnPtr);
    if (g_natives_n < SHIM_MAX_NATIVES) {
      snprintf(g_natives[g_natives_n].cls, sizeof g_natives[g_natives_n].cls, "%s", clsname);
      snprintf(g_natives[g_natives_n].name, sizeof g_natives[g_natives_n].name, "%s",
               m[i].name ? m[i].name : "?");
      snprintf(g_natives[g_natives_n].sig, sizeof g_natives[g_natives_n].sig, "%s",
               m[i].signature ? m[i].signature : "");
      g_natives[g_natives_n].fn = m[i].fnPtr;
      g_natives_n++;
    }
  }
  return JNI_OK;
}

/* API p/ o bridge invocar um native registrado (ex: nativeSetup/nativeRun). */
__attribute__((visibility("default")))
void *shim_jni_find_native(const char *cls, const char *method) {
  for (int i = 0; i < g_natives_n; i++)
    if ((!cls || !strcmp(g_natives[i].cls, cls)) &&
        !strcmp(g_natives[i].name, method))
      return g_natives[i].fn;
  return NULL;
}
__attribute__((visibility("default")))
const char *shim_jni_native_sig(const char *cls, const char *method) {
  for (int i = 0; i < g_natives_n; i++)
    if ((!cls || !strcmp(g_natives[i].cls, cls)) &&
        !strcmp(g_natives[i].name, method))
      return g_natives[i].sig;
  return NULL;
}
__attribute__((visibility("default")))
int shim_jni_natives_count(void) { return g_natives_n; }
__attribute__((visibility("default")))
void shim_jni_dump_natives(void) {
  fprintf(stderr, "== %d natives registrados ==\n", g_natives_n);
  for (int i = 0; i < g_natives_n; i++)
    fprintf(stderr, "  %s#%s %s\n", g_natives[i].cls, g_natives[i].name, g_natives[i].sig);
}
static jint JNICALL j_UnregisterNatives(JNIEnv *e, jclass c) { (void)e; (void)c; return JNI_OK; }
static jint JNICALL j_MonitorEnter(JNIEnv *e, jobject o) { (void)e; (void)o; return JNI_OK; }
static jint JNICALL j_MonitorExit(JNIEnv *e, jobject o)  { (void)e; (void)o; return JNI_OK; }

/* GetObjectRefType - 0=Invalid, 1=Local, 2=Global, 3=Weak, 4=Indexed */
static jint JNICALL j_GetObjectRefType(JNIEnv *e, jobject o) { (void)e; (void)o; return 1; }

/* -------- slots que faltavam e derrubavam initJni em pc=0x0 -------- */
/* initJni do Unity 2019.4 comeca com env->GetJavaVM(&vm); slot NULL =
 * bl 0 -> SIGSEGV imediato (crash visto no device). */
static jint JNICALL j_GetJavaVM(JNIEnv *e, void **vm) {
  extern const void *shim_jni_vm_cell_ptr(void);
  if (vm) *vm = (void*)shim_jni_vm_cell_ptr();
  (void)e;
  return JNI_OK;
}

/* Strings UTF-16 minimais: NewString guarda buffer no aux, len no refs. */
static jstring JNICALL j_NewString(JNIEnv *e, const jchar *unicode, jsize len) {
  (void)e;
  struct _jobjectRef *r = ref_alloc(JT_STRING, "", NULL);
  jsize n = len > 0 ? len : 0;
  r->refs = n;
  if (n && unicode) {
    jchar *buf = malloc((size_t)n * sizeof(jchar));
    memcpy(buf, unicode, (size_t)n * sizeof(jchar));
    r->aux = buf;
  }
  return (jstring)r;
}
static const jchar *JNICALL j_GetStringChars(JNIEnv *e, jstring s, jboolean *ic) {
  (void)e; if (ic) *ic = JNI_FALSE;
  return (const jchar*)(s ? ((struct _jobjectRef*)s)->aux : NULL);
}
static void JNICALL j_ReleaseStringChars(JNIEnv *e, jstring s, const jchar *p) {
  (void)e; (void)s; (void)p;
}
static void JNICALL j_GetStringRegion(JNIEnv *e, jstring s, jsize st, jsize ln, jchar *dst) {
  (void)e;
  const jchar *src = s ? (const jchar*)((struct _jobjectRef*)s)->aux : NULL;
  if (src && dst && ln > 0) memcpy(dst, src + st, (size_t)ln * sizeof(jchar));
}
static void JNICALL j_GetStringUTFRegion(JNIEnv *e, jstring s, jsize st, jsize ln, char *dst) {
  (void)e;
  if (!s || !dst || ln <= 0) return;
  const char *utf = ((struct _jobjectRef*)s)->name;
  if (!utf) { memset(dst, 0, (size_t)ln); return; }
  size_t slen = strlen(utf);
  size_t n = (size_t)ln < slen - (size_t)st ? (size_t)ln : (slen > (size_t)st ? slen - (size_t)st : 0);
  memcpy(dst, utf + st, n);
  if (n < (size_t)ln) memset(dst + n, 0, (size_t)ln - n);
}
static const jchar *JNICALL j_GetStringCritical(JNIEnv *e, jstring s, jboolean *ic) {
  return j_GetStringChars(e, s, ic);
}
static void JNICALL j_ReleaseStringCritical(JNIEnv *e, jstring s, const jchar *p) {
  (void)e; (void)s; (void)p;
}

/* Weak refs: pool nunca libera -> fraco == forte. */
static jobjectRef JNICALL j_NewWeakGlobalRef(JNIEnv *e, jobject o) { (void)e; return (jobjectRef)o; }
static void JNICALL j_DeleteWeakGlobalRef(JNIEnv *e, jobjectRef r) { (void)e; (void)r; }

/* Reflection: passthrough de refs. */
static jmethodID JNICALL j_FromReflectedMethod(JNIEnv *e, jobject m) { (void)e; return (jmethodID)m; }
static jfieldID  JNICALL j_FromReflectedField(JNIEnv *e, jobject f)  { (void)e; return (jfieldID)f; }
static jobject JNICALL j_ToReflectedMethod(JNIEnv *e, jclass c, jmethodID m, jboolean st) {
  (void)e; (void)c; (void)st; return (jobject)m;
}
static jobject JNICALL j_ToReflectedField(JNIEnv *e, jclass c, jfieldID f, jboolean st) {
  (void)e; (void)c; (void)st; return (jobject)f;
}

/* -------- vtable estática -------- */
static const struct JNINativeInterface_ g_env_vtable = {
  .reserved0 = NULL, .reserved1 = NULL, .reserved2 = NULL, .reserved3 = NULL,
  .GetVersion = j_GetVersion,
  .FindClass = j_FindClass,
  .FromReflectedMethod = j_FromReflectedMethod,
  .FromReflectedField = j_FromReflectedField,
  .ToReflectedMethod = j_ToReflectedMethod,
  .GetSuperclass = j_GetSuperclass,
  .IsAssignableFrom = j_IsAssignableFrom,
  .ToReflectedField = j_ToReflectedField,
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
  .NewString = j_NewString,
  .GetStringLength = j_GetStringLength,
  .NewStringUTF = j_NewStringUTF,
  .GetStringUTFLength = j_GetStringUTFLength,
  .GetStringUTFChars = j_GetStringUTFChars,
  .ReleaseStringUTFChars = j_ReleaseStringUTFChars,
  .ReleaseStringChars = j_ReleaseStringChars,
  .GetStringChars = j_GetStringChars,
  .GetArrayLength = j_GetArrayLength,
  .NewObjectArray = j_NewObjectArray,
  .GetObjectArrayElement = j_GetObjectArrayElement,
  .SetObjectArrayElement = j_SetObjectArrayElement,
  .NewBooleanArray = j_NewBooleanArray,
  .NewByteArray = j_NewByteArray,
  .NewCharArray = j_NewCharArray,
  .NewShortArray = j_NewShortArray,
  .NewIntArray = j_NewIntArray,
  .NewLongArray = j_NewLongArray,
  .NewFloatArray = j_NewFloatArray,
  .NewDoubleArray = j_NewDoubleArray,
  .GetBooleanArrayElements = j_GetBooleanArrayElements,
  .GetByteArrayElements = j_GetByteArrayElements,
  .GetCharArrayElements = j_GetCharArrayElements,
  .GetShortArrayElements = j_GetShortArrayElements,
  .GetIntArrayElements = j_GetIntArrayElements,
  .GetLongArrayElements = j_GetLongArrayElements,
  .GetFloatArrayElements = j_GetFloatArrayElements,
  .GetDoubleArrayElements = j_GetDoubleArrayElements,
  .ReleaseBooleanArrayElements = j_ReleaseBooleanArrayElements,
  .ReleaseByteArrayElements = j_ReleaseByteArrayElements,
  .ReleaseCharArrayElements = j_ReleaseCharArrayElements,
  .ReleaseShortArrayElements = j_ReleaseShortArrayElements,
  .ReleaseIntArrayElements = j_ReleaseIntArrayElements,
  .ReleaseLongArrayElements = j_ReleaseLongArrayElements,
  .ReleaseFloatArrayElements = j_ReleaseFloatArrayElements,
  .ReleaseDoubleArrayElements = j_ReleaseDoubleArrayElements,
  .GetBooleanArrayRegion = j_GetBooleanArrayRegion,
  .GetByteArrayRegion = j_GetByteArrayRegion,
  .GetCharArrayRegion = j_GetCharArrayRegion,
  .GetShortArrayRegion = j_GetShortArrayRegion,
  .GetIntArrayRegion = j_GetIntArrayRegion,
  .GetLongArrayRegion = j_GetLongArrayRegion,
  .GetFloatArrayRegion = j_GetFloatArrayRegion,
  .GetDoubleArrayRegion = j_GetDoubleArrayRegion,
  .SetBooleanArrayRegion = j_SetBooleanArrayRegion,
  .SetByteArrayRegion = j_SetByteArrayRegion,
  .SetCharArrayRegion = j_SetCharArrayRegion,
  .SetShortArrayRegion = j_SetShortArrayRegion,
  .SetIntArrayRegion = j_SetIntArrayRegion,
  .SetLongArrayRegion = j_SetLongArrayRegion,
  .SetFloatArrayRegion = j_SetFloatArrayRegion,
  .SetDoubleArrayRegion = j_SetDoubleArrayRegion,
  .GetPrimitiveArrayCritical = j_GetPrimitiveArrayCritical,
  .ReleasePrimitiveArrayCritical = j_ReleasePrimitiveArrayCritical,
  .RegisterNatives = j_RegisterNatives,
  .UnregisterNatives = j_UnregisterNatives,
  .MonitorEnter = j_MonitorEnter, .MonitorExit = j_MonitorExit,
  .GetJavaVM = j_GetJavaVM,
  .GetStringRegion = j_GetStringRegion,
  .GetStringUTFRegion = j_GetStringUTFRegion,
  .GetStringCritical = j_GetStringCritical,
  .ReleaseStringCritical = j_ReleaseStringCritical,
  .NewWeakGlobalRef = j_NewWeakGlobalRef,
  .DeleteWeakGlobalRef = j_DeleteWeakGlobalRef,
  .ExceptionCheck = j_ExceptionCheck,
  .NewDirectByteBuffer = j_NewDirectByteBuffer,
  .GetDirectBufferAddress = j_GetDirectBufferAddress,
  .GetDirectBufferCapacity = j_GetDirectBufferCapacity,
  .GetObjectRefType = j_GetObjectRefType,
};

/* -------- JavaVM + TLS env -------- */
/* -------- celulas do ABI (env -> cell -> vtable) --------
 * O libunity.so compilado em C++ faz `env->functions->GetVersion(env)`, ou
 * seja le *(env) esperando o ENDERECO da vtable. Se entregarmos a propria
 * vtable (env = &g_env_vtable), ele le o 1o campo (reserved0 = NULL) e entao
 * deref [NULL+0x20] -> SIGSEGV (foi o crash em JNI_OnLoad+0x30!). Entregamos
 * SEMPRE &cell, onde cell guarda o ponteiro da tabela. */
static const struct JNINativeInterface_  *g_env_cell;
static const struct JNIInvokeInterface_  *g_vm_cell;
static void jni_ensure_cells(void);

static jint JNICALL vm_AttachCurrentThread(JavaVM *vm, void **penv, void *args) {
  (void)vm; (void)args;
  jni_ensure_cells();
  if (penv) *penv = (void *)&g_env_cell;   /* ponteiro para a CELULA */
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

static void jni_ensure_cells(void) {
  if (!g_env_cell) g_env_cell = &g_env_vtable;
  if (!g_vm_cell)  g_vm_cell  = &g_vm_vtable;
}

__attribute__((visibility("default")))
const void *shim_jni_vm_cell_ptr(void) {
  jni_ensure_cells();
  return &g_vm_cell;
}

__attribute__((visibility("default")))
JavaVM *shim_jni_get_vm(void) {
  jni_ensure_cells();
  return (JavaVM *)&g_vm_cell;   /* cell**, nao vtable* */
}

__attribute__((visibility("default")))
JNIEnv *shim_jni_new_env_for_thread(const char *name) {
  (void)name;
  jni_ensure_cells();
  return (JNIEnv *)&g_env_cell;   /* cell**, nao vtable* */
}

__attribute__((visibility("default")))
void shim_jni_install(void) {
  shim_jni_get_vm();
  /* Força um JNIEnv na thread main (Unity vai AttachCurrentThread nas outras) */
  shim_jni_new_env_for_thread("main");
}
