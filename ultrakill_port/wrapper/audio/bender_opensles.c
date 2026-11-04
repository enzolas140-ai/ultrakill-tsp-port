#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <dlfcn.h>
#include <unistd.h>

/* Minimal OpenSL ES 1.0.1 ABI required by Oboe output streams. */
typedef int8_t SLint8;
typedef uint8_t SLuint8;
typedef int16_t SLint16;
typedef uint16_t SLuint16;
typedef int32_t SLint32;
typedef uint32_t SLuint32;
typedef SLuint32 SLboolean;
typedef SLuint8 SLchar;
typedef SLint16 SLmillibel;
typedef SLuint32 SLmillisecond;
typedef SLuint32 SLresult;
typedef SLint16 SLpermille;

#define SL_BOOLEAN_FALSE 0u
#define SL_BOOLEAN_TRUE 1u
#define SL_RESULT_SUCCESS 0u
#define SL_RESULT_PRECONDITIONS_VIOLATED 1u
#define SL_RESULT_PARAMETER_INVALID 2u
#define SL_RESULT_MEMORY_FAILURE 3u
#define SL_RESULT_RESOURCE_ERROR 4u
#define SL_RESULT_FEATURE_UNSUPPORTED 12u
#define SL_RESULT_INTERNAL_ERROR 13u
#define SL_OBJECT_STATE_UNREALIZED 1u
#define SL_OBJECT_STATE_REALIZED 2u
#define SL_PLAYSTATE_STOPPED 1u
#define SL_PLAYSTATE_PAUSED 2u
#define SL_PLAYSTATE_PLAYING 3u
#define SL_TIME_UNKNOWN 0xFFFFFFFFu
#define SL_DATAFORMAT_PCM 2u
#define SL_BYTEORDER_LITTLEENDIAN 2u
#define SL_DATALOCATOR_OUTPUTMIX 4u
#define SL_DATALOCATOR_BUFFERQUEUE 6u
#define SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE 0x800007BDu

struct SLInterfaceID_ {
    SLuint32 time_low;
    SLuint16 time_mid;
    SLuint16 time_hi_and_version;
    SLuint16 clock_seq;
    SLuint8 node[6];
};
typedef const struct SLInterfaceID_ *SLInterfaceID;

struct SLObjectItf_;
struct SLEngineItf_;
struct SLPlayItf_;
struct SLAndroidSimpleBufferQueueItf_;
struct SLAndroidConfigurationItf_;
struct SLVolumeItf_;

typedef const struct SLObjectItf_ * const *SLObjectItf;
typedef const struct SLEngineItf_ * const *SLEngineItf;
typedef const struct SLPlayItf_ * const *SLPlayItf;
typedef const struct SLAndroidSimpleBufferQueueItf_ * const *SLAndroidSimpleBufferQueueItf;
typedef const struct SLAndroidConfigurationItf_ * const *SLAndroidConfigurationItf;
typedef const struct SLVolumeItf_ * const *SLVolumeItf;

typedef struct SLDataSource_ { void *pLocator; void *pFormat; } SLDataSource;
typedef struct SLDataSink_ { void *pLocator; void *pFormat; } SLDataSink;
typedef struct SLDataFormat_PCM_ {
    SLuint32 formatType;
    SLuint32 numChannels;
    SLuint32 samplesPerSec;
    SLuint32 bitsPerSample;
    SLuint32 containerSize;
    SLuint32 channelMask;
    SLuint32 endianness;
} SLDataFormat_PCM;

typedef void (*slObjectCallback)(SLObjectItf, const void *, SLuint32, SLresult, SLuint32, void *);
typedef void (*slPlayCallback)(SLPlayItf, void *, SLuint32);
typedef void (*slAndroidSimpleBufferQueueCallback)(SLAndroidSimpleBufferQueueItf, void *);

typedef struct SLAndroidSimpleBufferQueueState_ { SLuint32 count; SLuint32 index; } SLAndroidSimpleBufferQueueState;

struct SLObjectItf_ {
    SLresult (*Realize)(SLObjectItf, SLboolean);
    SLresult (*Resume)(SLObjectItf, SLboolean);
    SLresult (*GetState)(SLObjectItf, SLuint32 *);
    SLresult (*GetInterface)(SLObjectItf, const SLInterfaceID, void *);
    SLresult (*RegisterCallback)(SLObjectItf, slObjectCallback, void *);
    void (*AbortAsyncOperation)(SLObjectItf);
    void (*Destroy)(SLObjectItf);
    SLresult (*SetPriority)(SLObjectItf, SLint32, SLboolean);
    SLresult (*GetPriority)(SLObjectItf, SLint32 *, SLboolean *);
    SLresult (*SetLossOfControlInterfaces)(SLObjectItf, SLint16, SLInterfaceID *, SLboolean);
};

struct SLEngineItf_ {
    void *CreateLEDDevice;
    void *CreateVibraDevice;
    SLresult (*CreateAudioPlayer)(SLEngineItf, SLObjectItf *, SLDataSource *, SLDataSink *, SLuint32, const SLInterfaceID *, const SLboolean *);
    void *CreateAudioRecorder;
    void *CreateMidiPlayer;
    void *CreateListener;
    void *Create3DGroup;
    SLresult (*CreateOutputMix)(SLEngineItf, SLObjectItf *, SLuint32, const SLInterfaceID *, const SLboolean *);
    void *CreateMetadataExtractor;
    void *CreateExtensionObject;
    void *QueryNumSupportedInterfaces;
    void *QuerySupportedInterfaces;
    void *QueryNumSupportedExtensions;
    void *QuerySupportedExtension;
    void *IsExtensionSupported;
};

struct SLPlayItf_ {
    SLresult (*SetPlayState)(SLPlayItf, SLuint32);
    SLresult (*GetPlayState)(SLPlayItf, SLuint32 *);
    SLresult (*GetDuration)(SLPlayItf, SLmillisecond *);
    SLresult (*GetPosition)(SLPlayItf, SLmillisecond *);
    SLresult (*RegisterCallback)(SLPlayItf, slPlayCallback, void *);
    SLresult (*SetCallbackEventsMask)(SLPlayItf, SLuint32);
    SLresult (*GetCallbackEventsMask)(SLPlayItf, SLuint32 *);
    SLresult (*SetMarkerPosition)(SLPlayItf, SLmillisecond);
    SLresult (*ClearMarkerPosition)(SLPlayItf);
    SLresult (*GetMarkerPosition)(SLPlayItf, SLmillisecond *);
    SLresult (*SetPositionUpdatePeriod)(SLPlayItf, SLmillisecond);
    SLresult (*GetPositionUpdatePeriod)(SLPlayItf, SLmillisecond *);
};

struct SLAndroidSimpleBufferQueueItf_ {
    SLresult (*Enqueue)(SLAndroidSimpleBufferQueueItf, const void *, SLuint32);
    SLresult (*Clear)(SLAndroidSimpleBufferQueueItf);
    SLresult (*GetState)(SLAndroidSimpleBufferQueueItf, SLAndroidSimpleBufferQueueState *);
    SLresult (*RegisterCallback)(SLAndroidSimpleBufferQueueItf, slAndroidSimpleBufferQueueCallback, void *);
};

struct SLAndroidConfigurationItf_ {
    SLresult (*SetConfiguration)(SLAndroidConfigurationItf, const SLchar *, const void *, SLuint32);
    SLresult (*GetConfiguration)(SLAndroidConfigurationItf, const SLchar *, SLuint32 *, void *);
};

struct SLVolumeItf_ {
    SLresult (*SetVolumeLevel)(SLVolumeItf, SLmillibel);
    SLresult (*GetVolumeLevel)(SLVolumeItf, SLmillibel *);
    SLresult (*GetMaxVolumeLevel)(SLVolumeItf, SLmillibel *);
    SLresult (*SetMute)(SLVolumeItf, SLboolean);
    SLresult (*GetMute)(SLVolumeItf, SLboolean *);
    SLresult (*EnableStereoPosition)(SLVolumeItf, SLboolean);
    SLresult (*IsEnabledStereoPosition)(SLVolumeItf, SLboolean *);
    SLresult (*SetStereoPosition)(SLVolumeItf, SLpermille);
    SLresult (*GetStereoPosition)(SLVolumeItf, SLpermille *);
};

/* Khronos/Android interface UUIDs. */
static const struct SLInterfaceID_ iid_engine_data = {0x8d97c260u,0xddd4u,0x11dbu,0x958fu,{0x00,0x02,0xa5,0xd5,0xc5,0x1b}};
static const struct SLInterfaceID_ iid_bufferqueue_data = {0x2bc99cc0u,0xddd4u,0x11dbu,0x8d99u,{0x00,0x02,0xa5,0xd5,0xc5,0x1b}};
static const struct SLInterfaceID_ iid_play_data = {0xef0bd9c0u,0xddd7u,0x11dbu,0xbf49u,{0x00,0x02,0xa5,0xd5,0xc5,0x1b}};
static const struct SLInterfaceID_ iid_record_data = {0xc5657aa0u,0xdddbu,0x11dbu,0x82f7u,{0x00,0x02,0xa5,0xd5,0xc5,0x1b}};
static const struct SLInterfaceID_ iid_volume_data = {0x09e8ede0u,0xdddeu,0x11dbu,0xb4f6u,{0x00,0x02,0xa5,0xd5,0xc5,0x1b}};
static const struct SLInterfaceID_ iid_androidconfig_data = {0x89f6a7e0u,0xbeacu,0x11dfu,0x8b5cu,{0x00,0x02,0xa5,0xd5,0xc5,0x1b}};
static const struct SLInterfaceID_ iid_androidsimplebq_data = {0x198e4940u,0xc5d7u,0x11dfu,0xa2a6u,{0x00,0x02,0xa5,0xd5,0xc5,0x1b}};

__attribute__((visibility("default"))) const SLInterfaceID SL_IID_ENGINE = &iid_engine_data;
__attribute__((visibility("default"))) const SLInterfaceID SL_IID_BUFFERQUEUE = &iid_bufferqueue_data;
__attribute__((visibility("default"))) const SLInterfaceID SL_IID_PLAY = &iid_play_data;
__attribute__((visibility("default"))) const SLInterfaceID SL_IID_RECORD = &iid_record_data;
__attribute__((visibility("default"))) const SLInterfaceID SL_IID_VOLUME = &iid_volume_data;
__attribute__((visibility("default"))) const SLInterfaceID SL_IID_ANDROIDCONFIGURATION = &iid_androidconfig_data;
__attribute__((visibility("default"))) const SLInterfaceID SL_IID_ANDROIDSIMPLEBUFFERQUEUE = &iid_androidsimplebq_data;

/* Minimal SDL2 ABI, loaded dynamically from the CFW. */
typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef Uint16 SDL_AudioFormat;
typedef Uint32 SDL_AudioDeviceID;
typedef void (*SDL_AudioCallback)(void *, Uint8 *, int);
typedef struct SDL_AudioSpec {
    int freq;
    SDL_AudioFormat format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint16 padding;
    Uint32 size;
    SDL_AudioCallback callback;
    void *userdata;
} SDL_AudioSpec;

#define SDL_INIT_AUDIO 0x00000010u
#define AUDIO_S16LSB 0x8010u
#define AUDIO_F32LSB 0x8120u

typedef int (*PFN_SDL_InitSubSystem)(Uint32);
typedef const char *(*PFN_SDL_GetError)(void);
typedef SDL_AudioDeviceID (*PFN_SDL_OpenAudioDevice)(const char *, int, const SDL_AudioSpec *, SDL_AudioSpec *, int);
typedef void (*PFN_SDL_CloseAudioDevice)(SDL_AudioDeviceID);
typedef void (*PFN_SDL_PauseAudioDevice)(SDL_AudioDeviceID, int);
typedef int (*PFN_SDL_QueueAudio)(SDL_AudioDeviceID, const void *, Uint32);
typedef Uint32 (*PFN_SDL_GetQueuedAudioSize)(SDL_AudioDeviceID);
typedef void (*PFN_SDL_ClearQueuedAudio)(SDL_AudioDeviceID);

static void *sdl_handle;
static PFN_SDL_InitSubSystem pSDL_InitSubSystem;
static PFN_SDL_GetError pSDL_GetError;
static PFN_SDL_OpenAudioDevice pSDL_OpenAudioDevice;
static PFN_SDL_CloseAudioDevice pSDL_CloseAudioDevice;
static PFN_SDL_PauseAudioDevice pSDL_PauseAudioDevice;
static PFN_SDL_QueueAudio pSDL_QueueAudio;
static PFN_SDL_GetQueuedAudioSize pSDL_GetQueuedAudioSize;
static PFN_SDL_ClearQueuedAudio pSDL_ClearQueuedAudio;
static pthread_once_t sdl_once = PTHREAD_ONCE_INIT;
static int sdl_ready;

static void bridge_log(const char *msg) {
    fprintf(stderr, "[BENDER][OpenSLES] %s\n", msg);
    fflush(stderr);
}

static void load_sdl_once(void) {
    const char *names[] = {"libSDL2-2.0.so.0", "libSDL2.so", NULL};
    for (int i=0; names[i] != NULL && sdl_handle == NULL; ++i) {
        sdl_handle = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
        if (sdl_handle != NULL) {
            fprintf(stderr, "[BENDER][OpenSLES] SDL2 carregada: %s\n", names[i]);
        }
    }
    if (!sdl_handle) {
        bridge_log("ERRO: SDL2 do firmware não encontrada");
        return;
    }
#define LOAD_SDL(name) do { *(void **)(&p##name) = dlsym(sdl_handle, #name); if (!p##name) { fprintf(stderr,"[BENDER][OpenSLES] ERRO: símbolo %s ausente\n",#name); return; } } while (0)
    LOAD_SDL(SDL_InitSubSystem);
    LOAD_SDL(SDL_GetError);
    LOAD_SDL(SDL_OpenAudioDevice);
    LOAD_SDL(SDL_CloseAudioDevice);
    LOAD_SDL(SDL_PauseAudioDevice);
    LOAD_SDL(SDL_QueueAudio);
    LOAD_SDL(SDL_GetQueuedAudioSize);
    LOAD_SDL(SDL_ClearQueuedAudio);
#undef LOAD_SDL
    if (pSDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr,"[BENDER][OpenSLES] ERRO SDL_InitSubSystem: %s\n", pSDL_GetError());
        return;
    }
    sdl_ready = 1;
}

static int ensure_sdl(void) {
    pthread_once(&sdl_once, load_sdl_once);
    return sdl_ready;
}

static int iid_equal(SLInterfaceID a, SLInterfaceID b) {
    return a && b && memcmp(a,b,sizeof(*a)) == 0;
}

enum { KIND_ENGINE=1, KIND_MIX=2, KIND_PLAYER=3 };

typedef struct BenderBase {
    const struct SLObjectItf_ *object_vtbl;
    int kind;
    SLuint32 state;
} BenderBase;

typedef struct BenderEngine {
    const struct SLObjectItf_ *object_vtbl;
    int kind;
    SLuint32 state;
    const struct SLEngineItf_ *engine_vtbl;
} BenderEngine;

#define MAX_PENDING 64

typedef struct BenderPlayer {
    const struct SLObjectItf_ *object_vtbl;
    int kind;
    SLuint32 state;
    const struct SLPlayItf_ *play_vtbl;
    const struct SLAndroidSimpleBufferQueueItf_ *queue_vtbl;
    const struct SLAndroidConfigurationItf_ *config_vtbl;
    const struct SLVolumeItf_ *volume_vtbl;
    SDL_AudioDeviceID device;
    int rate;
    int channels;
    int bits;
    Uint16 sdl_format;
    SLuint32 play_state;
    SLuint32 stream_type;
    SLuint32 performance_mode;
    SLmillibel volume_level;
    SLboolean muted;
    pthread_mutex_t lock;
    pthread_t thread;
    int thread_started;
    int running;
    slAndroidSimpleBufferQueueCallback queue_cb;
    void *queue_ctx;
    SLuint32 pending_sizes[MAX_PENDING];
    unsigned pending_head;
    unsigned pending_count;
    uint64_t total_enqueued;
    uint64_t total_callbacked;
    SLuint32 completed_index;
} BenderPlayer;

#define CONTAINER_OF(ptr,type,member) ((type *)((char *)(ptr) - offsetof(type, member)))
static BenderPlayer *player_from_play(SLPlayItf self) { return CONTAINER_OF(self,BenderPlayer,play_vtbl); }
static BenderPlayer *player_from_queue(SLAndroidSimpleBufferQueueItf self) { return CONTAINER_OF(self,BenderPlayer,queue_vtbl); }
static BenderPlayer *player_from_config(SLAndroidConfigurationItf self) { return CONTAINER_OF(self,BenderPlayer,config_vtbl); }
static BenderPlayer *player_from_volume(SLVolumeItf self) { return CONTAINER_OF(self,BenderPlayer,volume_vtbl); }

static SLresult object_realize(SLObjectItf self, SLboolean async);
static SLresult object_get_state(SLObjectItf self, SLuint32 *state);
static SLresult object_get_interface(SLObjectItf self, const SLInterfaceID iid, void *out);
static void object_destroy(SLObjectItf self);
static SLresult engine_create_player(SLEngineItf,SLObjectItf *,SLDataSource *,SLDataSink *,SLuint32,const SLInterfaceID *,const SLboolean *);
static SLresult engine_create_mix(SLEngineItf,SLObjectItf *,SLuint32,const SLInterfaceID *,const SLboolean *);
static SLresult play_set_state(SLPlayItf,SLuint32);
static SLresult play_get_state(SLPlayItf,SLuint32 *);
static SLresult play_get_duration(SLPlayItf,SLmillisecond *);
static SLresult play_get_position(SLPlayItf,SLmillisecond *);
static SLresult bq_enqueue(SLAndroidSimpleBufferQueueItf,const void *,SLuint32);
static SLresult bq_clear(SLAndroidSimpleBufferQueueItf);
static SLresult bq_get_state(SLAndroidSimpleBufferQueueItf,SLAndroidSimpleBufferQueueState *);
static SLresult bq_register(SLAndroidSimpleBufferQueueItf,slAndroidSimpleBufferQueueCallback,void *);
static SLresult cfg_set(SLAndroidConfigurationItf,const SLchar *,const void *,SLuint32);
static SLresult cfg_get(SLAndroidConfigurationItf,const SLchar *,SLuint32 *,void *);

static const struct SLObjectItf_ object_vtbl = {
    .Realize=object_realize,
    .GetState=object_get_state,
    .GetInterface=object_get_interface,
    .Destroy=object_destroy
};
static const struct SLEngineItf_ engine_vtbl = {
    .CreateAudioPlayer=engine_create_player,
    .CreateOutputMix=engine_create_mix
};
static const struct SLPlayItf_ play_vtbl = {
    .SetPlayState=play_set_state,
    .GetPlayState=play_get_state,
    .GetDuration=play_get_duration,
    .GetPosition=play_get_position
};
static const struct SLAndroidSimpleBufferQueueItf_ queue_vtbl = {
    .Enqueue=bq_enqueue,
    .Clear=bq_clear,
    .GetState=bq_get_state,
    .RegisterCallback=bq_register
};
static const struct SLAndroidConfigurationItf_ config_vtbl = {
    .SetConfiguration=cfg_set,
    .GetConfiguration=cfg_get
};

static SLresult vol_set_level(SLVolumeItf self,SLmillibel v){ player_from_volume(self)->volume_level=v; return SL_RESULT_SUCCESS; }
static SLresult vol_get_level(SLVolumeItf self,SLmillibel *v){ if(!v)return SL_RESULT_PARAMETER_INVALID; *v=player_from_volume(self)->volume_level; return SL_RESULT_SUCCESS; }
static SLresult vol_get_max(SLVolumeItf self,SLmillibel *v){ (void)self; if(!v)return SL_RESULT_PARAMETER_INVALID; *v=0; return SL_RESULT_SUCCESS; }
static SLresult vol_set_mute(SLVolumeItf self,SLboolean v){ player_from_volume(self)->muted=v; return SL_RESULT_SUCCESS; }
static SLresult vol_get_mute(SLVolumeItf self,SLboolean *v){ if(!v)return SL_RESULT_PARAMETER_INVALID; *v=player_from_volume(self)->muted; return SL_RESULT_SUCCESS; }
static SLresult vol_enable_stereo(SLVolumeItf self,SLboolean v){ (void)self;(void)v;return SL_RESULT_SUCCESS; }
static SLresult vol_is_stereo(SLVolumeItf self,SLboolean *v){ (void)self;if(!v)return SL_RESULT_PARAMETER_INVALID;*v=SL_BOOLEAN_FALSE;return SL_RESULT_SUCCESS; }
static SLresult vol_set_stereo(SLVolumeItf self,SLpermille v){(void)self;(void)v;return SL_RESULT_SUCCESS;}
static SLresult vol_get_stereo(SLVolumeItf self,SLpermille *v){(void)self;if(!v)return SL_RESULT_PARAMETER_INVALID;*v=0;return SL_RESULT_SUCCESS;}
static const struct SLVolumeItf_ volume_vtbl = {
    vol_set_level,vol_get_level,vol_get_max,vol_set_mute,vol_get_mute,
    vol_enable_stereo,vol_is_stereo,vol_set_stereo,vol_get_stereo
};

static int open_player_device(BenderPlayer *p) {
    if (p->device) return 1;
    if (!ensure_sdl()) return 0;
    SDL_AudioSpec want,have;
    memset(&want,0,sizeof(want)); memset(&have,0,sizeof(have));
    want.freq = p->rate > 0 ? p->rate : 48000;
    want.channels = (Uint8)((p->channels > 0 && p->channels <= 8) ? p->channels : 2);
    want.format = p->sdl_format;
    want.samples = 512;
    p->device = pSDL_OpenAudioDevice(NULL,0,&want,&have,0);
    if (!p->device) {
        fprintf(stderr,"[BENDER][OpenSLES] ERRO SDL_OpenAudioDevice %dHz %dch: %s\n",want.freq,want.channels,pSDL_GetError());
        return 0;
    }
    if (have.freq != want.freq || have.channels != want.channels || have.format != want.format) {
        fprintf(stderr,"[BENDER][OpenSLES] ERRO: formato SDL inesperado pedido=%d/%u/0x%x obtido=%d/%u/0x%x\n",
            want.freq,(unsigned)want.channels,(unsigned)want.format,have.freq,(unsigned)have.channels,(unsigned)have.format);
        pSDL_CloseAudioDevice(p->device); p->device=0; return 0;
    }
    pSDL_PauseAudioDevice(p->device,1);
    fprintf(stderr,"[BENDER][OpenSLES] dispositivo SDL aberto: %d Hz, %u canais, formato 0x%x\n",have.freq,(unsigned)have.channels,(unsigned)have.format);
    return 1;
}

static uint64_t consumed_bytes_locked(BenderPlayer *p) {
    uint64_t queued = p->device ? (uint64_t)pSDL_GetQueuedAudioSize(p->device) : 0;
    if (queued > p->total_enqueued) queued = p->total_enqueued;
    return p->total_enqueued - queued;
}

static void *player_pump(void *arg) {
    BenderPlayer *p=(BenderPlayer *)arg;
    for (;;) {
        slAndroidSimpleBufferQueueCallback cb=NULL;
        void *ctx=NULL;
        SLAndroidSimpleBufferQueueItf qhandle=NULL;
        pthread_mutex_lock(&p->lock);
        if (!p->running) { pthread_mutex_unlock(&p->lock); break; }
        if (p->device && p->play_state==SL_PLAYSTATE_PLAYING && p->pending_count>0) {
            uint64_t consumed=consumed_bytes_locked(p);
            SLuint32 front=p->pending_sizes[p->pending_head];
            if (consumed >= p->total_callbacked + (uint64_t)front) {
                p->total_callbacked += front;
                p->pending_head=(p->pending_head+1u)%MAX_PENDING;
                p->pending_count--;
                p->completed_index++;
                cb=p->queue_cb; ctx=p->queue_ctx;
                qhandle=(SLAndroidSimpleBufferQueueItf)&p->queue_vtbl;
            }
        }
        pthread_mutex_unlock(&p->lock);
        if (cb) cb(qhandle,ctx);
        else usleep(2000);
    }
    return NULL;
}

static SLresult object_realize(SLObjectItf self, SLboolean async) {
    (void)async;
    if (!self) return SL_RESULT_PARAMETER_INVALID;
    BenderBase *b=(BenderBase *)self;
    if (b->kind==KIND_PLAYER) {
        BenderPlayer *p=(BenderPlayer *)self;
        if (!open_player_device(p)) return SL_RESULT_RESOURCE_ERROR;
    }
    b->state=SL_OBJECT_STATE_REALIZED;
    return SL_RESULT_SUCCESS;
}
static SLresult object_get_state(SLObjectItf self, SLuint32 *state) {
    if(!self||!state) return SL_RESULT_PARAMETER_INVALID;
    *state=((BenderBase *)self)->state;
    return SL_RESULT_SUCCESS;
}
static SLresult object_get_interface(SLObjectItf self, const SLInterfaceID iid, void *out) {
    if(!self||!iid||!out)return SL_RESULT_PARAMETER_INVALID;
    BenderBase *b=(BenderBase *)self;
    if(b->kind==KIND_ENGINE && iid_equal(iid,SL_IID_ENGINE)) {
        BenderEngine *e=(BenderEngine *)self; *(SLEngineItf *)out=(SLEngineItf)&e->engine_vtbl; return SL_RESULT_SUCCESS;
    }
    if(b->kind==KIND_PLAYER) {
        BenderPlayer *p=(BenderPlayer *)self;
        if(iid_equal(iid,SL_IID_PLAY)){ *(SLPlayItf *)out=(SLPlayItf)&p->play_vtbl; return SL_RESULT_SUCCESS; }
        if(iid_equal(iid,SL_IID_BUFFERQUEUE)||iid_equal(iid,SL_IID_ANDROIDSIMPLEBUFFERQUEUE)){ *(SLAndroidSimpleBufferQueueItf *)out=(SLAndroidSimpleBufferQueueItf)&p->queue_vtbl; return SL_RESULT_SUCCESS; }
        if(iid_equal(iid,SL_IID_ANDROIDCONFIGURATION)){ *(SLAndroidConfigurationItf *)out=(SLAndroidConfigurationItf)&p->config_vtbl; return SL_RESULT_SUCCESS; }
        if(iid_equal(iid,SL_IID_VOLUME)){ *(SLVolumeItf *)out=(SLVolumeItf)&p->volume_vtbl; return SL_RESULT_SUCCESS; }
    }
    return SL_RESULT_FEATURE_UNSUPPORTED;
}
static void object_destroy(SLObjectItf self) {
    if(!self)return;
    BenderBase *b=(BenderBase *)self;
    if(b->kind==KIND_PLAYER) {
        BenderPlayer *p=(BenderPlayer *)self;
        pthread_mutex_lock(&p->lock); p->running=0; pthread_mutex_unlock(&p->lock);
        if(p->thread_started && !pthread_equal(pthread_self(),p->thread)) pthread_join(p->thread,NULL);
        if(p->device){pSDL_PauseAudioDevice(p->device,1);pSDL_ClearQueuedAudio(p->device);pSDL_CloseAudioDevice(p->device);p->device=0;}
        pthread_mutex_destroy(&p->lock);
        free(p);
    } else free(b);
}

static SLresult engine_create_mix(SLEngineItf self, SLObjectItf *out, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req) {
    (void)self;(void)n;(void)ids;(void)req;
    if(!out)return SL_RESULT_PARAMETER_INVALID;
    BenderBase *m=(BenderBase *)calloc(1,sizeof(*m)); if(!m)return SL_RESULT_MEMORY_FAILURE;
    m->object_vtbl=&object_vtbl;m->kind=KIND_MIX;m->state=SL_OBJECT_STATE_UNREALIZED;
    *out=(SLObjectItf)&m->object_vtbl; return SL_RESULT_SUCCESS;
}
static SLresult engine_create_player(SLEngineItf self, SLObjectItf *out, SLDataSource *src, SLDataSink *sink, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req) {
    (void)self;(void)sink;
    if(!out||!src||!src->pFormat)return SL_RESULT_PARAMETER_INVALID;
    for(SLuint32 i=0;i<n;i++) {
        if(req && req[i] && !(iid_equal(ids[i],SL_IID_BUFFERQUEUE)||iid_equal(ids[i],SL_IID_ANDROIDSIMPLEBUFFERQUEUE)||iid_equal(ids[i],SL_IID_ANDROIDCONFIGURATION)||iid_equal(ids[i],SL_IID_VOLUME))) return SL_RESULT_FEATURE_UNSUPPORTED;
    }
    SLDataFormat_PCM *fmt=(SLDataFormat_PCM *)src->pFormat;
    if(fmt->formatType!=SL_DATAFORMAT_PCM)return SL_RESULT_FEATURE_UNSUPPORTED;
    BenderPlayer *p=(BenderPlayer *)calloc(1,sizeof(*p)); if(!p)return SL_RESULT_MEMORY_FAILURE;
    p->object_vtbl=&object_vtbl;p->kind=KIND_PLAYER;p->state=SL_OBJECT_STATE_UNREALIZED;
    p->play_vtbl=&play_vtbl;p->queue_vtbl=&queue_vtbl;p->config_vtbl=&config_vtbl;p->volume_vtbl=&volume_vtbl;
    p->channels=(int)fmt->numChannels; p->rate=(int)(fmt->samplesPerSec/1000u); p->bits=(int)fmt->bitsPerSample;
    if(p->rate<=0) p->rate=48000;
    if(p->channels<=0) p->channels=2;
    p->sdl_format=(p->bits==32)?AUDIO_F32LSB:AUDIO_S16LSB;
    p->play_state=SL_PLAYSTATE_STOPPED; p->running=1;
    pthread_mutex_init(&p->lock,NULL);
    if(pthread_create(&p->thread,NULL,player_pump,p)!=0){pthread_mutex_destroy(&p->lock);free(p);return SL_RESULT_RESOURCE_ERROR;}
    p->thread_started=1;
    fprintf(stderr,"[BENDER][OpenSLES] player criado: %d Hz, %d canais, %d bits\n",p->rate,p->channels,p->bits);
    *out=(SLObjectItf)&p->object_vtbl; return SL_RESULT_SUCCESS;
}

static SLresult play_set_state(SLPlayItf self,SLuint32 state){
    BenderPlayer *p=player_from_play(self); if(!p)return SL_RESULT_PARAMETER_INVALID;
    pthread_mutex_lock(&p->lock);
    if(state==SL_PLAYSTATE_PLAYING){ if(!p->device && !open_player_device(p)){pthread_mutex_unlock(&p->lock);return SL_RESULT_RESOURCE_ERROR;} pSDL_PauseAudioDevice(p->device,0); }
    else if(state==SL_PLAYSTATE_PAUSED){ if(p->device)pSDL_PauseAudioDevice(p->device,1); }
    else if(state==SL_PLAYSTATE_STOPPED){ if(p->device){pSDL_PauseAudioDevice(p->device,1);pSDL_ClearQueuedAudio(p->device);} p->pending_head=p->pending_count=0;p->total_enqueued=p->total_callbacked=0;p->completed_index=0; }
    else { pthread_mutex_unlock(&p->lock); return SL_RESULT_PARAMETER_INVALID; }
    p->play_state=state; pthread_mutex_unlock(&p->lock); return SL_RESULT_SUCCESS;
}
static SLresult play_get_state(SLPlayItf self,SLuint32 *state){if(!state)return SL_RESULT_PARAMETER_INVALID;BenderPlayer*p=player_from_play(self);pthread_mutex_lock(&p->lock);*state=p->play_state;pthread_mutex_unlock(&p->lock);return SL_RESULT_SUCCESS;}
static SLresult play_get_duration(SLPlayItf self,SLmillisecond *m){(void)self;if(!m)return SL_RESULT_PARAMETER_INVALID;*m=SL_TIME_UNKNOWN;return SL_RESULT_SUCCESS;}
static SLresult play_get_position(SLPlayItf self,SLmillisecond *m){
    if(!m) return SL_RESULT_PARAMETER_INVALID;
    BenderPlayer*p=player_from_play(self);
    pthread_mutex_lock(&p->lock);
    uint64_t consumed=consumed_bytes_locked(p); uint64_t bpf=(uint64_t)(p->channels>0?p->channels:2)*(uint64_t)(p->bits==32?4:2);
    *m=(SLmillisecond)((bpf&&p->rate>0)?((consumed*1000u)/(bpf*(uint64_t)p->rate)):0u); pthread_mutex_unlock(&p->lock); return SL_RESULT_SUCCESS;
}

static SLresult bq_enqueue(SLAndroidSimpleBufferQueueItf self,const void *buf,SLuint32 size){
    if(!buf||size==0) return SL_RESULT_PARAMETER_INVALID;
    BenderPlayer*p=player_from_queue(self);
    pthread_mutex_lock(&p->lock);
    if(!p->device && p->state==SL_OBJECT_STATE_REALIZED && !open_player_device(p)){pthread_mutex_unlock(&p->lock);return SL_RESULT_RESOURCE_ERROR;}
    if(!p->device){pthread_mutex_unlock(&p->lock);return SL_RESULT_PRECONDITIONS_VIOLATED;}
    if(p->pending_count>=MAX_PENDING){pthread_mutex_unlock(&p->lock);return SL_RESULT_RESOURCE_ERROR;}
    if(pSDL_QueueAudio(p->device,buf,size)!=0){fprintf(stderr,"[BENDER][OpenSLES] ERRO SDL_QueueAudio: %s\n",pSDL_GetError());pthread_mutex_unlock(&p->lock);return SL_RESULT_RESOURCE_ERROR;}
    unsigned pos=(p->pending_head+p->pending_count)%MAX_PENDING;p->pending_sizes[pos]=size;p->pending_count++;p->total_enqueued+=size;
    pthread_mutex_unlock(&p->lock);return SL_RESULT_SUCCESS;
}
static SLresult bq_clear(SLAndroidSimpleBufferQueueItf self){BenderPlayer*p=player_from_queue(self);pthread_mutex_lock(&p->lock);if(p->device)pSDL_ClearQueuedAudio(p->device);p->pending_head=p->pending_count=0;p->total_enqueued=p->total_callbacked=0;pthread_mutex_unlock(&p->lock);return SL_RESULT_SUCCESS;}
static SLresult bq_get_state(SLAndroidSimpleBufferQueueItf self,SLAndroidSimpleBufferQueueState *s){if(!s)return SL_RESULT_PARAMETER_INVALID;BenderPlayer*p=player_from_queue(self);pthread_mutex_lock(&p->lock);s->count=p->pending_count;s->index=p->completed_index;pthread_mutex_unlock(&p->lock);return SL_RESULT_SUCCESS;}
static SLresult bq_register(SLAndroidSimpleBufferQueueItf self,slAndroidSimpleBufferQueueCallback cb,void *ctx){BenderPlayer*p=player_from_queue(self);pthread_mutex_lock(&p->lock);p->queue_cb=cb;p->queue_ctx=ctx;pthread_mutex_unlock(&p->lock);return SL_RESULT_SUCCESS;}

static SLresult cfg_set(SLAndroidConfigurationItf self,const SLchar *key,const void *value,SLuint32 size){
    if(!value||size<sizeof(SLuint32)) return SL_RESULT_PARAMETER_INVALID;
    BenderPlayer*p=player_from_config(self);
    SLuint32 v=*(const SLuint32 *)value;
    const char *k=(const char *)key;if(k && (strstr(k,"performance")||strstr(k,"PERFORMANCE")))p->performance_mode=v;else p->stream_type=v;return SL_RESULT_SUCCESS;
}
static SLresult cfg_get(SLAndroidConfigurationItf self,const SLchar *key,SLuint32 *size,void *value){
    if(!size) return SL_RESULT_PARAMETER_INVALID;
    if(*size<sizeof(SLuint32)){*size=sizeof(SLuint32);return SL_RESULT_PARAMETER_INVALID;}
    BenderPlayer*p=player_from_config(self);
    SLuint32 v=p->stream_type;const char*k=(const char *)key;if(k&&(strstr(k,"performance")||strstr(k,"PERFORMANCE")))v=p->performance_mode;
    if(value) *(SLuint32 *)value=v;
    *size=sizeof(SLuint32);
    return SL_RESULT_SUCCESS;
}

__attribute__((visibility("default"))) SLresult slCreateEngine(SLObjectItf *pEngine, SLuint32 numOptions, const void *pEngineOptions, SLuint32 numInterfaces, const SLInterfaceID *pInterfaceIds, const SLboolean *pInterfaceRequired) {
    (void)numOptions;(void)pEngineOptions;(void)numInterfaces;(void)pInterfaceIds;(void)pInterfaceRequired;
    if(!pEngine)return SL_RESULT_PARAMETER_INVALID;
    BenderEngine *e=(BenderEngine *)calloc(1,sizeof(*e));if(!e)return SL_RESULT_MEMORY_FAILURE;
    e->object_vtbl=&object_vtbl;e->kind=KIND_ENGINE;e->state=SL_OBJECT_STATE_UNREALIZED;e->engine_vtbl=&engine_vtbl;
    *pEngine=(SLObjectItf)&e->object_vtbl;
    bridge_log("bridge OpenSL ES -> SDL2 carregada");
    return SL_RESULT_SUCCESS;
}

__attribute__((constructor)) static void bender_opensles_loaded(void){bridge_log("libOpenSLES.so encontrada pelo loader");}
