/* bridge_jni.h - JavaVM / JNIEnv minimais mas ABI-compatíveis com JNI 1.6.
 *
 * Layout das structs abaixo é extraído de android-ndk r21 / system/include
 * /jni/jni.h. Não inventamos ordem nem offsets: qualquer desvio faz o
 * libunity.so chamar o function-pointer errado e SIGSEGV em vtable+X.
 *
 * Só implementamos os métodos que Unity 2019.4 toca no boot e no runtime.
 * Os demais têm entrada na vtable mas apontam para jni_stub_fail (retorna
 * 0 / NULL e loga). Isto é suficiente pra fazer o processo sobreviver até
 * o Player.log reportar os "unsupported JNI calls" e você adicionar os
 * métodos que faltar conforme aparecerem.
 */
#ifndef BRIDGE_JNI_H
#define BRIDGE_JNI_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------- typedefs básicos (jni.h) -------- */
typedef unsigned char jboolean;
typedef int8_t   jbyte;
typedef uint16_t jchar;
typedef int16_t  jshort;
typedef int32_t  jint;
typedef int64_t  jlong;
// jfloat definido logo abaixo; placeholder removido
typedef float    jfloat;
typedef double   jdouble;
typedef jint     jsize;

/* Referências opacas - representadas como ponteiros p/ nossos próprios stubs */
typedef void *jobject;
typedef jobject jclass;
typedef jobject jstring;
typedef jobject jarray;
typedef jarray jobjectArray;
typedef jarray jbooleanArray;
typedef jarray jbyteArray;
typedef jarray jcharArray;
typedef jarray jshortArray;
typedef jarray jintArray;
typedef jarray jlongArray;
typedef jarray jfloatArray;
typedef jarray jdoubleArray;
typedef jarray jthrowable;

typedef struct _jmethodID  *jmethodID;
typedef struct _jfieldID   *jfieldID;
typedef struct _jobjectRef *jobjectRef;

enum { JNI_OK = 0, JNI_ERR = -1, JNI_EDETACHED = -2, JNI_EVERSION = -3,
       JNI_ENOMEM = -4, JNI_EEXIST = -5, JNI_EINVAL = -6 };
enum { JNI_VERSION_1_1 = 0x00010001, JNI_VERSION_1_2 = 0x00010002,
       JNI_VERSION_1_4 = 0x00010004, JNI_VERSION_1_6 = 0x00010006 };
enum { JNI_COMMIT = 1, JNI_ABORT = 2 };

#define JNI_TRUE  1
#define JNI_FALSE 0
#define JNICALL

/* -------- JNIEnv (JNINativeInterface_) --------
 * Ordem exata do jni.h oficial. reserved0..3 = null (4 slots). Depois
 * ~229 funções. Só declaramos as que implementamos + placeholders para
 * preservar offsets. */
/* ABI JNI padrao (art/libdvm + NDK): o que circula e um PONTEIRO PARA UMA
 * CELULA que contem o endereco da vtable. Em C++ _JNIEnv tem como 1o membro
 * `const JNINativeInterface_* functions`; em C usa-se `(*env)->X(env,...)`.
 * Ambos exigem: env -> cell -> vtable (duplo apontamento). */
typedef const struct JNINativeInterface_ *JNIEnv;
typedef const struct JNINativeInterface_ *JNIEnvPtr;

struct JNINativeInterface_ {
    void    *reserved0;
    void    *reserved1;
    void    *reserved2;
    void    *reserved3;

    jint      (JNICALL *GetVersion)(JNIEnv *e);

    jclass    (JNICALL *DefineClass)(JNIEnv *e, const char *name, jobject loader,
                                     const jbyte *buf, jsize len);
    jclass    (JNICALL *FindClass)(JNIEnv *e, const char *name);

    jmethodID (JNICALL *FromReflectedMethod)(JNIEnv *e, jobject m);
    jfieldID  (JNICALL *FromReflectedField)(JNIEnv *e, jobject f);
    jobject   (JNICALL *ToReflectedMethod)(JNIEnv *e, jclass c, jmethodID m, jboolean isStatic);

    jclass    (JNICALL *GetSuperclass)(JNIEnv *e, jclass c);
    jboolean  (JNICALL *IsAssignableFrom)(JNIEnv *e, jclass s, jclass d);

    jobject   (JNICALL *ToReflectedField)(JNIEnv *e, jclass c, jfieldID f, jboolean isStatic);

    jint      (JNICALL *Throw)(JNIEnv *e, jthrowable obj);
    jint      (JNICALL *ThrowNew)(JNIEnv *e, jclass clazz, const char *msg);
    jthrowable(JNICALL *ExceptionOccurred)(JNIEnv *e);
    void      (JNICALL *ExceptionDescribe)(JNIEnv *e);
    void      (JNICALL *ExceptionClear)(JNIEnv *e);
    void      (JNICALL *FatalError)(JNIEnv *e, const char *msg);

    jint      (JNICALL *PushLocalFrame)(JNIEnv *e, jint capacity);
    jobject   (JNICALL *PopLocalFrame)(JNIEnv *e, jobject result);

    jobject   (JNICALL *NewGlobalRef)(JNIEnv *e, jobject l);
    void      (JNICALL *DeleteGlobalRef)(JNIEnv *e, jobject g);
    void      (JNICALL *DeleteLocalRef)(JNIEnv *e, jobject obj);
    jboolean  (JNICALL *IsSameObject)(JNIEnv *e, jobject a, jobject b);
    jobject   (JNICALL *NewLocalRef)(JNIEnv *e, jobject ref);
    jint      (JNICALL *EnsureLocalCapacity)(JNIEnv *e, jint capacity);

    jobject   (JNICALL *AllocObject)(JNIEnv *e, jclass c);
    jobject   (JNICALL *NewObject)(JNIEnv *e, jclass c, jmethodID m, ...);
    jobject   (JNICALL *NewObjectV)(JNIEnv *e, jclass c, jmethodID m, va_list args);
    jobject   (JNICALL *NewObjectA)(JNIEnv *e, jclass c, jmethodID m, const void *args);

    jclass    (JNICALL *GetObjectClass)(JNIEnv *e, jobject obj);
    jboolean  (JNICALL *IsInstanceOf)(JNIEnv *e, jobject obj, jclass c);

    jmethodID (JNICALL *GetMethodID)(JNIEnv *e, jclass c, const char *name, const char *sig);

    /* Virtual methods (CallObjectMethod..CallVoidMethodA) - 4 slots cada
     * x 9 tipos = 36. Declaramos só os ponteiros, ordem importa. */
    jobject   (JNICALL *CallObjectMethod)(JNIEnv*, jobject, jmethodID, ...);
    jobject   (JNICALL *CallObjectMethodV)(JNIEnv*, jobject, jmethodID, va_list);
    jobject   (JNICALL *CallObjectMethodA)(JNIEnv*, jobject, jmethodID, void*);
    jboolean  (JNICALL *CallBooleanMethod)(JNIEnv*, jobject, jmethodID, ...);
    jboolean  (JNICALL *CallBooleanMethodV)(JNIEnv*, jobject, jmethodID, va_list);
    jboolean  (JNICALL *CallBooleanMethodA)(JNIEnv*, jobject, jmethodID, void*);
    jbyte     (JNICALL *CallByteMethod)(JNIEnv*, jobject, jmethodID, ...);
    jbyte     (JNICALL *CallByteMethodV)(JNIEnv*, jobject, jmethodID, va_list);
    jbyte     (JNICALL *CallByteMethodA)(JNIEnv*, jobject, jmethodID, void*);
    jchar     (JNICALL *CallCharMethod)(JNIEnv*, jobject, jmethodID, ...);
    jchar     (JNICALL *CallCharMethodV)(JNIEnv*, jobject, jmethodID, va_list);
    jchar     (JNICALL *CallCharMethodA)(JNIEnv*, jobject, jmethodID, void*);
    jshort    (JNICALL *CallShortMethod)(JNIEnv*, jobject, jmethodID, ...);
    jshort    (JNICALL *CallShortMethodV)(JNIEnv*, jobject, jmethodID, va_list);
    jshort    (JNICALL *CallShortMethodA)(JNIEnv*, jobject, jmethodID, void*);
    jint      (JNICALL *CallIntMethod)(JNIEnv*, jobject, jmethodID, ...);
    jint      (JNICALL *CallIntMethodV)(JNIEnv*, jobject, jmethodID, va_list);
    jint      (JNICALL *CallIntMethodA)(JNIEnv*, jobject, jmethodID, void*);
    jlong     (JNICALL *CallLongMethod)(JNIEnv*, jobject, jmethodID, ...);
    jlong     (JNICALL *CallLongMethodV)(JNIEnv*, jobject, jmethodID, va_list);
    jlong     (JNICALL *CallLongMethodA)(JNIEnv*, jobject, jmethodID, void*);
    jfloat    (JNICALL *CallFloatMethod)(JNIEnv*, jobject, jmethodID, ...);
    jfloat    (JNICALL *CallFloatMethodV)(JNIEnv*, jobject, jmethodID, va_list);
    jfloat    (JNICALL *CallFloatMethodA)(JNIEnv*, jobject, jmethodID, void*);
    jdouble   (JNICALL *CallDoubleMethod)(JNIEnv*, jobject, jmethodID, ...);
    jdouble   (JNICALL *CallDoubleMethodV)(JNIEnv*, jobject, jmethodID, va_list);
    jdouble   (JNICALL *CallDoubleMethodA)(JNIEnv*, jobject, jmethodID, void*);
    void      (JNICALL *CallVoidMethod)(JNIEnv*, jobject, jmethodID, ...);
    void      (JNICALL *CallVoidMethodV)(JNIEnv*, jobject, jmethodID, va_list);
    void      (JNICALL *CallVoidMethodA)(JNIEnv*, jobject, jmethodID, void*);

    /* Nonvirtual + Static variants (mesma ordem de tipos) */
    jobject   (JNICALL *CallNonvirtualObjectMethod)(JNIEnv*, jobject, jclass, jmethodID, ...);
    jobject   (JNICALL *CallNonvirtualObjectMethodV)(JNIEnv*, jobject, jclass, jmethodID, va_list);
    jobject   (JNICALL *CallNonvirtualObjectMethodA)(JNIEnv*, jobject, jclass, jmethodID, void*);
    jboolean  (JNICALL *CallNonvirtualBooleanMethod)(JNIEnv*, jobject, jclass, jmethodID, ...);
    jboolean  (JNICALL *CallNonvirtualBooleanMethodV)(JNIEnv*, jobject, jclass, jmethodID, va_list);
    jboolean  (JNICALL *CallNonvirtualBooleanMethodA)(JNIEnv*, jobject, jclass, jmethodID, void*);
    jbyte     (JNICALL *CallNonvirtualByteMethod)(JNIEnv*, jobject, jclass, jmethodID, ...);
    jbyte     (JNICALL *CallNonvirtualByteMethodV)(JNIEnv*, jobject, jclass, jmethodID, va_list);
    jbyte     (JNICALL *CallNonvirtualByteMethodA)(JNIEnv*, jobject, jclass, jmethodID, void*);
    jchar     (JNICALL *CallNonvirtualCharMethod)(JNIEnv*, jobject, jclass, jmethodID, ...);
    jchar     (JNICALL *CallNonvirtualCharMethodV)(JNIEnv*, jobject, jclass, jmethodID, va_list);
    jchar     (JNICALL *CallNonvirtualCharMethodA)(JNIEnv*, jobject, jclass, jmethodID, void*);
    jshort    (JNICALL *CallNonvirtualShortMethod)(JNIEnv*, jobject, jclass, jmethodID, ...);
    jshort    (JNICALL *CallNonvirtualShortMethodV)(JNIEnv*, jobject, jclass, jmethodID, va_list);
    jshort    (JNICALL *CallNonvirtualShortMethodA)(JNIEnv*, jobject, jclass, jmethodID, void*);
    jint      (JNICALL *CallNonvirtualIntMethod)(JNIEnv*, jobject, jclass, jmethodID, ...);
    jint      (JNICALL *CallNonvirtualIntMethodV)(JNIEnv*, jobject, jclass, jmethodID, va_list);
    jint      (JNICALL *CallNonvirtualIntMethodA)(JNIEnv*, jobject, jclass, jmethodID, void*);
    jlong     (JNICALL *CallNonvirtualLongMethod)(JNIEnv*, jobject, jclass, jmethodID, ...);
    jlong     (JNICALL *CallNonvirtualLongMethodV)(JNIEnv*, jobject, jclass, jmethodID, va_list);
    jlong     (JNICALL *CallNonvirtualLongMethodA)(JNIEnv*, jobject, jclass, jmethodID, void*);
    jfloat    (JNICALL *CallNonvirtualFloatMethod)(JNIEnv*, jobject, jclass, jmethodID, ...);
    jfloat    (JNICALL *CallNonvirtualFloatMethodV)(JNIEnv*, jobject, jclass, jmethodID, va_list);
    jfloat    (JNICALL *CallNonvirtualFloatMethodA)(JNIEnv*, jobject, jclass, jmethodID, void*);
    jdouble   (JNICALL *CallNonvirtualDoubleMethod)(JNIEnv*, jobject, jclass, jmethodID, ...);
    jdouble   (JNICALL *CallNonvirtualDoubleMethodV)(JNIEnv*, jobject, jclass, jmethodID, va_list);
    jdouble   (JNICALL *CallNonvirtualDoubleMethodA)(JNIEnv*, jobject, jclass, jmethodID, void*);
    void      (JNICALL *CallNonvirtualVoidMethod)(JNIEnv*, jobject, jclass, jmethodID, ...);
    void      (JNICALL *CallNonvirtualVoidMethodV)(JNIEnv*, jobject, jclass, jmethodID, va_list);
    void      (JNICALL *CallNonvirtualVoidMethodA)(JNIEnv*, jobject, jclass, jmethodID, void*);

    jfieldID  (JNICALL *GetFieldID)(JNIEnv*, jclass, const char*, const char*);
    jobject   (JNICALL *GetObjectField)(JNIEnv*, jobject, jfieldID);
    jboolean  (JNICALL *GetBooleanField)(JNIEnv*, jobject, jfieldID);
    jbyte     (JNICALL *GetByteField)(JNIEnv*, jobject, jfieldID);
    jchar     (JNICALL *GetCharField)(JNIEnv*, jobject, jfieldID);
    jshort    (JNICALL *GetShortField)(JNIEnv*, jobject, jfieldID);
    jint      (JNICALL *GetIntField)(JNIEnv*, jobject, jfieldID);
    jlong     (JNICALL *GetLongField)(JNIEnv*, jobject, jfieldID);
    jfloat    (JNICALL *GetFloatField)(JNIEnv*, jobject, jfieldID);
    jdouble   (JNICALL *GetDoubleField)(JNIEnv*, jobject, jfieldID);
    void      (JNICALL *SetObjectField)(JNIEnv*, jobject, jfieldID, jobject);
    void      (JNICALL *SetBooleanField)(JNIEnv*, jobject, jfieldID, jboolean);
    void      (JNICALL *SetByteField)(JNIEnv*, jobject, jfieldID, jbyte);
    void      (JNICALL *SetCharField)(JNIEnv*, jobject, jfieldID, jchar);
    void      (JNICALL *SetShortField)(JNIEnv*, jobject, jfieldID, jshort);
    void      (JNICALL *SetIntField)(JNIEnv*, jobject, jfieldID, jint);
    void      (JNICALL *SetLongField)(JNIEnv*, jobject, jfieldID, jlong);
    void      (JNICALL *SetFloatField)(JNIEnv*, jobject, jfieldID, jfloat);
    void      (JNICALL *SetDoubleField)(JNIEnv*, jobject, jfieldID, jdouble);

    jmethodID (JNICALL *GetStaticMethodID)(JNIEnv*, jclass, const char*, const char*);
    jobject   (JNICALL *CallStaticObjectMethod)(JNIEnv*, jclass, jmethodID, ...);
    jobject   (JNICALL *CallStaticObjectMethodV)(JNIEnv*, jclass, jmethodID, va_list);
    jobject   (JNICALL *CallStaticObjectMethodA)(JNIEnv*, jclass, jmethodID, void*);
    jboolean  (JNICALL *CallStaticBooleanMethod)(JNIEnv*, jclass, jmethodID, ...);
    jboolean  (JNICALL *CallStaticBooleanMethodV)(JNIEnv*, jclass, jmethodID, va_list);
    jboolean  (JNICALL *CallStaticBooleanMethodA)(JNIEnv*, jclass, jmethodID, void*);
    jbyte     (JNICALL *CallStaticByteMethod)(JNIEnv*, jclass, jmethodID, ...);
    jbyte     (JNICALL *CallStaticByteMethodV)(JNIEnv*, jclass, jmethodID, va_list);
    jbyte     (JNICALL *CallStaticByteMethodA)(JNIEnv*, jclass, jmethodID, void*);
    jchar     (JNICALL *CallStaticCharMethod)(JNIEnv*, jclass, jmethodID, ...);
    jchar     (JNICALL *CallStaticCharMethodV)(JNIEnv*, jclass, jmethodID, va_list);
    jchar     (JNICALL *CallStaticCharMethodA)(JNIEnv*, jclass, jmethodID, void*);
    jshort    (JNICALL *CallStaticShortMethod)(JNIEnv*, jclass, jmethodID, ...);
    jshort    (JNICALL *CallStaticShortMethodV)(JNIEnv*, jclass, jmethodID, va_list);
    jshort    (JNICALL *CallStaticShortMethodA)(JNIEnv*, jclass, jmethodID, void*);
    jint      (JNICALL *CallStaticIntMethod)(JNIEnv*, jclass, jmethodID, ...);
    jint      (JNICALL *CallStaticIntMethodV)(JNIEnv*, jclass, jmethodID, va_list);
    jint      (JNICALL *CallStaticIntMethodA)(JNIEnv*, jclass, jmethodID, void*);
    jlong     (JNICALL *CallStaticLongMethod)(JNIEnv*, jclass, jmethodID, ...);
    jlong     (JNICALL *CallStaticLongMethodV)(JNIEnv*, jclass, jmethodID, va_list);
    jlong     (JNICALL *CallStaticLongMethodA)(JNIEnv*, jclass, jmethodID, void*);
    jfloat    (JNICALL *CallStaticFloatMethod)(JNIEnv*, jclass, jmethodID, ...);
    jfloat    (JNICALL *CallStaticFloatMethodV)(JNIEnv*, jclass, jmethodID, va_list);
    jfloat    (JNICALL *CallStaticFloatMethodA)(JNIEnv*, jclass, jmethodID, void*);
    jdouble   (JNICALL *CallStaticDoubleMethod)(JNIEnv*, jclass, jmethodID, ...);
    jdouble   (JNICALL *CallStaticDoubleMethodV)(JNIEnv*, jclass, jmethodID, va_list);
    jdouble   (JNICALL *CallStaticDoubleMethodA)(JNIEnv*, jclass, jmethodID, void*);
    void      (JNICALL *CallStaticVoidMethod)(JNIEnv*, jclass, jmethodID, ...);
    void      (JNICALL *CallStaticVoidMethodV)(JNIEnv*, jclass, jmethodID, va_list);
    void      (JNICALL *CallStaticVoidMethodA)(JNIEnv*, jclass, jmethodID, void*);

    jfieldID  (JNICALL *GetStaticFieldID)(JNIEnv*, jclass, const char*, const char*);
    jobject   (JNICALL *GetStaticObjectField)(JNIEnv*, jclass, jfieldID);
    jboolean  (JNICALL *GetStaticBooleanField)(JNIEnv*, jclass, jfieldID);
    jbyte     (JNICALL *GetStaticByteField)(JNIEnv*, jclass, jfieldID);
    jchar     (JNICALL *GetStaticCharField)(JNIEnv*, jclass, jfieldID);
    jshort    (JNICALL *GetStaticShortField)(JNIEnv*, jclass, jfieldID);
    jint      (JNICALL *GetStaticIntField)(JNIEnv*, jclass, jfieldID);
    jlong     (JNICALL *GetStaticLongField)(JNIEnv*, jclass, jfieldID);
    jfloat    (JNICALL *GetStaticFloatField)(JNIEnv*, jclass, jfieldID);
    jdouble   (JNICALL *GetStaticDoubleField)(JNIEnv*, jclass, jfieldID);
    void      (JNICALL *SetStaticObjectField)(JNIEnv*, jclass, jfieldID, jobject);
    void      (JNICALL *SetStaticBooleanField)(JNIEnv*, jclass, jfieldID, jboolean);
    void      (JNICALL *SetStaticByteField)(JNIEnv*, jclass, jfieldID, jbyte);
    void      (JNICALL *SetStaticCharField)(JNIEnv*, jclass, jfieldID, jchar);
    void      (JNICALL *SetStaticShortField)(JNIEnv*, jclass, jfieldID, jshort);
    void      (JNICALL *SetStaticIntField)(JNIEnv*, jclass, jfieldID, jint);
    void      (JNICALL *SetStaticLongField)(JNIEnv*, jclass, jfieldID, jlong);
    void      (JNICALL *SetStaticFloatField)(JNIEnv*, jclass, jfieldID, jfloat);
    void      (JNICALL *SetStaticDoubleField)(JNIEnv*, jclass, jfieldID, jdouble);

    jstring   (JNICALL *NewString)(JNIEnv*, const jchar*, jsize);
    jsize     (JNICALL *GetStringLength)(JNIEnv*, jstring);
    const jchar*(JNICALL *GetStringChars)(JNIEnv*, jstring, jboolean*);
    void      (JNICALL *ReleaseStringChars)(JNIEnv*, jstring, const jchar*);
    jstring   (JNICALL *NewStringUTF)(JNIEnv*, const char*);
    jsize     (JNICALL *GetStringUTFLength)(JNIEnv*, jstring);
    const char*(JNICALL *GetStringUTFChars)(JNIEnv*, jstring, jboolean*);
    void      (JNICALL *ReleaseStringUTFChars)(JNIEnv*, jstring, const char*);

    jsize     (JNICALL *GetArrayLength)(JNIEnv*, jarray);
    jobjectArray(JNICALL *NewObjectArray)(JNIEnv*, jsize, jclass, jobject);
    jobject   (JNICALL *GetObjectArrayElement)(JNIEnv*, jobjectArray, jsize);
    void      (JNICALL *SetObjectArrayElement)(JNIEnv*, jobjectArray, jsize, jobject);

    /* primitivos arrays 8 tipos x 4 slots cada = 32 */
    jbooleanArray (JNICALL *NewBooleanArray)(JNIEnv*, jsize);
    jbyteArray    (JNICALL *NewByteArray)(JNIEnv*, jsize);
    jcharArray    (JNICALL *NewCharArray)(JNIEnv*, jsize);
    jshortArray   (JNICALL *NewShortArray)(JNIEnv*, jsize);
    jintArray     (JNICALL *NewIntArray)(JNIEnv*, jsize);
    jlongArray    (JNICALL *NewLongArray)(JNIEnv*, jsize);
    jfloatArray   (JNICALL *NewFloatArray)(JNIEnv*, jsize);
    jdoubleArray  (JNICALL *NewDoubleArray)(JNIEnv*, jsize);
    jboolean* (JNICALL *GetBooleanArrayElements)(JNIEnv*, jbooleanArray, jboolean*);
    jbyte*    (JNICALL *GetByteArrayElements)(JNIEnv*, jbyteArray, jboolean*);
    jchar*    (JNICALL *GetCharArrayElements)(JNIEnv*, jcharArray, jboolean*);
    jshort*   (JNICALL *GetShortArrayElements)(JNIEnv*, jshortArray, jboolean*);
    jint*     (JNICALL *GetIntArrayElements)(JNIEnv*, jintArray, jboolean*);
    jlong*    (JNICALL *GetLongArrayElements)(JNIEnv*, jlongArray, jboolean*);
    jfloat*   (JNICALL *GetFloatArrayElements)(JNIEnv*, jfloatArray, jboolean*);
    jdouble*  (JNICALL *GetDoubleArrayElements)(JNIEnv*, jdoubleArray, jboolean*);
    void (JNICALL *ReleaseBooleanArrayElements)(JNIEnv*, jbooleanArray, jboolean*, jint);
    void (JNICALL *ReleaseByteArrayElements)(JNIEnv*, jbyteArray, jbyte*, jint);
    void (JNICALL *ReleaseCharArrayElements)(JNIEnv*, jcharArray, jchar*, jint);
    void (JNICALL *ReleaseShortArrayElements)(JNIEnv*, jshortArray, jshort*, jint);
    void (JNICALL *ReleaseIntArrayElements)(JNIEnv*, jintArray, jint*, jint);
    void (JNICALL *ReleaseLongArrayElements)(JNIEnv*, jlongArray, jlong*, jint);
    void (JNICALL *ReleaseFloatArrayElements)(JNIEnv*, jfloatArray, jfloat*, jint);
    void (JNICALL *ReleaseDoubleArrayElements)(JNIEnv*, jdoubleArray, jdouble*, jint);
    void (JNICALL *GetBooleanArrayRegion)(JNIEnv*, jbooleanArray, jsize, jsize, jboolean*);
    void (JNICALL *GetByteArrayRegion)(JNIEnv*, jbyteArray, jsize, jsize, jbyte*);
    void (JNICALL *GetCharArrayRegion)(JNIEnv*, jcharArray, jsize, jsize, jchar*);
    void (JNICALL *GetShortArrayRegion)(JNIEnv*, jshortArray, jsize, jsize, jshort*);
    void (JNICALL *GetIntArrayRegion)(JNIEnv*, jintArray, jsize, jsize, jint*);
    void (JNICALL *GetLongArrayRegion)(JNIEnv*, jlongArray, jsize, jsize, jlong*);
    void (JNICALL *GetFloatArrayRegion)(JNIEnv*, jfloatArray, jsize, jsize, jfloat*);
    void (JNICALL *GetDoubleArrayRegion)(JNIEnv*, jdoubleArray, jsize, jsize, jdouble*);
    jint (JNICALL *SetBooleanArrayRegion)(JNIEnv*, jbooleanArray, jsize, jsize, const jboolean*);
    jint (JNICALL *SetByteArrayRegion)(JNIEnv*, jbyteArray, jsize, jsize, const jbyte*);
    jint (JNICALL *SetCharArrayRegion)(JNIEnv*, jcharArray, jsize, jsize, const jchar*);
    jint (JNICALL *SetShortArrayRegion)(JNIEnv*, jshortArray, jsize, jsize, const jshort*);
    jint (JNICALL *SetIntArrayRegion)(JNIEnv*, jintArray, jsize, jsize, const jint*);
    jint (JNICALL *SetLongArrayRegion)(JNIEnv*, jlongArray, jsize, jsize, const jlong*);
    jint (JNICALL *SetFloatArrayRegion)(JNIEnv*, jfloatArray, jsize, jsize, const jfloat*);
    jint (JNICALL *SetDoubleArrayRegion)(JNIEnv*, jdoubleArray, jsize, jsize, const jdouble*);

    jint (JNICALL *RegisterNatives)(JNIEnv*, jclass, const void*, jint);
    jint (JNICALL *UnregisterNatives)(JNIEnv*, jclass);
    jint (JNICALL *MonitorEnter)(JNIEnv*, jobject);
    jint (JNICALL *MonitorExit)(JNIEnv*, jobject);
    jint (JNICALL *GetJavaVM)(JNIEnv*, void **vm);

    void (JNICALL *GetStringRegion)(JNIEnv*, jstring, jsize, jsize, jchar*);
    void (JNICALL *GetStringUTFRegion)(JNIEnv*, jstring, jsize, jsize, char*);
    void*(JNICALL *GetPrimitiveArrayCritical)(JNIEnv*, jarray, jboolean*);
    void (JNICALL *ReleasePrimitiveArrayCritical)(JNIEnv*, jarray, void*, jint);
    const jchar*(JNICALL *GetStringCritical)(JNIEnv*, jstring, jboolean*);
    void (JNICALL *ReleaseStringCritical)(JNIEnv*, jstring, const jchar*);

    jobjectRef (JNICALL *NewWeakGlobalRef)(JNIEnv*, jobject);
    void       (JNICALL *DeleteWeakGlobalRef)(JNIEnv*, jobjectRef);
    jboolean   (JNICALL *ExceptionCheck)(JNIEnv*);

    jobject   (JNICALL *NewDirectByteBuffer)(JNIEnv*, void*, jlong);
    void*     (JNICALL *GetDirectBufferAddress)(JNIEnv*, jobject);
    jlong     (JNICALL *GetDirectBufferCapacity)(JNIEnv*, jobject);

    /* JNI 1.6 - GetObjectRefType etc. */
    jint      (JNICALL *GetObjectRefType)(JNIEnv*, jobject);
};

/* -------- JavaVM (JNIInvokeInterface_) -------- */
typedef const struct JNIInvokeInterface_ *JavaVM;
typedef const struct JNIInvokeInterface_ *JavaVMPtr;

struct JNIInvokeInterface_ {
    void *reserved0;
    void *reserved1;
    void *reserved2;
    jint (JNICALL *DestroyJavaVM)(JavaVM *vm);
    jint (JNICALL *AttachCurrentThread)(JavaVM *vm, void **penv, void *args);
    jint (JNICALL *DetachCurrentThread)(JavaVM *vm);
    jint (JNICALL *GetEnv)(JavaVM *vm, void **penv, jint version);
    jint (JNICALL *AttachCurrentThreadAsDaemon)(JavaVM *vm, void **penv, void *args);
};

/* API pública do nosso shim: */
JavaVM *shim_jni_get_vm(void);
JNIEnv *shim_jni_new_env_for_thread(const char *thread_name);
void    shim_jni_install(void);          /* setenv ULTRAKILL_FAKE_JVM=1 etc */

/* =====================================================================
 * OBJETOS DE EVENTO VIRTUAIS (android.view.InputEvent)
 * =====================================================================
 * EVIDENCIA (tools/dumpelf.py + log do device): o libunity.so deste build
 * NAO importa nenhum AInputQueue_* - ele recebe input SOMENTE pelo native
 * registrado `nativeInjectEvent (Landroid/view/InputEvent;)Z`. O caminho e:
 * nos criamos um objeto Java FALSO de android/view/MotionEvent (ou
 * KeyEvent), entregamos o jobject ao nativeInjectEvent, e a mini-JVM
 * responde os Call*Method que o C++ do Unity faz em cima dele
 * (getAction/getX/getPointerCount/...).
 *
 * O envelope (jobject) e PERMANENTE: o chamador mutua o struct
 * shim_jni_event e chama nativeInjectEvent de novo. Nada de alocar por
 * evento (o pool do shim nunca libera). */
#define SHIM_JNI_MAX_POINTERS 8
#define SHIM_JNI_MAX_AXES     48

typedef struct shim_jni_pointer {
  int32_t id;
  float   x, y;
  float   pressure, size;
  int32_t tool_type;
} shim_jni_pointer;

typedef struct shim_jni_event {
  int      is_key;               /* 1 = KeyEvent, 0 = MotionEvent */
  int32_t  action;               /* AKEY_EVENT_ACTION_* / AMOTION_EVENT_ACTION_*
                                  * (com o index do ponteiro ja deslocado <<8,
                                  * exatamente como o Android entrega) */
  int64_t  down_ms, event_ms;
  int32_t  device_id, source, flags, meta_state;
  int32_t  key_code, key_scan, key_repeat;    /* KeyEvent */
  int      n_ptrs;
  shim_jni_pointer ptr[SHIM_JNI_MAX_POINTERS];
  float    axis[SHIM_JNI_MAX_AXES];           /* por AMOTION_EVENT_AXIS_* */
} shim_jni_event;

/* Cria (uma vez) um jobject que e uma visao do 'ev'. NAO libere. */
jobject shim_jni_make_event(shim_jni_event *ev, int is_key);
/* Devolve o struct associado, ou NULL se 'o' nao e um evento nosso. */
shim_jni_event *shim_jni_event_data(jobject o);
/* 1 = sim, o objeto e um dos nossos eventos. */
int shim_jni_is_event(jobject o);

#ifdef __cplusplus
}
#endif

#endif /* BRIDGE_JNI_H */
