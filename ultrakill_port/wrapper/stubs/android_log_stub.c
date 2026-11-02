/* android_log_stub.c - implementa liblog.so por cima de vfprintf(stderr).
 * O libunity.so do ULTRADROID.apk chama __android_log_print pra tudo que
 * e log de engine (shader compile errors, addressable load, etc). Sem
 * isto o jogo SIGSEGV no primeiro AL_LOG. */
#include <android_shim.h>
#include <stdio.h>
#include <string.h>

static const char *lvl(int prio) {
  switch (prio) {
    case ANDROID_LOG_VERBOSE: return "V";
    case ANDROID_LOG_DEBUG:   return "D";
    case ANDROID_LOG_INFO:    return "I";
    case ANDROID_LOG_WARN:    return "W";
    case ANDROID_LOG_ERROR:   return "E";
    case ANDROID_LOG_FATAL:   return "F";
    default:                  return "?";
  }
}

__attribute__((visibility("default")))
int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap) {
  int n = fprintf(stderr, "%s/%s: ", lvl(prio), tag ? tag : "-");
  n += vfprintf(stderr, fmt ? fmt : "", ap);
  fputc('\n', stderr);
  return n;
}

__attribute__((visibility("default")))
int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  int n = __android_log_vprint(prio, tag, fmt, ap);
  va_end(ap);
  return n;
}

__attribute__((visibility("default")))
int __android_log_write(int prio, const char *tag, const char *text) {
  return fprintf(stderr, "%s/%s: %s\n", lvl(prio), tag ? tag : "-",
                 text ? text : "");
}

__attribute__((visibility("default")))
void __android_log_assert(const char *cond, const char *tag, const char *fmt, ...) {
  if (cond && *cond) return;
  fprintf(stderr, "F/%s: ASSERT FAILED: %s\n", tag ? tag : "-",
          fmt ? fmt : "");
  if (fmt) {
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
  }
  fflush(stderr);
  abort();
}

/* NDK 21 tambem exporta isto (chamado por libmonoutils): */
__attribute__((visibility("default")))
int __android_log_close(void) { return 0; }

/* -------- __sF: Bionic stdio global FILE[3] {stdin,stdout,stderr} --------
 * libunity.so/libil2cpp.so referenciam o simbolo de DADOS `__sF` (array de
 * 3 FILE do Bionic). glibc nao exporta esse simbolo -> como e DATA, a
 * relocacao nao pode ser lazy: o dlopen falha com "undefined symbol: __sF"
 * mesmo com RTLD_LAZY. Definimos o array e preenchemos via construtor com
 * os FILE reais do glibc, entao fprintf(&__sF[1],..) escreve em stdout de
 * verdade. Indexacao Bionic: 0=stdin 1=stdout 2=stderr. */
__attribute__((visibility("default")))
FILE __sF[3];
__attribute__((constructor))
static void shim_init_sF(void) {
  if (stdin)  __sF[0] = *stdin;
  if (stdout) __sF[1] = *stdout;
  if (stderr) __sF[2] = *stderr;
}
