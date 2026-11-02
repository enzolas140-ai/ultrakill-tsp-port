/* android_asset_stub.c - AAssetManager que le a filesystem.
 *
 * O Unity 2019.4 chama AAssetManager_open(mgr, "bin/Data/Managed/global-metadata.dat",
 * AASSET_MODE_BUFFER) etc. No Android real o mgr vem do APK (JNI). Aqui
 * nós apontamos para o diretorio $ULTRAKILL_ASSET_ROOT (default: gamedata/).
 *
 * Limitacao: nao suportamos arquivos comprimidos com offset dentro do APK
 * (o que no Android e normal - ZIP deflate + aligned). Assumimos que os
 * arquivos ja foram extraidos por extract_payload.py para o disco. */
#include <android_shim.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct AAssetManager {
  char base[4096];
};

struct AAsset {
  FILE   *fp;
  char   *path;
  void   *buffer;         /* cache de AAsset_getBuffer */
  size_t  buflen;
  off_t   len;
};

static const char *resolve_base(void) {
  const char *env = getenv("ULTRAKILL_ASSET_ROOT");
  if (env && *env) return env;
  return ".";
}

__attribute__((visibility("default")))
AAssetManager *shim_AAssetManager_fromPath(const char *basepath) {
  AAssetManager *m = calloc(1, sizeof(*m));
  if (!m) return NULL;
  snprintf(m->base, sizeof(m->base), "%s", basepath ? basepath : resolve_base());
  return m;
}

/* Unity 2019.4 em libunity.so também chama AAssetManager_fromJava(JNIEnv*, jobject).
 * Sem JVM, sempre NULL — mas como nosso bridge ja injeta um AAssetManager
 * global via shim_AAssetManager_fromPath ANTES de UnityMain, o Unity nem
 * chega a chamar fromJava no nosso fluxo. */
__attribute__((visibility("default")))
AAssetManager *AAssetManager_fromJava(void *env, void *mgrObj) {
  (void)env; (void)mgrObj;
  static AAssetManager *singleton = NULL;
  if (!singleton) singleton = shim_AAssetManager_fromPath(resolve_base());
  return singleton;
}

static int within_base(const char *base, const char *joined) {
  /* impede path traversal "..": */
  return strncmp(joined, base, strlen(base)) == 0 &&
         strchr(joined + strlen(base), 0) != NULL &&
         strstr(joined, "/../") == NULL;
}

__attribute__((visibility("default")))
AAsset *AAssetManager_open(AAssetManager *mgr, const char *filename, int mode) {
  if (!mgr || !filename) return NULL;
  char path[4096];
  snprintf(path, sizeof(path), "%s/%s", mgr->base, filename);
  if (!within_base(mgr->base, path)) return NULL;

  FILE *fp = fopen(path, mode == AASSET_MODE_SEQUENTIAL ? "r" : "rb");
  if (!fp) return NULL;

  AAsset *a = calloc(1, sizeof(*a));
  a->fp = fp;
  a->path = strdup(path);
  struct stat st; fstat(fileno(fp), &st);
  a->len = st.st_size;
  return a;
}

__attribute__((visibility("default")))
off_t AAsset_getLength(AAsset *a) { return a ? a->len : -1; }

__attribute__((visibility="default"))
off_t AAsset_read(AAsset *a, void *buf, size_t count) {
  if (!a || !buf) return -1;
  size_t n = fread(buf, 1, count, a->fp);
  return (off_t)n;
}

__attribute__((visibility="default"))
off_t AAsset_seek(AAsset *a, off_t offset, int whence) {
  if (!a) return -1;
  return fseeko(a->fp, offset, whence);
}

__attribute__((visibility="default"))
off_t AAsset_seek64(AAsset *a, off_t offset, int whence) {
  return AAsset_seek(a, offset, whence);
}

__attribute__((visibility="default"))
const void *AAsset_getBuffer(AAsset *a) {
  if (!a) return NULL;
  if (!a->buffer) {
    off_t here = ftello(a->fp);
    fseeko(a->fp, 0, SEEK_SET);
    a->buffer = malloc((size_t)a->len);
    if (a->buffer) {
      a->buflen = fread(a->buffer, 1, (size_t)a->len, a->fp);
    }
    fseeko(a->fp, here, SEEK_SET);
  }
  return a->buffer;
}

__attribute__((visibility="default"))
void AAsset_close(AAsset *a) {
  if (!a) return;
  if (a->fp) fclose(a->fp);
  free(a->buffer);
  free(a->path);
  free(a);
}

__attribute__((visibility="default"))
int AAsset_getRemainingLength64(AAsset *a) {
  if (!a) return -1;
  off_t here = ftello(a->fp);
  return (int)(a->len - here);
}

__attribute__((visibility="default"))
void AAssetManager_delete(AAssetManager *m) { free(m); }
