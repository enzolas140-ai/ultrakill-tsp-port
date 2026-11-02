/* mediandk_stub.c - AMediaExtractor / AMediaCodec no-op.
 *
 * Usado pelo Unity VideoPlayer pra tocar as cutscenes .webm. Em UK-
 * Death_MARK a intro "IMMORTAL" e em V2_HOME o video do parapeito.
 * Com stub completo, o VideoPlayer reporta erro e Unity cai pra
 * "skip video". Aceitavel para smoke-test; video real = linkar
 * libavformat (fora do escopo deste skeleton). */
#include <android_shim.h>
#include <stdlib.h>

__attribute__((visibility("default")))
AMediaExtractor *AMediaExtractor_new(void) { return calloc(1, 16); }

__attribute__((visibility("default")))
void AMediaExtractor_delete(AMediaExtractor *e) { free(e); }

__attribute__((visibility("default")))
media_status_code AMediaExtractor_setDataSourceFd(AMediaExtractor *e, int fd,
                                                   off_t off, size_t len) {
  (void)e; (void)fd; (void)off; (void)len;
  return MEDIA_ERROR_UNKNOWN;
}

__attribute__((visibility("default")))
media_status_code AMediaExtractor_setDataSource(AMediaExtractor *e, const char *uri) {
  (void)e; (void)uri; return MEDIA_ERROR_UNKNOWN;
}

__attribute__((visibility("default")))
int32_t AMediaExtractor_getTrackCount(AMediaExtractor *e) { (void)e; return 0; }

__attribute__((visibility("default")))
AMediaFormat *AMediaExtractor_getTrackFormat(AMediaExtractor *e, uint32_t i) {
  (void)e; (void)i; return NULL;
}

__attribute__((visibility("default")))
AMediaFormat *AMediaFormat_new(void) { return calloc(1, 16); }
__attribute__((visibility("default")))
void AMediaFormat_delete(AMediaFormat *f) { free(f); }

__attribute__((visibility("default")))
AMediaCodec *AMediaCodec_createDecoderByType(const char *mime) {
  (void)mime; return calloc(1, 16);
}
__attribute__((visibility("default")))
void AMediaCodec_delete(AMediaCodec *c) { free(c); }
__attribute__((visibility("default")))
media_status_code AMediaCodec_configure(AMediaCodec *c, const AMediaFormat *f,
                                        void *surface, void *crypto, uint32_t flags) {
  (void)c; (void)f; (void)surface; (void)crypto; (void)flags;
  return MEDIA_ERROR_UNKNOWN;
}
__attribute__((visibility("default")))
media_status_code AMediaCodec_start(AMediaCodec *c) { (void)c; return MEDIA_ERROR_UNKNOWN; }
__attribute__((visibility("default")))
media_status_code AMediaCodec_stop(AMediaCodec *c)  { (void)c; return MEDIA_OK; }
__attribute__((visibility("default")))
media_status_code AMediaCodec_flush(AMediaCodec *c) { (void)c; return MEDIA_OK; }
